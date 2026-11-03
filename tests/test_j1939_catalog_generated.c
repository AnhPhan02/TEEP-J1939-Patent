/*
 * Phase 1 host test: generated compact catalog, customer profiles and packing
 * vectors. Also cross-checks the generated catalog against the legacy
 * J1939_Signal_Database so the Phase 2 migration starts from proven identity.
 */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "j1939_signal_definitions.h"
#include "j1939_customer_profiles.h"
#include "j1939_catalog_test_vectors_generated.h"
#include "j1939_encode_decode.h"
#include "j1939_signal_definitions_legacy.h"

_Static_assert(J1939_CAT_TX_PERIODIC == J1939_TX_MODE_PERIODIC, "mode value");
_Static_assert(J1939_CAT_TX_ON_CHANGE == J1939_TX_MODE_ON_CHANGE, "mode value");
_Static_assert(J1939_CAT_TX_ENGINE_SPEED_DEPENDENT ==
               J1939_TX_MODE_ENGINE_SPEED_DEPENDENT, "mode value");
_Static_assert(J1939_CAT_TX_ON_REQUEST == J1939_TX_MODE_ON_REQUEST, "mode value");
_Static_assert(J1939_CATALOG_SIGNAL_COUNT == J1939_SIGNAL_COUNT_EXPECTED,
               "generated and legacy catalog sizes differ");

/* ---- CRC-32/ISO-HDLC over the canonical serialization -------------------- */

static uint32_t s_crc;

static void crc_bytes(const uint8_t* data, size_t len)
{
    for (size_t i = 0u; i < len; i++) {
        s_crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            s_crc = (s_crc >> 1) ^ (0xEDB88320u & (0u - (s_crc & 1u)));
        }
    }
}

static void crc_u8(uint32_t v)
{
    uint8_t b = (uint8_t)v;
    crc_bytes(&b, 1u);
}

static void crc_u16(uint32_t v)
{
    crc_u8(v);
    crc_u8(v >> 8);
}

static void crc_u32(uint32_t v)
{
    crc_u16(v);
    crc_u16(v >> 16);
}

static void crc_f32(float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    crc_u32(bits);
}

static uint32_t recompute_catalog_crc(void)
{
    s_crc = 0xFFFFFFFFu;
    crc_bytes((const uint8_t*)"J1939CAT", 8u);
    crc_u16(J1939_CATALOG_SCHEMA_VERSION);
    crc_u16(J1939_CATALOG_SIGNAL_COUNT);
    crc_u16(J1939_CATALOG_PGN_COUNT);
    crc_u16(J1939_CATALOG_SCALE_COUNT);
    crc_u16(J1939_PROFILE_COUNT);
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* s = &J1939_Catalog_Signals[i];
        crc_u16(s->signal_key);
        crc_u32(s->spn);
        crc_u8(s->pgn_index);
        crc_u16(s->start_bit);
        crc_u16(s->bit_length);
        crc_u32(s->raw_min);
        crc_u32(s->raw_max);
        crc_u8(s->scale_index);
        crc_u8(s->control_kind);
        crc_u8(s->flags);
        crc_u8(s->special_codec);
    }
    for (uint16_t i = 0u; i < J1939_CATALOG_SCALE_COUNT; i++) {
        crc_f32(J1939_Catalog_Scales[i].resolution);
        crc_f32(J1939_Catalog_Scales[i].offset);
    }
    for (uint16_t i = 0u; i < J1939_CATALOG_PGN_COUNT; i++) {
        const J1939_Catalog_PGN_t* p = &J1939_Catalog_PGNs[i];
        crc_u32(p->pgn);
        crc_u16(p->first_signal);
        crc_u8(p->signal_count);
        crc_u16(p->payload_length);
        crc_u16(p->period_ms);
        crc_u16(p->min_interval_ms);
        crc_u8(p->priority);
        crc_u8(p->source_address);
        crc_u8(p->transmission_mode);
        crc_u8(p->flags);
        crc_u8(p->change_threshold_pct);
    }
    for (uint16_t i = 0u; i < J1939_PROFILE_COUNT; i++) {
        const J1939_Customer_Profile_t* e = &J1939_Customer_Profiles[i];
        crc_u8(e->profile_id);
        crc_u8(e->signal_count);
        crc_u8(e->pgn_count);
        crc_u8(e->flags);
        crc_bytes(e->key_mask, J1939_CATALOG_ACTIVE_MASK_BYTES);
    }
    return s_crc ^ 0xFFFFFFFFu;
}

/* ---- independent reference packer --------------------------------------- */

static void pack_raw(uint8_t* buf, uint16_t start_bit, uint16_t bits, uint32_t raw)
{
    for (uint16_t i = 0u; i < bits; i++) {
        uint16_t pos = (uint16_t)(start_bit + i);
        uint8_t mask = (uint8_t)(1u << (pos % 8u));
        if ((raw >> i) & 1u) {
            buf[pos / 8u] |= mask;
        } else {
            buf[pos / 8u] &= (uint8_t)~mask;
        }
    }
}

static const J1939_Catalog_Signal_t* by_key(uint16_t key)
{
    uint16_t idx = J1939_Catalog_Index_From_Key(key);
    assert(idx != J1939_CATALOG_INDEX_INVALID);
    return &J1939_Catalog_Signals[idx];
}

static const J1939_Catalog_PGN_t* pgn_of(uint32_t pgn)
{
    uint16_t idx = J1939_Catalog_Find_PGN_Index(pgn);
    assert(idx != J1939_CATALOG_INDEX_INVALID);
    return &J1939_Catalog_PGNs[idx];
}

/* ---- tests --------------------------------------------------------------- */

static void test_counts_and_crc(void)
{
    assert(J1939_CATALOG_SIGNAL_COUNT == 179u);
    assert(J1939_CATALOG_PGN_COUNT == 60u);
    assert(J1939_CATALOG_UNIQUE_SPN_COUNT == 177u);
    assert(J1939_CATALOG_ACTIVE_MASK_BYTES == 23u);
    assert(recompute_catalog_crc() == J1939_CATALOG_CRC32);

    uint16_t unique = 0u;
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        uint16_t first = 1u;
        for (uint16_t j = 0u; j < i; j++) {
            if (J1939_Catalog_Signals[j].spn == J1939_Catalog_Signals[i].spn) {
                first = 0u;
            }
        }
        unique = (uint16_t)(unique + first);
    }
    assert(unique == 177u);
}

static void test_key_mapping(void)
{
    uint8_t seen[J1939_CATALOG_SIGNAL_COUNT] = { 0 };

    assert(J1939_Catalog_Index_From_Key(0u) == J1939_CATALOG_INDEX_INVALID);
    assert(J1939_Catalog_Index_From_Key(180u) == J1939_CATALOG_INDEX_INVALID);
    assert(J1939_Catalog_Index_From_Key(0xFFFFu) == J1939_CATALOG_INDEX_INVALID);

    for (uint16_t key = 1u; key <= J1939_CATALOG_KEY_MAX; key++) {
        uint16_t idx = J1939_Catalog_Index_From_Key(key);
        assert(idx < J1939_CATALOG_SIGNAL_COUNT);
        assert(J1939_Catalog_Signals[idx].signal_key == key);
        assert(seen[idx] == 0u);
        seen[idx] = 1u;
    }

    /* Duplicate SPN numbers stay distinct records. */
    assert(by_key(J1939_KEY_VW_SPN928)->spn == 928u);
    assert(by_key(J1939_KEY_EAC2_SPN928)->spn == 928u);
    assert(J1939_KEY_VW_SPN928 != J1939_KEY_EAC2_SPN928);
    assert(by_key(J1939_KEY_VDC2_SPN1807)->flags & J1939_CAT_SIG_DUPLICATE_SPN);
    assert(by_key(J1939_KEY_SWA_SPN1807)->flags & J1939_CAT_SIG_DUPLICATE_SPN);
    assert(J1939_KEY_EEC1_SPN190 == 103u);
    assert(J1939_KEY_CCVS1_SPN84 == 144u);
}

static void test_pgn_table(void)
{
    uint16_t next_signal = 0u;
    uint16_t modes[4] = { 0u, 0u, 0u, 0u };
    uint16_t tp_pgns = 0u;
    uint16_t tp_records = 0u;
    uint16_t demo_broadcasts = 0u;
    double fixed_fps = 0.0;

    for (uint16_t i = 0u; i < J1939_CATALOG_PGN_COUNT; i++) {
        const J1939_Catalog_PGN_t* p = &J1939_Catalog_PGNs[i];
        if (i > 0u) {
            assert(J1939_Catalog_PGNs[i - 1u].pgn < p->pgn);
        }
        assert(J1939_Catalog_Find_PGN_Index(p->pgn) == i);
        assert(p->first_signal == next_signal);
        assert(p->signal_count > 0u);
        next_signal = (uint16_t)(next_signal + p->signal_count);
        assert(p->priority <= 7u);
        assert(p->transmission_mode <= J1939_CAT_TX_ON_REQUEST);
        modes[p->transmission_mode]++;

        uint16_t prev_end = 0u;
        uint16_t used_bits = 0u;
        for (uint16_t k = p->first_signal; k < p->first_signal + p->signal_count; k++) {
            const J1939_Catalog_Signal_t* s = &J1939_Catalog_Signals[k];
            assert(s->pgn_index == i);
            assert(s->start_bit >= prev_end);              /* sorted, no overlap */
            prev_end = (uint16_t)(s->start_bit + s->bit_length);
            used_bits = prev_end;
            assert(s->scale_index < J1939_CATALOG_SCALE_COUNT);
            assert(s->raw_min <= s->raw_max);
            if (s->bit_length < 32u) {
                assert(s->raw_max <= ((1u << s->bit_length) - 1u));
            }
        }
        assert(used_bits <= (uint16_t)(p->payload_length * 8u));

        if (p->flags & J1939_CAT_PGN_REQUIRES_TP) {
            assert(p->payload_length > 8u);
            tp_pgns++;
            tp_records = (uint16_t)(tp_records + p->signal_count);
        } else {
            assert(p->payload_length == 8u);
        }

        switch (p->transmission_mode) {
        case J1939_CAT_TX_PERIODIC:
            assert(p->period_ms > 0u);
            fixed_fps += 1000.0 / p->period_ms;
            break;
        case J1939_CAT_TX_ON_REQUEST:
            /* 0 = request-only; else the customer-approved demo broadcast. */
            if (p->period_ms != 0u) {
                assert((p->period_ms == 1000u) ||
                       ((p->pgn == PGN_EBS21) && (p->period_ms == 100u)) ||
                       ((p->pgn == PGN_VI) && (p->period_ms == 5000u)));
                demo_broadcasts++;
            }
            break;
        default:                                      /* event/dependent */
            assert(p->period_ms > 0u);                /* approved fallback */
            break;
        }
    }
    assert(next_signal == J1939_CATALOG_SIGNAL_COUNT);
    assert(modes[J1939_CAT_TX_PERIODIC] == 45u);
    assert(modes[J1939_CAT_TX_ON_REQUEST] == 10u);
    assert(demo_broadcasts == 10u);                   /* all On-Request PGNs */
    assert(modes[J1939_CAT_TX_ON_CHANGE] == 4u);
    assert(modes[J1939_CAT_TX_ENGINE_SPEED_DEPENDENT] == 1u);
    assert(fabs(fixed_fps - 697.0) < 0.5);
    assert(tp_pgns == 3u);
    assert(tp_records == 4u);

    assert(J1939_Catalog_Find_PGN_Index(0x12345u) == J1939_CATALOG_INDEX_INVALID);
    assert(J1939_Catalog_Find_PGN_Index(0u) == J1939_CATALOG_INDEX_INVALID);

    assert(pgn_of(PGN_RC)->payload_length == 18u);
    assert(pgn_of(PGN_EC)->payload_length == 21u);
    assert(pgn_of(PGN_VI)->payload_length == 200u);
    assert(pgn_of(PGN_VI)->flags & J1939_CAT_PGN_VARIABLE_LENGTH);
    assert(by_key(J1939_KEY_VI_SPN237)->special_codec == J1939_CAT_CODEC_ASCII_BLOB);
    assert(by_key(J1939_KEY_RC_SPN901)->bit_length == 4u);  /* narrow SPN, TP PGN */

    /* Phase 0 policy decisions. */
    assert(pgn_of(PGN_EEC1)->transmission_mode == J1939_CAT_TX_ENGINE_SPEED_DEPENDENT);
    assert(pgn_of(PGN_EEC1)->period_ms == 5000u);     /* customer demo */
    assert(pgn_of(PGN_EEC1)->flags & J1939_CAT_PGN_PROVISIONAL);
    assert(pgn_of(PGN_EBS21)->transmission_mode == J1939_CAT_TX_ON_REQUEST);
    assert(pgn_of(PGN_EBS21)->min_interval_ms == 100u);
    assert(pgn_of(PGN_EBS21)->period_ms == 100u);     /* demo broadcast */
    assert(pgn_of(PGN_VI)->period_ms == 5000u);       /* demo broadcast */
    assert(J1939_VIN_TX_BYTES == 21u && J1939_VIN_TERMINATOR == '*');
    assert(pgn_of(PGN_DM1)->transmission_mode == J1939_CAT_TX_ON_REQUEST);
    assert(pgn_of(PGN_OELC)->period_ms == 1000u);
    assert(pgn_of(PGN_OELC)->min_interval_ms == 100u);
    assert(pgn_of(PGN_RC)->period_ms == 5000u);
    assert(pgn_of(PGN_RC)->change_threshold_pct == 10u);
}

static uint16_t profile_pgn_count(const J1939_Customer_Profile_t* e)
{
    uint8_t used[J1939_CATALOG_PGN_COUNT] = { 0 };
    uint16_t count = 0u;
    for (uint16_t key = 1u; key <= J1939_CATALOG_KEY_MAX; key++) {
        if (J1939_Customer_Profile_Has_Key(e, key)) {
            uint8_t p = by_key(key)->pgn_index;
            count = (uint16_t)(count + (used[p] == 0u));
            used[p] = 1u;
        }
    }
    return count;
}

static void test_customer_profiles(void)
{
    static const struct {
        uint8_t id;
        uint8_t count;
        uint8_t pgns;
        uint8_t requires_tp;
        uint8_t baseline_load_decipct;
    } expected[] = {
        { J1939_PROFILE_ID_ALL, 179u, 60u, 1u, 229u },
        { J1939_PROFILE_ID_C,    57u, 25u, 0u, 180u },
        { J1939_PROFILE_ID_K,    36u, 20u, 0u, 130u },
        { J1939_PROFILE_ID_E,    61u, 29u, 0u, 108u },
        { J1939_PROFILE_ID_F,    16u, 13u, 0u,  50u },
        { J1939_PROFILE_ID_M,    23u, 13u, 1u,  18u },
        { J1939_PROFILE_ID_H,    78u, 33u, 0u, 90u },
        { J1939_PROFILE_ID_A,    29u, 14u, 0u,  31u },
        { J1939_PROFILE_ID_D,    51u, 20u, 1u,  31u },
        { J1939_PROFILE_ID_I,    29u, 20u, 0u, 88u },
        { J1939_PROFILE_ID_POC,  27u, 17u, 0u,  57u },
    };
    assert(J1939_PROFILE_COUNT == sizeof(expected) / sizeof(expected[0]));

    for (size_t i = 0u; i < sizeof(expected) / sizeof(expected[0]); i++) {
        const J1939_Customer_Profile_t* e = J1939_Customer_Profile_Find(expected[i].id);
        assert(e != NULL);
        assert(e->signal_count == expected[i].count);
        assert(e->pgn_count == expected[i].pgns);
        assert(e->baseline_load_500k_decipct ==
               expected[i].baseline_load_decipct);
        assert(e->unresolved_mode_count == 0u);
        assert(((e->flags & J1939_PROFILE_FLAG_REQUIRES_TP) != 0u) ==
               (expected[i].requires_tp != 0u));

        uint16_t bits = 0u;
        for (uint16_t key = 1u; key <= J1939_CATALOG_KEY_MAX; key++) {
            bits = (uint16_t)(bits + J1939_Customer_Profile_Has_Key(e, key));
        }
        assert(bits == e->signal_count);
        assert(profile_pgn_count(e) == e->pgn_count);
        /* Bits for keys 180..184 in the last mask byte stay clear. */
        assert((e->key_mask[J1939_CATALOG_ACTIVE_MASK_BYTES - 1u] & 0xF8u) == 0u);
        assert(!J1939_Customer_Profile_Has_Key(e, 0u));
        assert(!J1939_Customer_Profile_Has_Key(e, 180u));
    }
    assert(J1939_Customer_Profile_Find(200u) == NULL);
    assert(!J1939_Customer_Profile_Has_Key(NULL, 1u));

    /* TP membership from the specification. */
    const J1939_Customer_Profile_t* m = J1939_Customer_Profile_Find(J1939_PROFILE_ID_M);
    const J1939_Customer_Profile_t* d = J1939_Customer_Profile_Find(J1939_PROFILE_ID_D);
    assert(J1939_Customer_Profile_Has_Key(m, J1939_KEY_VI_SPN237));
    assert(J1939_Customer_Profile_Has_Key(d, J1939_KEY_RC_SPN901));
    assert(J1939_Customer_Profile_Has_Key(d, J1939_KEY_RC_SPN556));
    assert(J1939_Customer_Profile_Has_Key(d, J1939_KEY_EC_SPN544));
}

static void test_vectors(void)
{
    for (uint16_t i = 0u; i < J1939_SIGNAL_VECTOR_COUNT; i++) {
        const J1939_Signal_Vector_t* v = &J1939_Signal_Vectors[i];
        const J1939_Catalog_Signal_t* s = by_key(v->signal_key);
        const J1939_Catalog_PGN_t* p = &J1939_Catalog_PGNs[s->pgn_index];
        uint8_t buf[J1939_VECTOR_MAX_PAYLOAD];

        assert(v->payload_length == p->payload_length);
        assert(v->raw >= s->raw_min && v->raw <= s->raw_max);
        memset(buf, 0xFF, sizeof(buf));
        pack_raw(buf, s->start_bit, s->bit_length, v->raw);
        assert(memcmp(buf, v->payload, v->payload_length) == 0);

        /* Bit placement agrees with the legacy codec. */
        const J1939_Signal_Definition_t* legacy =
            J1939_Find_Signal_By_PGN_SPN(p->pgn, s->spn);
        const J1939_Catalog_Scale_t* sc = &J1939_Catalog_Scales[s->scale_index];
        float decoded = 0.0f;
        assert(legacy != NULL);
        assert(J1939_Decode_Signal_Sized(legacy, v->payload, v->payload_length,
                                         &decoded));
        double expected = (double)sc->offset + (double)v->raw * (double)sc->resolution;
        double tol = fmax((double)sc->resolution, fabs(expected) * 1e-6);
        assert(fabs((double)decoded - expected) <= tol);
    }

    for (uint16_t i = 0u; i < J1939_PGN_VECTOR_COUNT; i++) {
        const J1939_PGN_Vector_t* v = &J1939_PGN_Vectors[i];
        const J1939_Catalog_PGN_t* p = pgn_of(v->pgn);
        uint8_t buf[J1939_VECTOR_MAX_PAYLOAD];

        assert(v->payload_length == p->payload_length);
        memset(buf, 0xFF, sizeof(buf));
        for (uint16_t k = p->first_signal; k < p->first_signal + p->signal_count; k++) {
            const J1939_Catalog_Signal_t* s = &J1939_Catalog_Signals[k];
            pack_raw(buf, s->start_bit, s->bit_length,
                     s->raw_min + (s->raw_max - s->raw_min) / 2u);
        }
        assert(memcmp(buf, v->payload, v->payload_length) == 0);
    }
}

static void test_legacy_consistency(void)
{
    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* s = &J1939_Catalog_Signals[i];
        const J1939_Catalog_PGN_t* p = &J1939_Catalog_PGNs[s->pgn_index];
        const J1939_Signal_Definition_t* legacy =
            J1939_Find_Signal_By_PGN_SPN(p->pgn, s->spn);

        assert(legacy != NULL);
        assert((uint16_t)((legacy->start_byte - 1u) * 8u + (legacy->start_bit - 1u)) ==
               s->start_bit);
        assert(legacy->num_bits == s->bit_length);
        assert(legacy->tx_mode == (J1939_Transmission_Mode_t)p->transmission_mode);
        assert(fabsf(legacy->resolution - J1939_Catalog_Scales[s->scale_index].resolution) <
               1e-9f);
        assert(fabsf(legacy->offset - J1939_Catalog_Scales[s->scale_index].offset) < 1e-4f);

        /* Every stable key already in firmware must keep its identity. */
        const J1939_Signal_Definition_t* keyed = J1939_Find_Signal_By_Key(s->signal_key);
        if (keyed != NULL) {
            assert(keyed == legacy);
        }
    }

    for (uint16_t i = 0u; i < J1939_UI_Profile_40_Key_Count; i++) {
        J1939_Signal_Key_t key = J1939_UI_Profile_40_Keys[i];
        const J1939_Catalog_Signal_t* s = by_key(key);
        const J1939_Signal_Definition_t* legacy = J1939_Find_Signal_By_Key(key);
        assert(legacy != NULL);
        assert(legacy->spn == s->spn);
        assert(legacy->pgn == J1939_Catalog_PGNs[s->pgn_index].pgn);
    }
}

/* The firmware database rebuilds J1939_Signal_Definition_t for every record
 * without a second table; it must match the legacy reference and drive the
 * float codec to the same bits as the raw packer. */
static void test_expanded_definition(void)
{
    J1939_Signal_Definition_t def;

    assert(!J1939_Catalog_Get_Definition(J1939_CATALOG_SIGNAL_COUNT, &def));
    assert(!J1939_Catalog_Get_Definition(0u, NULL));

    for (uint16_t i = 0u; i < J1939_CATALOG_SIGNAL_COUNT; i++) {
        const J1939_Catalog_Signal_t* s = &J1939_Catalog_Signals[i];
        const J1939_Catalog_PGN_t* p = &J1939_Catalog_PGNs[s->pgn_index];
        const J1939_Signal_Definition_t* legacy =
            J1939_Find_Signal_By_PGN_SPN(p->pgn, s->spn);

        memset(&def, 0xA5, sizeof(def));
        assert(J1939_Catalog_Get_Definition(i, &def));
        assert(legacy != NULL);
        assert(def.pgn == legacy->pgn);
        assert(def.spn == legacy->spn);
        assert(def.name == NULL && def.unit == NULL);
        assert(def.start_byte == legacy->start_byte);
        assert(def.start_bit == legacy->start_bit);
        assert(def.num_bits == legacy->num_bits);
        assert(def.tx_mode == legacy->tx_mode);
        assert(def.tx_period_ms == p->period_ms);
        assert(def.resolution == J1939_Catalog_Scales[s->scale_index].resolution);
        assert(def.offset == J1939_Catalog_Scales[s->scale_index].offset);
        assert(fabsf(def.resolution - legacy->resolution) < 1e-9f);
        assert(fabsf(def.offset - legacy->offset) < 1e-4f);

        /* Physical bounds are the catalog's operational raw bounds; their
         * relation to the rounded workbook range is checked by the generator
         * tests (display tolerance, J1939 operational clamp). */
        const double lo = (double)def.offset + (double)s->raw_min * (double)def.resolution;
        const double hi = (double)def.offset + (double)s->raw_max * (double)def.resolution;
        assert(fabs((double)def.min_physical - lo) <= fabs(lo) * 1e-6 + 1e-6);
        assert(fabs((double)def.max_physical - hi) <= fabs(hi) * 1e-6 + 1e-6);
        assert(def.min_physical <= def.max_physical);
    }

    /* Float codec on the expanded definition decodes the raw vectors. */
    for (uint16_t i = 0u; i < J1939_SIGNAL_VECTOR_COUNT; i++) {
        const J1939_Signal_Vector_t* v = &J1939_Signal_Vectors[i];
        float decoded = 0.0f;
        assert(J1939_Catalog_Get_Definition(J1939_Catalog_Index_From_Key(v->signal_key),
                                            &def));
        assert(J1939_Decode_Signal_Sized(&def, v->payload, v->payload_length, &decoded));
        const double expected = (double)def.offset + (double)v->raw * (double)def.resolution;
        assert(fabs((double)decoded - expected) <=
               fmax((double)def.resolution, fabs(expected) * 1e-6));
    }
}

int main(void)
{
    test_counts_and_crc();
    test_key_mapping();
    test_pgn_table();
    test_customer_profiles();
    test_vectors();
    test_legacy_consistency();
    test_expanded_definition();
    printf("j1939 generated catalog tests passed (CRC32 0x%08X, %u vectors)\n",
           (unsigned)J1939_CATALOG_CRC32,
           (unsigned)(J1939_SIGNAL_VECTOR_COUNT + J1939_PGN_VECTOR_COUNT));
    return 0;
}
