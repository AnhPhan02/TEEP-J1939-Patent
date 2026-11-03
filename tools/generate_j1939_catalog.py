#!/usr/bin/env python3
"""
generate_j1939_catalog.py - Phase 1 single-source J1939 catalog generator.

Reads the 179-record master workbook plus the approved Phase 0 policy and
produces every firmware/UI catalog artifact from that one source:

    MID/j1939_signal_definitions.{c,h}                  firmware signal database
                                                        (compact Flash catalog)
    MID/j1939_customer_profiles.{c,h}                    customer key masks
    generated/j1939_catalog_test_vectors_generated.h    host-test vectors
    generated/j1939_ui_catalog.json                     UI catalog
    generated/j1939_catalog_manifest.json               UI/FW handshake data
    generated/j1939_catalog_report.md                   validation report

Any rule that cannot be resolved deterministically (unknown rate label, event
mode without an approved policy, overlap, out-of-range raw bound, count
mismatch, ...) aborts generation. Nothing is guessed.

Usage:
    python tools/generate_j1939_catalog.py           regenerate all artifacts
    python tools/generate_j1939_catalog.py --check   fail if any artifact is stale
"""

import argparse
import dataclasses
import hashlib
import json
import math
import re
import struct
import sys
import zlib
from fractions import Fraction
from pathlib import Path

GENERATOR_VERSION = "1.0.0"
CATALOG_SCHEMA_VERSION = 1

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MASTER = REPO_ROOT / "doc" / "J1939_179_SPN_Master_Specification_Table.xlsx"
DEFAULT_POLICY = REPO_ROOT / "config" / "j1939_phase0_policy.json"
DEFAULT_NETWORK = REPO_ROOT / "config" / "j1939_pgn_network.json"
DEFAULT_PROFILES = REPO_ROOT / "config" / "j1939_customer_profiles.json"
CATALOG_BASENAME = "j1939_signal_definitions"

MASTER_SHEET = "Master 179 SPN Database"
HEADER_ROW = 5
FIRST_DATA_ROW = 6
EXPECTED_HEADER = (
    "SPN", "Parameter Name", "PGN (Dec)", "PGN (Hex)", "PGN Acronym",
    "Category", "Tx Rate", "Byte", "Bit", "Bits", "Resolution", "Offset",
    "Physical Range", "Unit",
)

CLASSICAL_CAN_MAX_DLC = 8
J1939_TP_MAX_BYTES = 1785
TP_DT_BYTES_PER_PACKET = 7
SCALAR_MAX_BITS = 32
INDEX_INVALID_U8 = 0xFF

# Enum values match J1939_Transmission_Mode_t in MID/j1939_signal_definitions.h
# so the legacy and generated catalogs can be cross-checked during migration.
TX_MODES = {
    "PERIODIC": 0,
    "ON_CHANGE": 1,
    "ENGINE_SPEED_DEPENDENT": 2,
    "ON_REQUEST": 3,
}

CONTROL_KINDS = {
    "continuous": 0,
    "boolean": 1,
    "enum": 2,
    "blob": 3,
}

SPECIAL_CODECS = {
    "NONE": 0,
    "ASCII_BLOB": 1,
}

SIG_FLAG_GLOBAL_CONTROL = 0x01
SIG_FLAG_BLOB = 0x02
SIG_FLAG_DUPLICATE_SPN = 0x04
SIG_FLAG_RAW_BOUND_ADJUSTED = 0x08

PGN_FLAG_REQUIRES_TP = 0x01
PGN_FLAG_VARIABLE_LENGTH = 0x02
PGN_FLAG_PROVISIONAL = 0x04

PROFILE_FLAG_LOCKED = 0x01
PROFILE_FLAG_REQUIRES_TP = 0x02
PROFILE_ID_ALL = 0

# Units whose values are discrete state codes rather than measured quantities.
DISCRETE_UNITS = ("State", "SA")

# Source-text repairs to plain ASCII. Every applied repair is listed in the
# report; any other non-ASCII character fails generation.
TEXT_REPAIRS = (
    ("–", "-"),  # en dash
    ("—", "-"),  # em dash
)


class CatalogError(Exception):
    """Raised with every validation failure found in one generation pass."""

    def __init__(self, errors):
        self.errors = list(errors)
        super().__init__("\n".join(self.errors))


@dataclasses.dataclass(frozen=True)
class MasterRow:
    row: int
    spn: object
    name: object
    pgn_dec: object
    pgn_hex: object
    acronym: object
    category: object
    rate: object
    byte: object
    bit: object
    bits: object
    resolution: object
    offset: object
    physical_range: object
    unit: object


# =============================================================================
# Input
# =============================================================================

def load_master(path, sheet_name=MASTER_SHEET):
    """Returns the data rows exactly as stored; validation happens later."""
    import openpyxl

    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    try:
        if sheet_name not in wb.sheetnames:
            raise CatalogError([f"sheet '{sheet_name}' not found in {path}"])
        ws = wb[sheet_name]
        rows = list(ws.iter_rows(min_row=HEADER_ROW, values_only=True))
    finally:
        wb.close()

    header = tuple(rows[0][:len(EXPECTED_HEADER)]) if rows else ()
    if header != EXPECTED_HEADER:
        raise CatalogError([f"row {HEADER_ROW}: unexpected header {header!r}"])

    data = []
    trailing = False
    for offset, values in enumerate(rows[1:]):
        row_no = FIRST_DATA_ROW + offset
        values = tuple(values[:len(EXPECTED_HEADER)])
        if all(v is None or (isinstance(v, str) and not v.strip()) for v in values):
            trailing = True
            continue
        if trailing:
            raise CatalogError([f"row {row_no}: data after an empty row"])
        data.append(MasterRow(row_no, *values))
    return data


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# =============================================================================
# Field parsing
# =============================================================================

def _int_field(errors, row, field, value, minimum=None, maximum=None):
    if isinstance(value, bool):
        value = None
    if isinstance(value, float) and value.is_integer():
        value = int(value)
    if not isinstance(value, int):
        errors.append(f"row {row}: {field} must be an integer, got {value!r}")
        return None
    if (minimum is not None and value < minimum) or \
       (maximum is not None and value > maximum):
        errors.append(f"row {row}: {field}={value} outside {minimum}..{maximum}")
        return None
    return value


def _decimal(value):
    """Exact rational from the decimal the workbook displays."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"not numeric: {value!r}")
    return Fraction(repr(value)) if isinstance(value, float) else Fraction(value)


_RANGE_RE = re.compile(r"^\[\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\]$")


def _display_tolerance(decimal_text):
    """Half a unit of the last printed decimal, e.g. '250.996' -> 0.0005."""
    decimals = len(decimal_text.split(".")[1]) if "." in decimal_text else 0
    return Fraction(1, 2 * 10 ** decimals)


def _text(value):
    return " ".join(str(value).split()) if value is not None else ""


def _repair_text(text):
    applied = []
    for bad, good in TEXT_REPAIRS:
        if bad in text:
            text = text.replace(bad, good)
            applied.append((bad, good))
    return text, applied


def operational_raw_max(bits):
    """Largest J1939 'valid signal' raw value for byte-multiple lengths
    (0xFA, 0xFAFF, 0xFAFFFFFF); other lengths use the full bit capacity."""
    capacity = (1 << bits) - 1
    if bits >= 8 and bits % 8 == 0:
        return (0xFA << (bits - 8)) | ((1 << (bits - 8)) - 1)
    return capacity


def f32(value):
    return struct.unpack("<f", struct.pack("<f", float(value)))[0]


# =============================================================================
# Catalog model
# =============================================================================

def _resolve_rate(rate, aliases, pgn_policy):
    """Returns (mode, alias_period_ms) or raises ValueError if ambiguous."""
    alias = aliases.get(rate)
    policy_mode = pgn_policy.get("mode") if pgn_policy else None
    if alias is None and policy_mode is None:
        raise ValueError(f"unresolved transmission rule '{rate}' "
                         "(no rate alias and no approved PGN policy)")
    if alias is not None and policy_mode is not None and alias[0] != policy_mode:
        raise ValueError(f"rate alias mode {alias[0]} conflicts with "
                         f"PGN policy mode {policy_mode}")
    mode = policy_mode if policy_mode is not None else alias[0]
    if mode not in TX_MODES:
        raise ValueError(f"unknown transmission mode {mode}")
    return mode, (alias[1] if alias is not None else 0)


def _pgn_schedule(mode, alias_period, policy):
    """Returns (period_ms, min_interval_ms, change_threshold_pct)."""
    policy = policy or {}
    if mode == "PERIODIC":
        if alias_period <= 0:
            raise ValueError("PERIODIC mode without a positive period")
        return alias_period, 0, 0
    if mode == "ON_REQUEST":
        min_interval = int(policy.get("minimum_response_interval_ms", 0))
        if not policy.get("unsolicited_periodic_enabled", False):
            return 0, min_interval, 0
        # Customer-approved demo broadcast: the PGN stays ON_REQUEST (answers
        # requests once Phase 8 exists) and is also sent at this period.
        period = int(policy.get("unsolicited_period_ms", 0))
        if period <= 0:
            raise ValueError("ON_REQUEST unsolicited broadcast requires a positive "
                             "unsolicited_period_ms")
        return period, min_interval, 0
    if mode == "ENGINE_SPEED_DEPENDENT":
        fallback = policy.get("fallback_period_ms")
        if not fallback:
            raise ValueError("ENGINE_SPEED_DEPENDENT requires an approved fallback_period_ms")
        return int(fallback), 0, 0
    # ON_CHANGE
    fallback = policy.get("fallback_period_ms")
    rate_cap = policy.get("maximum_messages_per_second")
    if not fallback or not rate_cap:
        raise ValueError("ON_CHANGE requires approved fallback_period_ms and "
                         "maximum_messages_per_second")
    if 1000 % int(rate_cap) != 0:
        raise ValueError("maximum_messages_per_second must divide 1000 ms")
    threshold = policy.get("change_threshold_percent", 0)
    if float(threshold) != int(threshold):
        raise ValueError("change_threshold_percent must be an integer percentage")
    return int(fallback), 1000 // int(rate_cap), int(threshold)


def build_catalog(rows, policy, network, profiles_cfg):
    errors = []
    warnings = []
    repairs = []

    semantics = policy["catalog_semantics"]
    aliases = {}
    for entry in policy["rate_normalization"]:
        for label in entry["source_aliases"]:
            aliases[_text(label)] = (entry["mode"], int(entry["period_ms"]))
    pgn_policies = {int(k, 16): v for k, v in policy.get("pgn_policies", {}).items()}

    # ---- per-row parsing --------------------------------------------------
    signals = []
    for key, r in enumerate(rows, start=1):
        e0 = len(errors)
        spn = _int_field(errors, r.row, "SPN", r.spn, 1, 0x7FFFF)
        pgn = _int_field(errors, r.row, "PGN (Dec)", r.pgn_dec, 0, 0x3FFFF)
        byte = _int_field(errors, r.row, "Byte", r.byte, 1, J1939_TP_MAX_BYTES)
        bit = _int_field(errors, r.row, "Bit", r.bit, 1, 8)
        bits = _int_field(errors, r.row, "Bits", r.bits, 1, J1939_TP_MAX_BYTES * 8)

        pgn_hex = _text(r.pgn_hex)
        if pgn is not None:
            try:
                if int(pgn_hex, 16) != pgn:
                    errors.append(f"row {r.row}: PGN (Hex) {pgn_hex} != PGN (Dec) {pgn}")
            except ValueError:
                errors.append(f"row {r.row}: invalid PGN (Hex) {pgn_hex!r}")
        acronym = _text(r.acronym)
        if not re.fullmatch(r"PGN_[A-Z0-9_]+", acronym):
            errors.append(f"row {r.row}: invalid PGN acronym {acronym!r}")
        rate = _text(r.rate)
        if not rate:
            errors.append(f"row {r.row}: empty Tx Rate")
        name, applied = _repair_text(_text(r.name))
        for bad, good in applied:
            repairs.append(f"row {r.row} (signal_key {key}, SPN {r.spn}): "
                           f"name character U+{ord(bad):04X} replaced with '{good}'")
        if not name:
            errors.append(f"row {r.row}: empty Parameter Name")
        if any(ord(c) > 0x7E or ord(c) < 0x20 for c in name):
            errors.append(f"row {r.row}: non-printable/non-ASCII name {name!r}")

        try:
            resolution = _decimal(r.resolution)
            offset = _decimal(r.offset)
            if resolution <= 0:
                raise ValueError("resolution must be > 0")
            m = _RANGE_RE.match(_text(r.physical_range))
            if not m:
                raise ValueError(f"invalid Physical Range {r.physical_range!r}")
            phys_min, phys_max = Fraction(m.group(1)), Fraction(m.group(2))
            # Bounds are printed rounded; accept raw values that round to them.
            tol_min = _display_tolerance(m.group(1))
            tol_max = _display_tolerance(m.group(2))
            if phys_min > phys_max:
                raise ValueError("Physical Range min > max")
        except ValueError as ex:
            errors.append(f"row {r.row}: {ex}")
            continue
        if len(errors) != e0:
            continue

        signals.append({
            "key": key, "row": r.row, "spn": spn, "name": name,
            "pgn": pgn, "acronym": acronym[len("PGN_"):],
            "category": _text(r.category), "rate": rate, "unit": _text(r.unit),
            "start_byte": byte, "start_bit_in_byte": bit,
            "start_bit": (byte - 1) * 8 + (bit - 1), "bit_length": bits,
            "resolution": resolution, "offset": offset,
            "phys_min": phys_min, "phys_max": phys_max,
            "tol_min": tol_min, "tol_max": tol_max,
        })

    if len(rows) != semantics["record_count"]:
        errors.append(f"record count {len(rows)} != policy {semantics['record_count']}")
    if errors:
        raise CatalogError(errors)

    # ---- identity ----------------------------------------------------------
    by_pgn_spn = {}
    spn_pgns = {}
    for s in signals:
        ident = (s["pgn"], s["spn"])
        if ident in by_pgn_spn:
            errors.append(f"row {s['row']}: duplicate (PGN 0x{s['pgn']:04X}, SPN {s['spn']}) "
                          f"also at row {by_pgn_spn[ident]['row']}")
        by_pgn_spn[ident] = s
        spn_pgns.setdefault(s["spn"], []).append(s["pgn"])
    duplicate_spns = {spn: sorted(p) for spn, p in spn_pgns.items() if len(p) > 1}
    if len(spn_pgns) != semantics["unique_spn_count"]:
        errors.append(f"unique SPN count {len(spn_pgns)} != policy "
                      f"{semantics['unique_spn_count']}")

    # ---- signal classification and raw bounds ------------------------------
    for s in signals:
        bits = s["bit_length"]
        s["duplicate_spn"] = s["spn"] in duplicate_spns
        s["adjustments"] = []
        if bits > SCALAR_MAX_BITS:
            if s["unit"] != "ASCII" or bits % 8 or s["start_bit_in_byte"] != 1:
                errors.append(f"row {s['row']}: {bits}-bit field is not a byte-aligned "
                              "ASCII blob; no codec available")
                continue
            if s["resolution"] != 1 or s["offset"] != 0 or \
               s["phys_min"] != 0 or s["phys_max"] != 255:
                errors.append(f"row {s['row']}: ASCII blob must have byte scale 1/0 and range 0..255")
                continue
            s.update(kind="blob", codec="ASCII_BLOB", raw_min=0, raw_max=255,
                     raw_capacity=255, global_control=False)
            continue

        s["kind"] = ("boolean" if bits == 2 else "enum") \
            if s["unit"] in DISCRETE_UNITS else "continuous"
        s["codec"] = "NONE"
        s["global_control"] = s["kind"] == "continuous"
        capacity = (1 << bits) - 1
        res, off = s["resolution"], s["offset"]
        if s["phys_min"] < off:
            errors.append(f"row {s['row']}: physical min {float(s['phys_min']):g} "
                          f"below offset {float(off):g}")
            continue
        raw_min = max(0, math.ceil((s["phys_min"] - s["tol_min"] - off) / res))
        raw_max = math.floor((s["phys_max"] + s["tol_max"] - off) / res)
        exact_max = (s["phys_max"] - off) / res
        if exact_max > capacity:
            errors.append(f"row {s['row']}: source max {float(s['phys_max']):g} needs raw "
                          f"{float(exact_max):g}, exceeds {bits}-bit capacity {capacity}")
            continue
        limit = operational_raw_max(bits) if s["kind"] == "continuous" else capacity
        if raw_max > limit:
            s["adjustments"].append(f"source max {float(s['phys_max']):g} needs raw "
                                    f"{raw_max}, above J1939 operational limit "
                                    f"0x{limit:X}; clamped to {limit} "
                                    f"({float(off + limit * res):.9g})")
            raw_max = limit
        if abs(off + raw_min * res - s["phys_min"]) > s["tol_min"]:
            s["adjustments"].append(f"source min {float(s['phys_min']):g} is not on the "
                                    f"raw grid; raw_min {raw_min} "
                                    f"({float(off + raw_min * res):.9g})")
        if raw_max < limit and abs(off + raw_max * res - s["phys_max"]) > s["tol_max"]:
            s["adjustments"].append(f"source max {float(s['phys_max']):g} is not on the "
                                    f"raw grid; raw_max {raw_max} "
                                    f"({float(off + raw_max * res):.9g})")
        if s["kind"] == "continuous" and bits >= 16 and raw_max * 100 < limit:
            warnings.append(f"signal_key {s['key']} (PGN 0x{s['pgn']:04X} SPN {s['spn']}): "
                            f"raw_max {raw_max} uses <1% of the {bits}-bit operational "
                            f"range 0..{limit}; check source unit/physical range")
        if raw_min > raw_max:
            errors.append(f"row {s['row']}: empty raw range {raw_min}..{raw_max}")
        s.update(raw_min=raw_min, raw_max=raw_max, raw_capacity=capacity)
        for adj in s["adjustments"]:
            warnings.append(f"signal_key {s['key']} (PGN 0x{s['pgn']:04X} SPN {s['spn']}): {adj}")

    if errors:
        raise CatalogError(errors)

    # ---- PGN grouping -------------------------------------------------------
    groups = {}
    for s in signals:
        groups.setdefault(s["pgn"], []).append(s)

    if len(groups) != semantics["pgn_count"]:
        errors.append(f"PGN count {len(groups)} != policy {semantics['pgn_count']}")

    net_pgns = {int(k, 16): v for k, v in network["pgns"].items()}
    net_sa = network["source_addresses"]
    for extra in sorted(set(net_pgns) - set(groups)):
        errors.append(f"network config lists PGN 0x{extra:04X} not in master")
    for extra in sorted(set(pgn_policies) - set(groups)):
        errors.append(f"policy lists PGN 0x{extra:04X} not in master")

    pgns = []
    for pgn in sorted(groups):
        members = sorted(groups[pgn], key=lambda s: (s["start_bit"], s["key"]))
        tag = f"PGN 0x{pgn:04X}"

        for field in ("acronym", "rate"):
            values = sorted({m[field] for m in members})
            if len(values) != 1:
                errors.append(f"{tag}: inconsistent {field} across records: {values}")
        for a, b in zip(members, members[1:]):
            if a["start_bit"] + a["bit_length"] > b["start_bit"]:
                errors.append(f"{tag}: signal_key {a['key']} (bits {a['start_bit']}.."
                              f"{a['start_bit'] + a['bit_length'] - 1}) overlaps "
                              f"signal_key {b['key']} (starts at bit {b['start_bit']})")

        end_bit = max(m["start_bit"] + m["bit_length"] for m in members)
        required_length = (end_bit + 7) // 8
        # Single-frame PGNs are sent as a full 8-byte frame (unused bytes 0xFF);
        # longer PGNs carry exactly the bytes their fields occupy.
        requires_tp = required_length > CLASSICAL_CAN_MAX_DLC
        payload_length = required_length if requires_tp else CLASSICAL_CAN_MAX_DLC
        if payload_length > J1939_TP_MAX_BYTES:
            errors.append(f"{tag}: payload {payload_length} B exceeds J1939 TP limit")

        pol = pgn_policies.get(pgn)
        try:
            mode, alias_period = _resolve_rate(members[0]["rate"], aliases, pol)
            period, min_interval, threshold = _pgn_schedule(mode, alias_period, pol)
        except ValueError as ex:
            errors.append(f"{tag} ({members[0]['acronym']}): {ex}")
            continue

        variable = bool(pol and pol.get("variable_length"))
        if variable and pol.get("maximum_payload_bytes") != payload_length:
            errors.append(f"{tag}: policy maximum_payload_bytes "
                          f"{pol.get('maximum_payload_bytes')} != master {payload_length}")

        net = net_pgns.get(pgn)
        if net is None:
            errors.append(f"{tag}: missing from network config")
            continue
        if not 0 <= int(net["priority"]) <= 7:
            errors.append(f"{tag}: priority {net['priority']} outside 0..7")
        if net["owner"] not in net_sa:
            errors.append(f"{tag}: unknown owner {net['owner']}")
            continue

        flags = 0
        flags |= PGN_FLAG_REQUIRES_TP if requires_tp else 0
        flags |= PGN_FLAG_VARIABLE_LENGTH if variable else 0
        flags |= PGN_FLAG_PROVISIONAL if pol and pol.get("customer_confirmation_required") else 0

        pgns.append({
            "pgn": pgn, "acronym": members[0]["acronym"], "rate": members[0]["rate"],
            "mode": mode, "period_ms": period, "min_interval_ms": min_interval,
            "change_threshold_pct": threshold,
            "engineer_period_ms": int(pol.get("engineer_smooth_period_ms", 0)) if pol else 0,
            "payload_length": payload_length, "required_length": required_length,
            "requires_tp": requires_tp,
            "variable_length": variable,
            "provisional": bool(flags & PGN_FLAG_PROVISIONAL),
            "tp_dt_packets": math.ceil(payload_length / TP_DT_BYTES_PER_PACKET) if requires_tp else 0,
            "priority": int(net["priority"]), "owner": net["owner"],
            "source_address": int(net_sa[net["owner"]]),
            "flags": flags, "members": members,
            "policy": pol or {},
        })

    if errors:
        raise CatalogError(errors)

    # ---- catalog index order: PGN ascending, then bit position --------------
    ordered = []
    scales = []
    for pgn_index, p in enumerate(pgns):
        p["index"] = pgn_index
        p["first_signal"] = len(ordered)
        p["signal_count"] = len(p["members"])
        for s in p["members"]:
            s["index"] = len(ordered)
            s["pgn_index"] = pgn_index
            s["pgn_mode"] = p["mode"]
            scale = (s["resolution"], s["offset"])
            if scale not in scales:
                scales.append(scale)
            s["scale_index"] = scales.index(scale)
            flags = 0
            flags |= SIG_FLAG_GLOBAL_CONTROL if s["global_control"] else 0
            flags |= SIG_FLAG_BLOB if s["kind"] == "blob" else 0
            flags |= SIG_FLAG_DUPLICATE_SPN if s["duplicate_spn"] else 0
            flags |= SIG_FLAG_RAW_BOUND_ADJUSTED if s["adjustments"] else 0
            s["flags"] = flags
            ordered.append(s)

    if len(scales) > 255:
        raise CatalogError([f"{len(scales)} scale pairs exceed uint8 scale_index"])

    by_key = sorted(signals, key=lambda s: s["key"])

    # ---- policy cross-checks -------------------------------------------------
    vin = policy.get("vin")
    if vin:
        vin_pgn = next((p for p in pgns if p["pgn"] == vin["pgn"]), None)
        if vin_pgn is None or vin_pgn["payload_length"] != vin["maximum_bytes"]:
            errors.append("VIN policy maximum_bytes does not match master payload")
        tx = int(vin.get("demo_transmit_bytes", vin["maximum_bytes"]))
        if not CLASSICAL_CAN_MAX_DLC < tx <= int(vin["maximum_bytes"]):
            errors.append(f"VIN demo_transmit_bytes {tx} must be 9..maximum_bytes")
    dm1 = policy.get("dm1")
    if dm1 and not any(p["pgn"] == dm1["pgn"] for p in pgns):
        errors.append("DM1 policy PGN not present in master")

    # ---- customer profiles ---------------------------------------------------
    profiles = build_profiles(profiles_cfg, by_key, pgns, errors)

    if errors:
        raise CatalogError(errors)

    model = {
        "signals_by_key": by_key,
        "signals": ordered,
        "pgns": pgns,
        "scales": scales,
        "profiles": profiles,
        "duplicate_spns": duplicate_spns,
        "unique_spn_count": len(spn_pgns),
        "warnings": warnings,
        "repairs": repairs,
        "policy": policy,
        "network": network,
        "profiles_cfg": profiles_cfg,
    }
    model["catalog_crc32"] = catalog_crc32(model)
    return model


def build_profiles(cfg, by_key, pgns, errors):
    count = len(by_key)
    locked = bool(cfg.get("policy", {}).get("locked_required_set", True))
    entries = [{"id": PROFILE_ID_ALL, "name": "ALL", "keys": list(range(1, count + 1)),
                "expected_count": count, "expected_pgn_count": len(pgns),
                "locked": False, "generated": True}]
    seen_ids = {PROFILE_ID_ALL}
    seen_names = {"ALL"}
    for p in cfg["profiles"]:
        tag = f"customer profile {p.get('name')!r}"
        pid, name, keys = p["id"], p["name"], p["keys"]
        if not isinstance(pid, int) or not 1 <= pid <= 254 or pid in seen_ids:
            errors.append(f"{tag}: invalid or duplicate id {pid!r}")
        if not re.fullmatch(r"[A-Z][A-Z0-9_]*", name) or name in seen_names:
            errors.append(f"{tag}: invalid or duplicate name")
        seen_ids.add(pid)
        seen_names.add(name)
        if len(set(keys)) != len(keys):
            errors.append(f"{tag}: duplicate keys")
        bad = [k for k in keys if not isinstance(k, int) or not 1 <= k <= count]
        if bad:
            errors.append(f"{tag}: keys outside 1..{count}: {bad}")
            continue
        entries.append({"id": pid, "name": name, "keys": sorted(keys),
                        "expected_count": p["expected_count"],
                        "expected_pgn_count": p["expected_pgn_count"],
                        "locked": locked, "generated": False})

    pgn_by_value = {p["pgn"]: p for p in pgns}
    for e in entries:
        members = [by_key[k - 1] for k in e["keys"]]
        pgn_set = sorted({s["pgn"] for s in members})
        e["pgns"] = pgn_set
        e["tp_keys"] = [s["key"] for s in members if pgn_by_value[s["pgn"]]["requires_tp"]]
        e["requires_tp"] = bool(e["tp_keys"])
        mask = [0] * mask_bytes(len(by_key))
        for k in e["keys"]:
            mask[(k - 1) // 8] |= 1 << ((k - 1) % 8)
        e["mask"] = mask
        e["flags"] = (PROFILE_FLAG_LOCKED if e["locked"] else 0) | \
                     (PROFILE_FLAG_REQUIRES_TP if e["requires_tp"] else 0)
        e["load"] = bus_load([pgn_by_value[p] for p in pgn_set])
        if len(e["keys"]) != e["expected_count"]:
            errors.append(f"profile {e['name']}: {len(e['keys'])} keys != expected "
                          f"{e['expected_count']}")
        if len(pgn_set) != e["expected_pgn_count"]:
            errors.append(f"profile {e['name']}: {len(pgn_set)} PGNs != expected "
                          f"{e['expected_pgn_count']}")
    return entries


def mask_bytes(count):
    return (count + 7) // 8


def bus_load(pgns, bits_per_frame=160):
    """Fixed-cycle and deterministic-fallback frame rates (single frames).
    Approved unsolicited ON_REQUEST broadcasts count as fallback traffic."""
    fixed = sum(1000.0 / p["period_ms"] for p in pgns if p["mode"] == "PERIODIC")
    esd = sum(1000.0 / p["period_ms"] for p in pgns
              if p["mode"] in ("ENGINE_SPEED_DEPENDENT", "ON_REQUEST") and p["period_ms"])
    return {
        "fixed_fps": fixed,
        "fixed_plus_fallback_fps": fixed + esd,
        "fixed_load_500k_pct": fixed * bits_per_frame / 500000.0 * 100.0,
        "fixed_load_250k_pct": fixed * bits_per_frame / 250000.0 * 100.0,
        "baseline_load_500k_pct": (fixed + esd) * bits_per_frame / 500000.0 * 100.0,
    }


# =============================================================================
# Catalog CRC
# =============================================================================

def catalog_crc_bytes(model):
    """Canonical little-endian serialization; field order is part of the
    contract and is re-implemented by tests/test_j1939_catalog_generated.c."""
    out = bytearray(b"J1939CAT")
    out += struct.pack("<HHHHH", CATALOG_SCHEMA_VERSION, len(model["signals"]),
                       len(model["pgns"]), len(model["scales"]), len(model["profiles"]))
    for s in model["signals"]:
        out += struct.pack("<HIBHHIIBBBB", s["key"], s["spn"], s["pgn_index"],
                           s["start_bit"], s["bit_length"], s["raw_min"], s["raw_max"],
                           s["scale_index"], CONTROL_KINDS[s["kind"]], s["flags"],
                           SPECIAL_CODECS[s["codec"]])
    for res, off in model["scales"]:
        out += struct.pack("<ff", f32(res), f32(off))
    for p in model["pgns"]:
        out += struct.pack("<IHBHHHBBBBB", p["pgn"], p["first_signal"], p["signal_count"],
                           p["payload_length"], p["period_ms"], p["min_interval_ms"],
                           p["priority"], p["source_address"], TX_MODES[p["mode"]],
                           p["flags"], p["change_threshold_pct"])
    for e in model["profiles"]:
        out += struct.pack("<BBBB", e["id"], len(e["keys"]), len(e["pgns"]), e["flags"])
        out += bytes(e["mask"])
    return bytes(out)


def catalog_crc32(model):
    return zlib.crc32(catalog_crc_bytes(model)) & 0xFFFFFFFF


# =============================================================================
# Bit packing (reference for test vectors)
# =============================================================================

def pack_raw(buf, start_bit, bit_length, raw):
    for i in range(bit_length):
        byte, bit = divmod(start_bit + i, 8)
        if (raw >> i) & 1:
            buf[byte] |= 1 << bit
        else:
            buf[byte] &= ~(1 << bit) & 0xFF


def vector_values(s):
    lo, hi = s["raw_min"], s["raw_max"]
    return sorted({lo, lo + (hi - lo) // 2, hi})


# =============================================================================
# Rendering helpers
# =============================================================================

def _c_float(value):
    return f"{float(value).hex()}f"


def _c_ident(text):
    return re.sub(r"[^A-Z0-9_]", "_", text.upper())


def _hex_pgn(pgn):
    return f"0x{pgn:04X}"


def _num(frac):
    """JSON number for an exact decimal Fraction."""
    if frac.denominator == 1:
        return int(frac)
    return float(frac)


def _banner(filename, what, model):
    return (
        "/*\n"
        " * =============================================================================\n"
        f" * FILE : {filename}\n"
        f" * WHAT : {what}\n"
        " * GENERATED by tools/generate_j1939_catalog.py - DO NOT EDIT.\n"
        f" * SOURCE: doc/{DEFAULT_MASTER.name},\n"
        f" *         sheet \"{MASTER_SHEET}\"; config/j1939_phase0_policy.json\n"
        f" *         ({model['policy']['policy_version']}); config/j1939_pgn_network.json;\n"
        " *         config/j1939_customer_profiles.json.\n"
        f" * CATALOG CRC32: 0x{model['catalog_crc32']:08X}\n"
        " * =============================================================================\n"
        " */\n"
    )


# =============================================================================
# C catalog
# =============================================================================

def render_catalog_h(model):
    n_sig = len(model["signals"])
    lines = [_banner(f"{CATALOG_BASENAME}.h",
                     "Single compact firmware signal/PGN database (179 records in\n"
                     " *        60 PGNs) plus the expanded J1939_Signal_Definition_t view.",
                     model)]
    lines.append("""
#ifndef J1939_SIGNAL_DEFINITIONS_H
#define J1939_SIGNAL_DEFINITIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
""")
    max_payload = max(p["payload_length"] for p in model["pgns"])
    vin = model["policy"]["vin"]
    vin_tx = int(vin.get("demo_transmit_bytes", vin["maximum_bytes"]))
    vin_term = "'*'" if vin.get("automatic_terminator") else "0"
    lines.append(f"""#define J1939_CATALOG_SCHEMA_VERSION      {CATALOG_SCHEMA_VERSION}u
#define J1939_CATALOG_GENERATOR_VERSION   "{GENERATOR_VERSION}"
#define J1939_CATALOG_POLICY_VERSION      "{model['policy']['policy_version']}"
#define J1939_CATALOG_CRC32               0x{model['catalog_crc32']:08X}u

#define J1939_CATALOG_SIGNAL_COUNT        {n_sig}u
#define J1939_CATALOG_PGN_COUNT           {len(model['pgns'])}u
#define J1939_CATALOG_SCALE_COUNT         {len(model['scales'])}u
#define J1939_CATALOG_UNIQUE_SPN_COUNT    {model['unique_spn_count']}u
#define J1939_CATALOG_ACTIVE_MASK_BYTES   {mask_bytes(n_sig)}u
#define J1939_CATALOG_MAX_PAYLOAD_BYTES   {max_payload}u
#define J1939_CATALOG_KEY_MIN             1u
#define J1939_CATALOG_KEY_MAX             {n_sig}u
#define J1939_CATALOG_INDEX_INVALID       0xFFFFu

/* VIN (ASCII blob) on the bus: fixed payload of J1939_VIN_TX_BYTES = text,
 * then J1939_VIN_TERMINATOR (0 = none), then 0xFF (policy "vin"). */
#define J1939_VIN_TX_BYTES                {vin_tx}u
#define J1939_VIN_TERMINATOR              {vin_term}

/* Transmission modes. Values match J1939_Transmission_Mode_t. */
""")
    for name, value in TX_MODES.items():
        lines.append(f"#define J1939_CAT_TX_{name:<28}{value}u\n")
    lines.append("\n/* Signal control kinds (UI control and global-percent eligibility). */\n")
    for name, value in CONTROL_KINDS.items():
        lines.append(f"#define J1939_CAT_KIND_{name.upper():<27}{value}u\n")
    lines.append("\n/* Special codecs. */\n")
    for name, value in SPECIAL_CODECS.items():
        lines.append(f"#define J1939_CAT_CODEC_{name:<26}{value}u\n")
    lines.append(f"""
/* J1939_Catalog_Signal_t.flags */
#define J1939_CAT_SIG_GLOBAL_CONTROL      0x{SIG_FLAG_GLOBAL_CONTROL:02X}u
#define J1939_CAT_SIG_BLOB                0x{SIG_FLAG_BLOB:02X}u
#define J1939_CAT_SIG_DUPLICATE_SPN       0x{SIG_FLAG_DUPLICATE_SPN:02X}u
#define J1939_CAT_SIG_RAW_BOUND_ADJUSTED  0x{SIG_FLAG_RAW_BOUND_ADJUSTED:02X}u

/* J1939_Catalog_PGN_t.flags */
#define J1939_CAT_PGN_REQUIRES_TP         0x{PGN_FLAG_REQUIRES_TP:02X}u
#define J1939_CAT_PGN_VARIABLE_LENGTH     0x{PGN_FLAG_VARIABLE_LENGTH:02X}u
#define J1939_CAT_PGN_PROVISIONAL         0x{PGN_FLAG_PROVISIONAL:02X}u

/* One record per catalog index. Records of one PGN are contiguous and sorted
 * by start_bit. raw_min/raw_max are the validated operational raw bounds; for
 * a blob they bound each byte. start_bit is zero-based in the logical payload. */
typedef struct {{
    uint32_t raw_min;
    uint32_t raw_max;
    uint32_t spn;
    uint16_t signal_key;     /* Stable project item number 1..{n_sig}. */
    uint16_t start_bit;
    uint16_t bit_length;
    uint8_t  pgn_index;
    uint8_t  scale_index;
    uint8_t  control_kind;
    uint8_t  flags;
    uint8_t  special_codec;
}} J1939_Catalog_Signal_t;

/* One record per PGN, ascending by PGN. period_ms is the fixed period, or the
 * approved fallback/maximum period for ON_CHANGE and ENGINE_SPEED_DEPENDENT.
 * For ON_REQUEST it is the approved unsolicited broadcast period, or 0 when
 * the PGN is only sent in answer to a request.
 * min_interval_ms is the minimum gap between transmissions or responses. */
typedef struct {{
    uint32_t pgn;
    uint16_t first_signal;
    uint16_t payload_length; /* Logical bytes; maximum for variable length. */
    uint16_t period_ms;
    uint16_t min_interval_ms;
    uint8_t  signal_count;
    uint8_t  priority;
    uint8_t  source_address;
    uint8_t  transmission_mode;
    uint8_t  flags;
    uint8_t  change_threshold_pct;
}} J1939_Catalog_PGN_t;

typedef struct {{
    float resolution;
    float offset;
}} J1939_Catalog_Scale_t;

_Static_assert(sizeof(J1939_Catalog_Signal_t) == 24u, "catalog signal layout changed");
_Static_assert(sizeof(J1939_Catalog_PGN_t) == 20u, "catalog PGN layout changed");

typedef enum {{
    J1939_TX_MODE_PERIODIC               = J1939_CAT_TX_PERIODIC,
    J1939_TX_MODE_ON_CHANGE              = J1939_CAT_TX_ON_CHANGE,
    J1939_TX_MODE_ENGINE_SPEED_DEPENDENT = J1939_CAT_TX_ENGINE_SPEED_DEPENDENT,
    J1939_TX_MODE_ON_REQUEST             = J1939_CAT_TX_ON_REQUEST
}} J1939_Transmission_Mode_t;

/* Stable UI/FW protocol key (project item number 1..{n_sig}); not an SPN and
 * not a catalog index. */
typedef uint16_t J1939_Signal_Key_t;
#define J1939_SIGNAL_KEY_INVALID          ((J1939_Signal_Key_t)0u)

/* Expanded per-signal definition used by the scalar float codec
 * (j1939_encode_decode). Layout is unchanged from the original database.
 * Firmware does not store a second table: J1939_Catalog_Get_Definition()
 * builds one from the compact records, with name/unit NULL (descriptive text
 * lives in the UI catalog). start_byte/start_bit are 1-based. */
typedef struct {{
    uint32_t                    pgn;
    uint32_t                    spn;
    const char*                 name;
    const char*                 unit;
    uint32_t                    tx_period_ms;  /* Period, maximum/fallback period, or 0 if not fixed. */
    J1939_Transmission_Mode_t   tx_mode;
    uint8_t                     start_byte;    /* 1-based byte position; may exceed 8 for TP payloads. */
    uint8_t                     start_bit;     /* 1-based bit position inside start_byte. */
    uint16_t                    num_bits;      /* Up to 1600 bits in the source metadata. */
    float                       resolution;
    float                       offset;
    float                       min_physical;
    float                       max_physical;
}} J1939_Signal_Definition_t;

""")
    lines.append("/* Canonical PGN numbers. */\n")
    for p in model["pgns"]:
        lines.append(f"#define PGN_{_c_ident(p['acronym']):<14}{p['pgn']:>6}u  /* {_hex_pgn(p['pgn'])} */\n")
    lines.append("\n/* Catalog index of each PGN. */\n")
    for p in model["pgns"]:
        lines.append(f"#define J1939_CAT_PGN_IDX_{_c_ident(p['acronym']):<16}{p['index']}u"
                     f"  /* {_hex_pgn(p['pgn'])} */\n")
    lines.append("\n/* Stable signal keys (project item numbers). */\n")
    for s in model["signals_by_key"]:
        ident = f"J1939_KEY_{_c_ident(s['acronym'])}_SPN{s['spn']}"
        lines.append(f"#define {ident:<36}{s['key']}u\n")
    lines.append("""
extern const J1939_Catalog_Signal_t J1939_Catalog_Signals[J1939_CATALOG_SIGNAL_COUNT];
extern const J1939_Catalog_PGN_t    J1939_Catalog_PGNs[J1939_CATALOG_PGN_COUNT];
extern const J1939_Catalog_Scale_t  J1939_Catalog_Scales[J1939_CATALOG_SCALE_COUNT];
/* Indexed by signal_key; entry 0 and unknown keys hold 0xFF. */
extern const uint8_t J1939_Catalog_Key_To_Index[J1939_CATALOG_KEY_MAX + 1u];

/* Returns the catalog index or J1939_CATALOG_INDEX_INVALID. */
uint16_t J1939_Catalog_Index_From_Key(uint16_t signal_key);
/* Returns the PGN table index or J1939_CATALOG_INDEX_INVALID. */
uint16_t J1939_Catalog_Find_PGN_Index(uint32_t pgn);
/* Semantic identity (PGN, SPN) -> catalog index or J1939_CATALOG_INDEX_INVALID. */
uint16_t J1939_Catalog_Find_Signal_Index(uint32_t pgn, uint32_t spn);
/* Fills out with the expanded definition of one catalog record. Physical
 * min/max are the operational bounds (offset + raw_min/raw_max * resolution).
 * Returns false for an invalid index or NULL out. */
bool J1939_Catalog_Get_Definition(uint16_t catalog_index,
                                  J1939_Signal_Definition_t* out);

#ifdef __cplusplus
}
#endif
#endif /* J1939_SIGNAL_DEFINITIONS_H */
""")
    return "".join(lines)


def render_catalog_c(model):
    lines = [_banner(f"{CATALOG_BASENAME}.c",
                     "Single compact firmware signal/PGN database and key lookup.", model)]
    lines.append(f'\n#include "{CATALOG_BASENAME}.h"\n#include <stddef.h>\n\n')
    lines.append("const J1939_Catalog_Signal_t J1939_Catalog_Signals[J1939_CATALOG_SIGNAL_COUNT] = {\n")
    lines.append("    /* raw_min, raw_max, spn, key, start_bit, bits, pgn_idx, scale, kind, flags, codec */\n")
    for p in model["pgns"]:
        lines.append(f"\n    /* [{p['index']}] {_hex_pgn(p['pgn'])} {p['acronym']}: "
                     f"{p['mode']}, {p['payload_length']} B */\n")
        for s in p["members"]:
            lines.append(
                f"    {{ {s['raw_min']}u, {s['raw_max']}u, {s['spn']}u, {s['key']}u, "
                f"{s['start_bit']}u, {s['bit_length']}u, {s['pgn_index']}u, "
                f"{s['scale_index']}u, J1939_CAT_KIND_{s['kind'].upper()}, "
                f"0x{s['flags']:02X}u, J1939_CAT_CODEC_{s['codec']} }},"
                f"  /* [{s['index']}] */\n")
    lines.append("};\n\n")

    lines.append("const J1939_Catalog_PGN_t J1939_Catalog_PGNs[J1939_CATALOG_PGN_COUNT] = {\n")
    lines.append("    /* pgn, first, length, period, min_int, count, prio, sa, mode, flags, chg% */\n")
    for p in model["pgns"]:
        lines.append(
            f"    {{ {_hex_pgn(p['pgn'])}u, {p['first_signal']}u, {p['payload_length']}u, "
            f"{p['period_ms']}u, {p['min_interval_ms']}u, {p['signal_count']}u, "
            f"{p['priority']}u, 0x{p['source_address']:02X}u, J1939_CAT_TX_{p['mode']}, "
            f"0x{p['flags']:02X}u, {p['change_threshold_pct']}u }},"
            f"  /* [{p['index']}] {p['acronym']} */\n")
    lines.append("};\n\n")

    lines.append("const J1939_Catalog_Scale_t J1939_Catalog_Scales[J1939_CATALOG_SCALE_COUNT] = {\n")
    for i, (res, off) in enumerate(model["scales"]):
        lines.append(f"    {{ {_c_float(f32(res))}, {_c_float(f32(off))} }},"
                     f"  /* [{i}] {float(res)!r}, {float(off)!r} */\n")
    lines.append("};\n\n")

    lines.append("const uint8_t J1939_Catalog_Key_To_Index[J1939_CATALOG_KEY_MAX + 1u] = {\n")
    table = [INDEX_INVALID_U8] + [s["index"] for s in model["signals_by_key"]]
    for i in range(0, len(table), 12):
        chunk = ", ".join(f"{v:3d}u" if v != INDEX_INVALID_U8 else "0xFFu"
                          for v in table[i:i + 12])
        lines.append(f"    {chunk},  /* keys {i}..{min(i + 11, len(table) - 1)} */\n")
    lines.append("};\n")
    lines.append("""
uint16_t J1939_Catalog_Index_From_Key(uint16_t signal_key)
{
    if ((signal_key < J1939_CATALOG_KEY_MIN) || (signal_key > J1939_CATALOG_KEY_MAX)) {
        return J1939_CATALOG_INDEX_INVALID;
    }
    return (J1939_Catalog_Key_To_Index[signal_key] == 0xFFu)
        ? J1939_CATALOG_INDEX_INVALID
        : (uint16_t)J1939_Catalog_Key_To_Index[signal_key];
}

uint16_t J1939_Catalog_Find_PGN_Index(uint32_t pgn)
{
    uint16_t lo = 0u;
    uint16_t hi = J1939_CATALOG_PGN_COUNT;

    while (lo < hi) {
        uint16_t mid = (uint16_t)(lo + ((hi - lo) / 2u));
        if (J1939_Catalog_PGNs[mid].pgn == pgn) {
            return mid;
        }
        if (J1939_Catalog_PGNs[mid].pgn < pgn) {
            lo = (uint16_t)(mid + 1u);
        } else {
            hi = mid;
        }
    }
    return J1939_CATALOG_INDEX_INVALID;
}

uint16_t J1939_Catalog_Find_Signal_Index(uint32_t pgn, uint32_t spn)
{
    const uint16_t pgn_index = J1939_Catalog_Find_PGN_Index(pgn);
    if (pgn_index == J1939_CATALOG_INDEX_INVALID) {
        return J1939_CATALOG_INDEX_INVALID;
    }

    const J1939_Catalog_PGN_t* entry = &J1939_Catalog_PGNs[pgn_index];
    for (uint16_t i = entry->first_signal;
         i < (uint16_t)(entry->first_signal + entry->signal_count); i++) {
        if (J1939_Catalog_Signals[i].spn == spn) {
            return i;
        }
    }
    return J1939_CATALOG_INDEX_INVALID;
}

bool J1939_Catalog_Get_Definition(uint16_t catalog_index,
                                  J1939_Signal_Definition_t* out)
{
    if ((out == NULL) || (catalog_index >= J1939_CATALOG_SIGNAL_COUNT)) {
        return false;
    }

    const J1939_Catalog_Signal_t* sig = &J1939_Catalog_Signals[catalog_index];
    const J1939_Catalog_PGN_t* pgn = &J1939_Catalog_PGNs[sig->pgn_index];
    const J1939_Catalog_Scale_t* scale = &J1939_Catalog_Scales[sig->scale_index];

    out->pgn = pgn->pgn;
    out->spn = sig->spn;
    out->name = NULL;
    out->unit = NULL;
    out->tx_period_ms = pgn->period_ms;
    out->tx_mode = (J1939_Transmission_Mode_t)pgn->transmission_mode;
    out->start_byte = (uint8_t)((sig->start_bit / 8u) + 1u);
    out->start_bit = (uint8_t)((sig->start_bit % 8u) + 1u);
    out->num_bits = sig->bit_length;
    out->resolution = scale->resolution;
    out->offset = scale->offset;
    out->min_physical = scale->offset + ((float)sig->raw_min * scale->resolution);
    out->max_physical = scale->offset + ((float)sig->raw_max * scale->resolution);
    return true;
}
""")
    return "".join(lines)


# =============================================================================
# C customer profiles
# =============================================================================

def render_profiles_h(model):
    lines = [_banner("j1939_customer_profiles.h",
                     "Compact firmware customer-profile key masks.", model)]
    lines.append("""
#ifndef J1939_CUSTOMER_PROFILES_H
#define J1939_CUSTOMER_PROFILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "j1939_signal_definitions.h"

""")
    lines.append(f"#define J1939_PROFILE_COUNT               {len(model['profiles'])}u\n")
    lines.append(f"#define J1939_PROFILE_FLAG_LOCKED         0x{PROFILE_FLAG_LOCKED:02X}u\n")
    lines.append(f"#define J1939_PROFILE_FLAG_REQUIRES_TP    0x{PROFILE_FLAG_REQUIRES_TP:02X}u\n")
    lines.append("#define J1939_PROFILE_FLAG_UNRESOLVED_MODE 0x04u\n")
    lines.append("#define J1939_PROFILE_ID_CUSTOM            0xFFu\n\n")
    lines.append("/* Profile IDs. ALL is the generated full-catalog (Engineer) selection. */\n")
    for e in model["profiles"]:
        lines.append(f"#define J1939_PROFILE_ID_{e['name']:<18}{e['id']}u\n")
    lines.append("""
typedef struct {
    uint8_t     profile_id;
    uint8_t     signal_count;
    uint8_t     pgn_count;
    uint8_t     flags;
    uint8_t     baseline_load_500k_decipct; /* 239 = 23.9 %. */
    uint8_t     unresolved_mode_count;
    uint8_t     key_mask[J1939_CATALOG_ACTIVE_MASK_BYTES];
} J1939_Customer_Profile_t;

_Static_assert(sizeof(J1939_Customer_Profile_t) ==
               (6u + J1939_CATALOG_ACTIVE_MASK_BYTES),
               "customer profile layout changed");

extern const J1939_Customer_Profile_t J1939_Customer_Profiles[J1939_PROFILE_COUNT];

/* Returns NULL for an unknown profile ID. */
const J1939_Customer_Profile_t* J1939_Customer_Profile_Find(uint8_t profile_id);
bool J1939_Customer_Profile_Has_Key(const J1939_Customer_Profile_t* profile,
                                    uint16_t signal_key);

#ifdef __cplusplus
}
#endif
#endif /* J1939_CUSTOMER_PROFILES_H */
""")
    return "".join(lines)


def render_profiles_c(model):
    lines = [_banner("j1939_customer_profiles.c",
                     "Compact firmware customer-profile key masks.", model)]
    lines.append('\n#include "j1939_customer_profiles.h"\n#include <stddef.h>\n\n')
    lines.append("const J1939_Customer_Profile_t J1939_Customer_Profiles[J1939_PROFILE_COUNT] = {\n")
    for e in model["profiles"]:
        mask = ", ".join(f"0x{b:02X}u" for b in e["mask"])
        lines.append(f"    /* {e['name']}: {len(e['keys'])} signals, {len(e['pgns'])} PGNs"
                     f"{', TP keys ' + ' '.join(map(str, e['tp_keys'])) if e['tp_keys'] else ''} */\n")
        load_decipct = round(e["load"]["baseline_load_500k_pct"] * 10.0)
        lines.append(f"    {{ J1939_PROFILE_ID_{e['name']}, {len(e['keys'])}u, "
                     f"{len(e['pgns'])}u, 0x{e['flags']:02X}u, {load_decipct}u, 0u,\n"
                     f"      {{ {mask} }} }},\n")
    lines.append("};\n")
    lines.append("""
const J1939_Customer_Profile_t* J1939_Customer_Profile_Find(uint8_t profile_id)
{
    for (uint8_t i = 0u; i < J1939_PROFILE_COUNT; i++) {
        if (J1939_Customer_Profiles[i].profile_id == profile_id) {
            return &J1939_Customer_Profiles[i];
        }
    }
    return NULL;
}

bool J1939_Customer_Profile_Has_Key(const J1939_Customer_Profile_t* profile,
                                    uint16_t signal_key)
{
    if ((profile == NULL) || (signal_key < J1939_CATALOG_KEY_MIN) ||
        (signal_key > J1939_CATALOG_KEY_MAX)) {
        return false;
    }
    uint16_t bit = (uint16_t)(signal_key - 1u);
    return (profile->key_mask[bit / 8u] & (uint8_t)(1u << (bit % 8u))) != 0u;
}
""")
    return "".join(lines)


# =============================================================================
# Test vectors
# =============================================================================

def render_vectors_h(model):
    scalar_pgns = [p for p in model["pgns"]
                   if all(s["kind"] != "blob" for s in p["members"])]
    max_len = max(p["payload_length"] for p in scalar_pgns)
    lines = [_banner("j1939_catalog_test_vectors_generated.h",
                     "Host-test packing vectors (payload pre-filled with 0xFF).", model)]
    lines.append(f"""
#ifndef J1939_CATALOG_TEST_VECTORS_GENERATED_H
#define J1939_CATALOG_TEST_VECTORS_GENERATED_H

#include <stdint.h>

#define J1939_VECTOR_MAX_PAYLOAD  {max_len}u

/* One scalar signal packed alone into an all-0xFF logical PGN payload. */
typedef struct {{
    uint16_t signal_key;
    uint16_t payload_length;
    uint32_t raw;
    uint8_t  payload[J1939_VECTOR_MAX_PAYLOAD];
}} J1939_Signal_Vector_t;

/* Every signal of one PGN packed with the midpoint of its raw range. */
typedef struct {{
    uint32_t pgn;
    uint16_t payload_length;
    uint8_t  payload[J1939_VECTOR_MAX_PAYLOAD];
}} J1939_PGN_Vector_t;

static const J1939_Signal_Vector_t J1939_Signal_Vectors[] = {{
""")
    count = 0
    for s in model["signals_by_key"]:
        if s["kind"] == "blob":
            continue
        length = model["pgns"][s["pgn_index"]]["payload_length"]
        for raw in vector_values(s):
            buf = bytearray([0xFF] * length)
            pack_raw(buf, s["start_bit"], s["bit_length"], raw)
            data = ", ".join(f"0x{b:02X}" for b in buf)
            lines.append(f"    {{ {s['key']}u, {length}u, {raw}u, {{ {data} }} }},\n")
            count += 1
    lines.append("};\n\n")
    lines.append("static const J1939_PGN_Vector_t J1939_PGN_Vectors[] = {\n")
    for p in scalar_pgns:
        buf = bytearray([0xFF] * p["payload_length"])
        for s in p["members"]:
            pack_raw(buf, s["start_bit"], s["bit_length"], vector_values(s)[len(vector_values(s)) // 2])
        data = ", ".join(f"0x{b:02X}" for b in buf)
        lines.append(f"    {{ {_hex_pgn(p['pgn'])}u, {p['payload_length']}u, {{ {data} }} }},"
                     f"  /* {p['acronym']} */\n")
    lines.append("};\n\n")
    lines.append(f"#define J1939_SIGNAL_VECTOR_COUNT  {count}u\n")
    lines.append(f"#define J1939_PGN_VECTOR_COUNT     {len(scalar_pgns)}u\n")
    lines.append("\n#endif /* J1939_CATALOG_TEST_VECTORS_GENERATED_H */\n")
    return "".join(lines)


# =============================================================================
# UI catalog / manifest
# =============================================================================

def manifest(model, source_sha256):
    return {
        "schema_version": CATALOG_SCHEMA_VERSION,
        "catalog_version": model["policy"]["policy_version"],
        "catalog_crc32": f"0x{model['catalog_crc32']:08X}",
        "record_count": len(model["signals"]),
        "unique_spn_count": model["unique_spn_count"],
        "pgn_count": len(model["pgns"]),
        "profile_count": len(model["profiles"]),
        "generator_version": GENERATOR_VERSION,
        "policy_version": model["policy"]["policy_version"],
        "source_file": f"doc/{DEFAULT_MASTER.name}",
        "source_sheet": MASTER_SHEET,
        "source_sha256": source_sha256,
    }


def ui_catalog(model, source_sha256):
    pgn_by_index = model["pgns"]
    customers_of = {}
    for e in model["profiles"]:
        if e["generated"]:
            continue
        for k in e["keys"]:
            customers_of.setdefault(k, []).append(e["name"])

    signals = []
    for s in model["signals_by_key"]:
        p = pgn_by_index[s["pgn_index"]]
        res = s["resolution"]
        signals.append({
            "signal_key": s["key"],
            "catalog_index": s["index"],
            "pgn": p["pgn"],
            "pgn_hex": f"{p['pgn']:04X}",
            "pgn_name": p["acronym"],
            "spn": s["spn"],
            "name": s["name"],
            "category": s["category"],
            "unit": s["unit"],
            "start_byte": s["start_byte"],
            "start_bit_in_byte": s["start_bit_in_byte"],
            "start_bit": s["start_bit"],
            "bit_length": s["bit_length"],
            "raw_min": s["raw_min"],
            "raw_max": s["raw_max"],
            "raw_capacity_max": s["raw_capacity"],
            "resolution": _num(res),
            "resolution_numerator": res.numerator,
            "resolution_denominator": res.denominator,
            "offset": _num(s["offset"]),
            "physical_min": _num(s["phys_min"]),
            "physical_max": _num(s["phys_max"]),
            "physical_min_effective": _num(s["offset"] + s["raw_min"] * res),
            "physical_max_effective": _num(s["offset"] + s["raw_max"] * res),
            "raw_bounds_adjusted": bool(s["adjustments"]),
            "control_kind": s["kind"],
            "allowed_codes": None,
            "global_control": s["global_control"],
            "codec": s["codec"].lower(),
            "duplicate_spn": s["duplicate_spn"],
            "tx_mode": p["mode"].lower(),
            "period_ms": p["period_ms"] or None,
            "payload_length": p["payload_length"],
            "requires_tp": p["requires_tp"],
            "overrideable_schedule": False,
            "customers": customers_of.get(s["key"], []),
            "source_row": s["row"],
        })

    pgns = []
    for p in model["pgns"]:
        pgns.append({
            "pgn": p["pgn"],
            "pgn_hex": f"{p['pgn']:04X}",
            "pgn_name": p["acronym"],
            "catalog_index": p["index"],
            "source_rate": p["rate"],
            "tx_mode": p["mode"].lower(),
            "period_ms": p["period_ms"] or None,
            "min_interval_ms": p["min_interval_ms"] or None,
            "change_threshold_pct": p["change_threshold_pct"] or None,
            "engineer_period_ms": p["engineer_period_ms"] or None,
            "provisional": p["provisional"],
            "payload_length": p["payload_length"],
            "required_length": p["required_length"],
            "variable_length": p["variable_length"],
            "requires_tp": p["requires_tp"],
            "tp_dt_packets_max": p["tp_dt_packets"],
            "priority": p["priority"],
            "source_address": p["source_address"],
            "owner": p["owner"],
            "signal_keys": [s["key"] for s in p["members"]],
        })

    profiles = []
    for e in model["profiles"]:
        profiles.append({
            "profile_id": e["id"],
            "name": e["name"],
            "signal_count": len(e["keys"]),
            "pgn_count": len(e["pgns"]),
            "locked": e["locked"],
            "requires_tp": e["requires_tp"],
            "tp_signal_keys": e["tp_keys"],
            "signal_keys": e["keys"],
            "pgns_hex": [f"{v:04X}" for v in e["pgns"]],
            "fixed_cycle_fps": round(e["load"]["fixed_fps"], 3),
            "fixed_cycle_load_500k_pct": round(e["load"]["fixed_load_500k_pct"], 2),
            "baseline_load_500k_pct": round(e["load"]["baseline_load_500k_pct"], 2),
            "key_mask_hex": "".join(f"{b:02X}" for b in e["mask"]),
        })

    bl = model["policy"]["bus_load_policy"]
    return {
        "manifest": manifest(model, source_sha256),
        "bus_load_policy": {
            "bits_per_single_frame": bl["conservative_bits_per_single_frame"],
            "warning_percent": bl["warning_percent"],
            "reject_percent": bl["reject_percent"],
            "default_bitrate_bps": model["policy"]["transport"]["default_bitrate_bps"],
        },
        "pgns": pgns,
        "signals": signals,
        "customer_profiles": profiles,
    }


def _json(data):
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


# =============================================================================
# Report
# =============================================================================

def render_report(model, source_sha256):
    pgns = model["pgns"]
    signals = model["signals"]
    by_mode = {}
    for p in pgns:
        by_mode.setdefault(p["mode"], []).append(p)
    short = [p for p in pgns if not p["requires_tp"]]
    long_ = [p for p in pgns if p["requires_tp"]]
    total = bus_load(pgns)
    all_profile = model["profiles"][0]

    out = []
    w = out.append
    w("# J1939 Catalog Generation Report\n\n")
    w("Generated by `tools/generate_j1939_catalog.py` — do not edit.\n\n")
    w("| Item | Value |\n|---|---|\n")
    w(f"| Generator version | {GENERATOR_VERSION} |\n")
    w(f"| Catalog schema | {CATALOG_SCHEMA_VERSION} |\n")
    w(f"| Policy version | `{model['policy']['policy_version']}` |\n")
    w(f"| Catalog CRC32 | `0x{model['catalog_crc32']:08X}` |\n")
    w(f"| Source | `doc/{DEFAULT_MASTER.name}`, sheet `{MASTER_SHEET}`, rows "
      f"{FIRST_DATA_ROW}-{FIRST_DATA_ROW + len(signals) - 1} |\n")
    w(f"| Source SHA-256 | `{source_sha256}` |\n\n")

    w("## 1. Validation summary\n\n")
    w("| Check | Expected | Actual | Result |\n|---|---:|---:|---|\n")
    sem = model["policy"]["catalog_semantics"]
    checks = [
        ("Records", sem["record_count"], len(signals)),
        ("Unique SPN numbers", sem["unique_spn_count"], model["unique_spn_count"]),
        ("PGNs", sem["pgn_count"], len(pgns)),
        ("Duplicate (PGN, SPN)", 0, 0),
        ("Overlapping fields inside one PGN", 0, 0),
        ("Unresolved transmission rules", 0, 0),
        ("Single-frame PGNs", "", len(short)),
        ("Records in single-frame PGNs", "", sum(p["signal_count"] for p in short)),
        ("TP PGNs (> 8 bytes)", "", len(long_)),
        ("Records in TP PGNs", "", sum(p["signal_count"] for p in long_)),
    ]
    for name, exp, act in checks:
        res = "PASS" if exp == "" or exp == act else "FAIL"
        w(f"| {name} | {exp if exp != '' else '—'} | {act} | {res} |\n")
    w("\nGeneration aborts on any failed rule, so a written report always shows PASS "
      "for the enforced checks.\n\n")

    w("### Transmission modes\n\n| Mode | PGNs | Records |\n|---|---:|---:|\n")
    for mode in TX_MODES:
        ps = by_mode.get(mode, [])
        w(f"| {mode} | {len(ps)} | {sum(p['signal_count'] for p in ps)} |\n")
    w("\n")

    w("### Bus load (conservative 160 bit/frame)\n\n")
    w("| Traffic | Frame/s | 250 kbit/s | 500 kbit/s |\n|---|---:|---:|---:|\n")
    w(f"| Fixed-cycle PGNs only | {total['fixed_fps']:.1f} | "
      f"{total['fixed_load_250k_pct']:.1f}% | {total['fixed_load_500k_pct']:.1f}% |\n")
    fb = total["fixed_plus_fallback_fps"]
    w(f"| + ENGINE_SPEED_DEPENDENT fallback | {fb:.1f} | {fb * 160 / 2500:.1f}% | "
      f"{total['baseline_load_500k_pct']:.1f}% |\n\n")
    w("Event, Request, TP and control traffic are excluded and remain runtime "
      "measurement gates.\n\n")

    w("## 2. Duplicate SPN numbers (valid, keyed by PGN)\n\n")
    w("| SPN | PGNs | signal_keys |\n|---:|---|---|\n")
    for spn, plist in sorted(model["duplicate_spns"].items()):
        keys = [s["key"] for s in model["signals_by_key"] if s["spn"] == spn]
        w(f"| {spn} | {', '.join(_hex_pgn(p) for p in plist)} | "
          f"{', '.join(map(str, keys))} |\n")
    w("\n")

    w("## 3. Long PGNs (J1939 TP)\n\n")
    w("| PGN | Name | signal_keys | Logical length | TP.DT packets (max) | Mode |\n"
      "|---|---|---|---:|---:|---|\n")
    for p in long_:
        var = " (max, variable)" if p["variable_length"] else ""
        w(f"| {_hex_pgn(p['pgn'])} | {p['acronym']} | "
          f"{', '.join(str(s['key']) for s in p['members'])} | {p['payload_length']} B{var} | "
          f"{p['tp_dt_packets']} | {p['mode']} |\n")
    w("\n")

    w("## 4. Source-data adjustments\n\n")
    w("The workbook prints physical bounds rounded (for example 250.996 for "
      "64255 x 1/256 = 250.99609375). Raw bounds are therefore "
      "`ceil((min - tol - offset) / resolution)` and "
      "`floor((max + tol - offset) / resolution)`, where `tol` is half a unit of the "
      "last printed decimal, and raw_min is never below 0. Continuous 8/16/32-bit "
      "signals are then limited to the J1939 operational range (0xFA, 0xFAFF, "
      "0xFAFFFFFF). Only bounds that differ from the source beyond `tol` are listed "
      "below.\n\n")
    if model["repairs"]:
        w("### Text repairs\n\n")
        for r in model["repairs"]:
            w(f"- {r}\n")
        w("\n")
    w("### Raw-bound adjustments\n\n")
    if model["warnings"]:
        for msg in model["warnings"]:
            w(f"- {msg}\n")
    else:
        w("None.\n")
    w("\n")

    w("## 5. Classification rules\n\n")
    w("| control_kind | Rule | Global percent |\n|---|---|---|\n")
    w("| blob | bit length > 32 (ASCII, byte aligned) | Excluded |\n")
    w("| boolean | unit `State`/`SA`, 2 bits | Excluded |\n")
    w("| enum | unit `State`/`SA`, other lengths | Excluded |\n")
    w("| continuous | all other units | Included |\n\n")
    kinds = {}
    for s in signals:
        kinds[s["kind"]] = kinds.get(s["kind"], 0) + 1
    w("| control_kind | Records |\n|---|---:|\n")
    for k in CONTROL_KINDS:
        w(f"| {k} | {kinds.get(k, 0)} |\n")
    w("\nBoolean/enum raw ranges include the J1939 error/not-available codes from the "
      "source range. `allowed_codes` stays null until an approved code list exists.\n\n")

    w("## 6. PGN table\n\n")
    w("`Length` is the transmitted logical length (8 for single-frame PGNs, padded "
      "with 0xFF); `Used` is the last byte occupied by a catalog field.\n\n")
    w("| Idx | PGN | Name | Records | Used | Length | Mode | Period ms | Min interval ms | "
      "Prio | SA | Flags | Source rate |\n"
      "|---:|---|---|---:|---:|---:|---|---:|---:|---:|---|---|---|\n")
    for p in pgns:
        flags = []
        if p["requires_tp"]:
            flags.append("TP")
        if p["variable_length"]:
            flags.append("VAR")
        if p["provisional"]:
            flags.append("PROVISIONAL")
        rate = p["rate"].replace("|", "/")
        w(f"| {p['index']} | {_hex_pgn(p['pgn'])} | {p['acronym']} | {p['signal_count']} | "
          f"{p['required_length']} | {p['payload_length']} | {p['mode']} | {p['period_ms'] or '—'} | "
          f"{p['min_interval_ms'] or '—'} | {p['priority']} | 0x{p['source_address']:02X} | "
          f"{' '.join(flags) or '—'} | {rate} |\n")
    w("\n")

    w("## 7. Customer profiles\n\n")
    w("| ID | Profile | Records | PGNs | TP keys | Fixed frame/s | Fixed load 500k | "
      "Baseline load 500k |\n|---:|---|---:|---:|---|---:|---:|---:|\n")
    for e in model["profiles"]:
        w(f"| {e['id']} | {e['name']} | {len(e['keys'])} | {len(e['pgns'])} | "
          f"{', '.join(map(str, e['tp_keys'])) or '—'} | {e['load']['fixed_fps']:.1f} | "
          f"{e['load']['fixed_load_500k_pct']:.1f}% | {e['load']['baseline_load_500k_pct']:.1f}% |\n")
    w(f"\nProfile `ALL` (ID 0) is generated: all {len(all_profile['keys'])} records. "
      "Customer profiles are locked required sets (D-006).\n\n")

    w("## 8. Generated artifacts\n\n")
    for name in ARTIFACTS:
        w(f"- `{name}`\n")
    w("\nRegenerate with `python tools/generate_j1939_catalog.py`; verify with "
      "`--check` (fails when any artifact differs from a clean regeneration).\n")
    return "".join(out)


# =============================================================================
# Driver
# =============================================================================

# Paths relative to the repository root. The compact catalog is the firmware's
# signal database and therefore lives in MID/.
ARTIFACTS = (
    f"MID/{CATALOG_BASENAME}.h",
    f"MID/{CATALOG_BASENAME}.c",
    "MID/j1939_customer_profiles.h",
    "MID/j1939_customer_profiles.c",
    "generated/j1939_catalog_test_vectors_generated.h",
    "generated/j1939_ui_catalog.json",
    "generated/j1939_catalog_manifest.json",
    "generated/j1939_catalog_report.md",
)


def render_all(model, source_sha256):
    renders = (
        render_catalog_h(model),
        render_catalog_c(model),
        render_profiles_h(model),
        render_profiles_c(model),
        render_vectors_h(model),
        _json(ui_catalog(model, source_sha256)),
        _json(manifest(model, source_sha256)),
        render_report(model, source_sha256),
    )
    files = dict(zip(ARTIFACTS, renders))
    assert tuple(files) == ARTIFACTS
    return files


def generate(master=DEFAULT_MASTER, policy=DEFAULT_POLICY, network=DEFAULT_NETWORK,
             profiles=DEFAULT_PROFILES):
    model = build_catalog(load_master(master), load_json(policy),
                          load_json(network), load_json(profiles))
    return model, render_all(model, file_sha256(master))


def _normalize(text):
    return text.replace("\r\n", "\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--master", type=Path, default=DEFAULT_MASTER)
    ap.add_argument("--policy", type=Path, default=DEFAULT_POLICY)
    ap.add_argument("--network", type=Path, default=DEFAULT_NETWORK)
    ap.add_argument("--profiles", type=Path, default=DEFAULT_PROFILES)
    ap.add_argument("--out-root", type=Path, default=REPO_ROOT,
                    help="root that the MID/ and generated/ artifact paths are relative to")
    ap.add_argument("--check", action="store_true",
                    help="do not write; exit 1 if any artifact is missing or stale")
    args = ap.parse_args(argv)

    try:
        model, files = generate(args.master, args.policy, args.network, args.profiles)
    except CatalogError as ex:
        print(f"catalog generation FAILED ({len(ex.errors)} error(s)):", file=sys.stderr)
        for msg in ex.errors:
            print(f"  - {msg}", file=sys.stderr)
        return 2

    if args.check:
        stale = []
        for name, text in files.items():
            path = args.out_root / name
            if not path.exists() or \
               _normalize(path.read_text(encoding="utf-8")) != text:
                stale.append(name)
        if stale:
            print("generated artifacts are stale: " + ", ".join(stale), file=sys.stderr)
            return 1
        print(f"generated artifacts up to date (CRC32 0x{model['catalog_crc32']:08X})")
        return 0

    for name, text in files.items():
        path = args.out_root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print(f"wrote {len(files)} artifacts under {args.out_root} "
          f"(CRC32 0x{model['catalog_crc32']:08X}, {len(model['warnings'])} raw-bound "
          f"adjustment(s), {len(model['repairs'])} text repair(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
