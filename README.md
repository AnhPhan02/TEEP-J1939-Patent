# TEEP-J1939-Patent

Bare-metal **SAE J1939 network simulator** firmware for the **STM32F103C8T6** (Cortex-M3, 72 MHz, 64 KB Flash / 20 KB RAM).
It uses register-level drivers and is built with `arm-none-eabi-gcc`.

The firmware broadcasts the full **179-record signal catalog** (177 unique SPNs, 60 PGNs) with the correct
period, priority and source address for each PGN. It is configured from a PC UI over the same CAN bus, stores the
configuration in internal Flash, and resumes transmitting by itself after a power cycle.

| | |
|---|---|
| Signal catalog | 179 records / 177 SPNs / 60 PGNs, catalog CRC `0xF155BBF0` |
| Customer profiles | 11 generated profiles (ALL, C, K, E, F, M, H, A, D, I, POC) + Custom |
| Control channel | Proprietary A (PGN `0xEF00`), UI `0xF9` ↔ firmware `0x80`, protocol v2.5 |
| Long messages | J1939-21 TP BAM (18 B, 21 B VIN, 200 B) |
| Persistence | Two 4 KiB Flash slots used alternately, power-fail safe |
| Default build | `make` → `build/profile-179/j1939_generator_179scalar.elf` |

Further documents:

| Document | Audience |
|---|---|
| [doc/FW_Feature_Overview.md](doc/FW_Feature_Overview.md) | Feature overview for project managers and customers (Vietnamese) |
| [doc/FW_Architecture_Design_EN.md](doc/FW_Architecture_Design_EN.md) / [VN](doc/FW_Architecture_Design.md) | Module-level design for firmware developers |
| [doc/FW_Optimization_Ideas.md](doc/FW_Optimization_Ideas.md) | Data and TX-timing optimization proposals (Vietnamese) |
| [generated/j1939_catalog_report.md](generated/j1939_catalog_report.md) | Catalog validation report, per-profile bus load |
| [Phase0_Requirement_Decision_Log.md](Phase0_Requirement_Decision_Log.md) | Requirement decisions for the 179-record upgrade |

---

## Contents

1. [Features](#1-features)
2. [Architecture](#2-architecture)
3. [The 179-signal catalog](#3-the-179-signal-catalog)
4. [Control protocol (UI ↔ firmware)](#4-control-protocol-ui--firmware)
5. [Boot lifecycle](#5-boot-lifecycle)
6. [Hardware and clock plan](#6-hardware-and-clock-plan)
7. [Build, flash and test](#7-build-flash-and-test)
8. [Resource usage: Flash, RAM, CPU](#8-resource-usage-flash-ram-cpu)
9. [Scope and next steps](#9-scope-and-next-steps)
10. [References](#10-references)

---

## 1. Features

| Feature | Description |
|---|---|
| **179-signal simulation** | All 179 catalog records can be active at once. Signals that share a PGN are packed into one payload and sent with that PGN's catalog timing. |
| **Customer profiles** | One command selects a customer's locked signal set. Custom mode allows any subset. |
| **Transactional configuration** | Changes are staged and applied atomically on COMMIT: either everything is applied or nothing is. Live transmission continues while staging. |
| **Reliable UI handshake** | Every command gets a correlated response (opcode, sequence, status). Retried commands are de-duplicated for 2 s. |
| **Flash persistence** | `SAVE_CONFIG` stores the profile, signal set, values, bitrate and autostart flag in A/B slots with CRC protection. |
| **Autostart** | After reset the saved configuration is restored and transmission restarts without the UI. An operator START/STOP always wins. |
| **Long messages** | PGNs longer than 8 bytes (component ID, ECU ID, VIN) are sent with TP BAM. |
| **Self-protection** | The IWDG watchdog (≈ 2 s) resets the MCU if it hangs. After 3 consecutive watchdog resets, the MCU enters safe mode and does not autostart. Bus-off recovery is automatic. |
| **Diagnostics** | 10 `GET_STATUS` pages: protocol, runtime, config CRC, limits, profile, storage, boot, CAN link. |

---

## 2. Architecture

```
OP   op_main                 main(), boot order, watchdog, safe-mode detection, CAN supervision
     op_fault                Fault policy: leave CAN bus, LED on, debugger break or reset
     op_config.h             Watchdog, CAN self test, retry/supervision periods
      │
APP  app_control_can         UI control protocol on PGN 0xEF00: decode, execute, respond, dedup
     app_generator           Run state START/STOP, telemetry, LED, CAN diagnostics
     app_boot                Restore saved configuration at boot, autostart state machine
     app_persistence         SAVE / LOAD / ERASE glue between value store and profile storage
     app_cli                 Optional development CLI over USART1 (build flag APP_CLI_ENABLE)
     app_config.h            Application tunables (default CAN bitrate, console baud, LED)
      │
MID  j1939_signal_definitions  GENERATED 179-record catalog (signals, PGNs, scales)
     j1939_customer_profiles   GENERATED customer profile key masks
     j1939_value_store         Double-banked raw values: staging/active, transactions, config CRC
     j1939_tx_scheduler        Per-PGN timing, payload build, sorted 32-frame TX queue
     j1939_tp                  J1939-21 TP BAM transmitter (TP.CM / TP.DT)
     j1939_encode_decode       Physical ↔ raw, bit insertion into the payload
     j1939_profile_storage     A/B Flash slot format: header, CRCs, commit marker
     j1939_rx_router           RX dispatch by PGN to the registered handler
     j1939_link                Send/receive PGN via hal_can (the only MID → HAL CAN user)
     j1939_frame               29-bit ID ↔ priority/PGN/SA/DA (PDU1/PDU2), pure logic
     j1939_crc32               CRC-32 used by the catalog, config and storage images
     j1939_pattern_generator   Legacy waveforms, compiled out unless ENABLE_WAVEFORM=1
      │
HAL  hal_system  RCC clock tree, Flash latency, CSS/NMI, reset cause, IWDG
     hal_time    SysTick 1 ms
     hal_can     bxCAN: bit timing, filter, TX mailboxes, RX FIFO0, self test
     hal_flash   Page erase / half-word program of the profile slots
     hal_console USART1 115200, IRQ ring buffers
     hal_led     PC13 status LED
     hal_board_cfg.h, stm32f103_reg.h   Clock plan, pin map, RM0008 register map (private to HAL)

startup_stm32f103xb.s    Vector table + Reset_Handler
STM32F103C8TX_FLASH.ld   FLASH 56 KiB app + PROFILE_A/B 4 KiB each, RAM 20 KiB
tools/generate_j1939_catalog.py   Builds the catalog from the master workbook + config/*.json
```

Dependency rules:

- Dependencies point downward only: OP → APP → MID → HAL.
- OP is the only layer that calls every other layer, and it owns the boot order.
- HAL includes no MID, APP or OP header and has no J1939 knowledge.
- MID reaches hardware only through `hal_*.h`. CAN access from APP goes through `j1939_link`.
- RX is upward through a callback: `j1939_rx_router` hands control frames to `app_control_can`.
- Only `HAL/*_stm32f1.c`, `HAL/stm32f103_reg.h` and `HAL/hal_board_cfg.h` are chip or board specific.

---

## 3. The 179-signal catalog

The catalog is **generated, not hand-written**. A single source produces both the firmware tables and the UI catalog,
so the two sides cannot drift apart.

```
doc/J1939_179_SPN_Master_Specification_Table.xlsx ─┐
config/j1939_phase0_policy.json                    ├─► tools/generate_j1939_catalog.py ─┬─► MID/j1939_signal_definitions.{c,h}
config/j1939_pgn_network.json                      │                                   ├─► MID/j1939_customer_profiles.{c,h}
config/j1939_customer_profiles.json ───────────────┘                                   ├─► generated/j1939_ui_catalog.json
                                                                                         ├─► generated/j1939_catalog_manifest.json
                                                                                         └─► generated/j1939_catalog_report.md
```

Each signal has three identifiers:

| Identifier | Range | Used by |
|---|---|---|
| `signal_key` | 1..179 | The UI protocol. Stable project item number, never reused. |
| Catalog index | 0..178 | Firmware arrays. Signals are sorted by PGN, then start bit. |
| (PGN, SPN) | J1939 numbers | Semantic identity. 177 unique SPNs; two SPNs appear in two PGNs each. |

The **catalog CRC** (`0xF155BBF0`) identifies the catalog version. The UI sends it in `BEGIN_CONFIG`, and a saved
configuration records it. A mismatch is rejected, so an image saved with one catalog is never loaded into another.

Customer profiles (from the generator report; bus load at 500 kbit/s):

| ID | Profile | Signals | PGNs | Bus load |
|---:|---|---:|---:|---:|
| 0 | ALL | 179 | 60 | 22.9 % |
| 1 | C | 57 | 25 | 18.0 % |
| 2 | K | 36 | 20 | 13.0 % |
| 3 | E | 61 | 29 | 10.8 % |
| 4 | F | 16 | 13 | 5.0 % |
| 5 | M | 23 | 13 | 1.8 % |
| 6 | H | 78 | 33 | 9.0 % |
| 7 | A | 29 | 14 | 3.1 % |
| 8 | D | 51 | 20 | 3.1 % |
| 9 | I | 29 | 20 | 8.8 % |
| 10 | POC | 27 | 17 | 5.7 % |

Regenerate after editing the workbook or the config files:

```sh
make catalog         # regenerate MID/ and generated/
make catalog-check   # fail if the committed files differ from a clean regeneration
```

---

## 4. Control protocol (UI ↔ firmware)

All control traffic uses **PGN 0xEF00 (Proprietary A)**, priority 6. It is a PDU1 PGN, so the PS byte carries the destination address.

| Direction | CAN ID | Source → destination |
|---|---|---|
| UI → firmware (request) | `0x18EF80F9` | `0xF9` (off-board service tool) → `0x80` |
| Firmware → UI (response) | `0x18EFF980` | `0x80` → `0xF9` |

The firmware drops any frame whose priority, PGN, SA or DA does not match exactly.

**Frame layout (always 8 bytes, little-endian):**

```
Request : [opcode] [seq] [payload × 6]
Response: [opcode | 0x80] [seq] [status] [detail] [value_u32]
```

| Opcode | Command | Purpose |
|---|---|---|
| `0x01` | TC1001 | Legacy test command |
| `0x02` / `0x03` | STOP / START | Stop / start transmission |
| `0x04` | GET_STATUS | Read status page 0–9 |
| `0x05` | CLEAR_CONFIG | Remove all active signals |
| `0x10` | SET_CONST | Legacy single-signal constant |
| `0x11` | BEGIN_CONFIG | Open a transaction (catalog CRC, expected counts, profile ID) |
| `0x12` | SET_SIGNAL_RAW | Stage one raw value by `signal_key` |
| `0x13` / `0x14` | COMMIT_CONFIG / ABORT_CONFIG | Apply or discard the staged configuration |
| `0x15` | WRITE_BLOB | Stage a long payload in 3-byte chunks |
| `0x16` | SET_GLOBAL_PERCENT | Set every active scalar to a percentage of its range |
| `0x17` | SET_STRING | Stage an ASCII string (VIN, 1–20 characters) |
| `0x18` / `0x19` / `0x1A` | SAVE / LOAD / ERASE_SAVED_CONFIG | Flash persistence |
| `0x1B` | SET_AUTOSTART | Enable/disable start-on-boot for the saved configuration |

Typical UI sequence:

```
STOP → BEGIN_CONFIG(catalog CRC, profile) → SET_SIGNAL_RAW × N [→ WRITE_BLOB / SET_STRING]
     → COMMIT_CONFIG → START → SAVE_CONFIG → SET_AUTOSTART(1)
```

Example: set signal key 103 to raw `0x2EE0` (sequence 7):

```
UI → FW  0x18EF80F9  12 07 67 00 E0 2E 00 00
FW → UI  0x18EFF980  92 07 00 01 E0 2E 00 00     (status 0 = OK)
```

The full request/response layouts and the `GET_STATUS` pages are documented in [APP/app_control_can.h](APP/app_control_can.h).

---

## 5. Boot lifecycle

```
RESET → SYSTEM_INIT → BOARD_INIT → APP_INIT → COMM_INIT → RUN
                                                  │          ▲
                                                  └─► COMM_FAULT ┘  (retry CAN join every 1 s)
```

1. `Reset_Handler`: `SystemInit()`, copy `.data`, zero `.bss`, call `main()`.
2. **SYSTEM_INIT**: clock tree (72 MHz from HSE, 64 MHz HSI fallback) and SysTick.
3. **BOARD_INIT**: LED, USART1, boot report (reset cause, clock source, bus clocks).
4. **APP_INIT** runs **before** the CAN join, because the saved configuration decides the bitrate:
   - count consecutive watchdog resets in `.noinit` RAM. After 3, enter **safe mode**, which suppresses autostart;
   - initialise the RX router, control protocol and generator;
   - `App_Boot_Restore()` loads the newest valid A/B slot. If the autostart flag is set, autostart becomes *PENDING*;
   - an empty or invalid slot leaves the device STOPPED and waiting for the UI. The Flash contents are kept for diagnosis.
5. **COMM_INIT**: CAN self test in loopback + silent mode, then join the bus at the restored bitrate.
   The default is `APP_DEFAULT_CAN_BAUD_KBPS` when nothing is saved.
6. Start the watchdog.
7. **RUN**: feed the watchdog, poll CAN RX, process control commands, run the scheduler, supervise the CAN error state.
   A *PENDING* autostart begins here, unless START/STOP was received first.

---

## 6. Hardware and clock plan

| Item | Value | RM0008 reference |
|---|---|---|
| SYSCLK | HSE 8 MHz × PLL 9 = **72 MHz** | §7.2, RCC_CFGR PLLMUL=0111 |
| AHB / APB1 / APB2 | 72 / **36** (CAN, max 36) / 72 MHz | RCC_CFGR HPRE=0, PPRE1=100, PPRE2=0 |
| Fallback (no HSE) | HSI/2 × 16 = 64 MHz, APB1 32 MHz | RCC_CFGR PLLSRC=0 |
| FLASH | 2 wait states + prefetch | §3.3.3 FLASH_ACR |
| CAN pins | PB8 = RX (input pull-up), PB9 = TX (AF push-pull) | §9.3.x, AFIO_MAPR CAN_REMAP=10 |
| Console | USART1 PA9 TX / PA10 RX, 115200 8N1 (USB-UART adapter) | §27.6, BRR = PCLK2 / baud |
| LED | PC13, active low. 1 Hz idle, 5 Hz transmitting, 10 Hz CAN not started | |
| Debug | SWD only (JTAG pins released) | AFIO_MAPR SWJ_CFG=010 |
| Watchdog | IWDG ≈ 2 s (LSI 40 kHz, /64), frozen while halted | §19.4, DBGMCU_CR |

CAN bit timing (target sample point 87.5 %, SJW 1 TQ):

| PCLK1 | Bitrate | BRP | TQ = 1 + TS1 + TS2 | Sample point |
|---|---|---|---|---|
| 36 MHz | 250 kbit/s | 9 | 16 = 1 + 13 + 2 | 87.5 % |
| 36 MHz | 500 kbit/s | 4 | 18 = 1 + 15 + 2 | 88.9 % |
| 32 MHz | 250 kbit/s | 8 | 16 = 1 + 13 + 2 | 87.5 % |
| 32 MHz | 500 kbit/s | 4 | 16 = 1 + 13 + 2 | 87.5 % |

---

## 7. Build, flash and test

Requires `arm-none-eabi-gcc` (tested with 15.3.1) and GNU `make` in `PATH`.
On Windows, install `make` from MSYS2 or with `winget install ezwinports.make`.
The catalog and host tests also need Python 3 and a host C compiler.

```sh
make              # 179-signal firmware → build/profile-179/j1939_generator_179scalar.{elf,hex,bin}
make flash        # program over SWD with ST-LINK_CLI, verify, reset
make size         # section sizes
make host-test    # catalog check + all host unit tests
make clean
```

Build options (all default to the 179-signal release configuration):

| Option | Default | Effect |
|---|---|---|
| `ENABLE_179_SCALAR` | `1` | 179 signals / 60 PGNs. Set to `0` for the legacy 40-signal / 11-PGN build in `build/`. |
| `APP_CLI_ENABLE` | `0` | `1` enables the USART1 development CLI (`CONFIG START STOP CLEAR BAUD STATUS RESET CANTEST TXLOG`). |
| `APP_BOOT_AUTOSTART_DEFAULTS` | `0` | `1` = demonstration build: with empty storage, load demo signals (EEC1, EEC2, CCVS1) and start. |
| `ENABLE_WAVEFORM` | `0` | `1` enables the legacy waveform engine (sine, ramp, …). The release build is constant-value only. |
| `ENABLE_100_SPNS` | `0` | Legacy 100-slot capacity experiment in `build/profile-100/`. Kept only for comparison. |

Host tests (`tests/`) cover the catalog generator, the generated catalog, value-store transactions, the scheduler,
the control protocol, customer profiles, persistence (with a fake Flash), autostart, CAN identity and dedup.

> Note: `J1939_PROFILE_IS_EXPERIMENTAL` is still `1` for the 179 build in `MID/j1939_build_profile.h`.
> The flag is only checked by `tests/test_j1939_build_profile.c`. Set it to `0`, and update the test,
> when the 179 build is formally released.

---

## 8. Resource usage: Flash, RAM, CPU

All figures were measured on the default build: `make`, 179 signals / 60 PGNs, GCC 15.3.1,
output `build/profile-179/j1939_generator_179scalar.elf`.

### At a glance

| Resource | Used | Available | Usage | Verdict |
|---|---:|---:|---:|---|
| Flash (application) | 31,188 B | 56 KiB | 54 % | OK. About 26 KiB free |
| RAM (static + reservation) | 8,320 B | 20 KiB | 41 % | OK. About 13 KiB left for the stack |
| CPU | a few % (estimated) | 72 MHz Cortex-M3 | — | OK. Not the limiting factor |
| CAN bus, profile ALL | ≈ 700 frames/s | 500 / 250 kbit/s | ≈ 23 % / 46 % | OK |

None of the resources is close to its limit. The real timing limit is **how frames line up in time**, not CPU or memory (see [8.5](#85-cpu)).

### 8.1 Reading the build report

Every `make` run ends with this report, printed because the linker runs with `-Wl,--print-memory-usage`:

```
: the message above does not take linker garbage collection into account
Memory region         Used Size  Region Size  %age Used
             RAM:        8320 B        20 KB     40.62%
           FLASH:       31188 B        56 KB     54.39%
       PROFILE_A:           0 B         4 KB      0.00%
       PROFILE_B:           0 B         4 KB      0.00%
```

Each region is a `MEMORY` entry in `STM32F103C8TX_FLASH.ld`:

| Region | Address range | What it holds | How to read the number |
|---|---|---|---|
| `FLASH` | `0x08000000`–`0x0800DFFF` (56 KiB) | Code and constant data | Must stay below 56 KB. The figure includes 12 B of alignment padding, so it is slightly larger than the sum of the sections (31,176 B). |
| `PROFILE_A` | `0x0800E000`–`0x0800EFFF` (4 KiB) | Saved configuration, slot A | **0 B is correct.** The firmware writes these slots at run time (`SAVE_CONFIG`). The linker places nothing in them. |
| `PROFILE_B` | `0x0800F000`–`0x0800FFFF` (4 KiB) | Saved configuration, slot B | Same as slot A. A non-zero value would mean the image overflowed into the slots. |
| `RAM` | `0x20000000`–`0x20004FFF` (20 KiB) | Variables and stack | Includes a fixed 1,536 B heap/stack reservation, so the real static footprint is 6,784 B. |

**About the first line.** It is the last line of the newlib warnings `_close is not implemented and will always fail`
(also `_read`, `_write`, `_lseek`, `_fstat`, `_isatty`, `_kill`, `_sbrk`). They come from `nano.specs` / `nosys.specs`.
The linker prints them **before** `--gc-sections` removes unused code. In the final image all of these stubs are removed:
the map file lists them under *Discarded input sections*, and `arm-none-eabi-nm` does not find them in the ELF.
The warning is therefore **harmless**, because the firmware calls no stdio, file or heap function.

### 8.2 Memory map

```
FLASH 64 KiB
0x08000000 ┌──────────────────────────────┐
           │ .isr_vector      236 B       │
           │ .text         21,964 B       │  code
           │ .rodata        8,888 B       │  catalog, scales, profiles
           │ .data image       88 B       │  initial values, copied to RAM at reset
0x080079D4 ├──────────────────────────────┤
           │ free          ≈ 26 KiB       │
0x0800E000 ├──────────────────────────────┤
           │ PROFILE_A          4 KiB     │  saved configuration (A/B alternate)
0x0800F000 ├──────────────────────────────┤
           │ PROFILE_B          4 KiB     │
0x08010000 └──────────────────────────────┘

RAM 20 KiB
0x20000000 ┌──────────────────────────────┐
           │ .data               80 B     │
           │ .bss             6,696 B     │  all static state (tables below)
           │ .noinit              8 B     │  reset counter, survives watchdog reset
0x20001A80 ├──────────────────────────────┤
           │ free for stack ≈ 13.4 KiB    │  (1,536 B of it is the linker reservation)
           │              ▲               │
           │              │ stack grows down
0x20005000 └──────────────────────────────┘  _estack
```

### 8.3 Flash

| Content | Size | Share of 56 KiB |
|---|---:|---:|
| Application code (`.text`) | 21,964 B | 38 % |
| Signal catalog, 179 × 24 B | 4,296 B | 7 % |
| PGN catalog, 60 × 20 B | 1,200 B | 2 % |
| Other constants (scales, profiles, strings) | 3,392 B | 6 % |
| Vector table, `.data` image, padding | 336 B | < 1 % |
| **Total** | **31,188 B** | **54 %** |

Largest code items: the control-command handler (2,076 B), `J1939_Sched_Tick` (1,092 B) and the software
floating-point library (≈ 1,900 B; the Cortex-M3 has no FPU).

**Saved configuration.** The configuration is stored in two 4 KiB slots that take turns.
A Flash page survives about 10,000 erase cycles, so the device supports about 20,000 saves. That is more than enough for bench use.

### 8.4 RAM

| Object | Size | Purpose |
|---|---:|---|
| `s_vs`, value store | 1,932 B | Two copies (active and staging) of all 179 values, plus the 200 B long-message buffer |
| `s_image`, storage buffer | 1,536 B | Builds the configuration image during Save/Load |
| `s_tx_buf`, console | 1,024 B | UART log buffer |
| `s_tx_queue`, TX queue | 640 B | 32 frames waiting for a CAN mailbox |
| `s_pgns`, scheduler | 480 B | Next send time of each of the 60 PGNs |
| `s_cache`, command dedup | 384 B | Last 16 UI commands, so a resent command is not executed twice |
| Other modules | 788 B | TP session, CAN and protocol state, counters |
| **Static total** | **6,784 B** | `.data` + `.bss` + `.noinit` |

**Stack.** The stack starts at the top of RAM and grows down toward `.bss`, so it can use all of the ≈ 13.4 KiB in between.
The linker only checks that at least 1 KiB is free. The peak stack depth has not been measured yet.
Measure it with stack painting or `-fstack-usage` before release.

### 8.5 CPU

The firmware runs a **single main loop** paced by a 1 ms SysTick. There are no CAN interrupts:
the loop reads the RX FIFO and refills the 3 TX mailboxes on each pass.

| Activity | Cost | When |
|---|---|---|
| Scheduler check, 60 PGN entries | Small, integer compares | Every 1 ms |
| Building one frame payload (bit insertion) | A few µs | ≈ 700 times per second |
| Physical value → raw conversion (software float) | Tens of µs | Only when the UI changes a value |
| Flash erase + program, one 4 KiB slot | Tens of ms; the CPU stalls | Only on `SAVE_CONFIG`; well within the 2 s watchdog |

Total load is estimated at a few percent of the CPU. This is derived from the code paths and has not been measured yet.
To measure it, read the DWT `CYCCNT` counter around `App_Gen_Process`.

**The real limit is burst alignment.** The current phase formula can make up to 9 frames due in the same millisecond.
At 250 kbit/s that delays the last one by about 5 ms, and a receiver with a small FIFO may lose frames.
Computing phases offline caps this at 2 frames/ms (see [doc/FW_Optimization_Ideas.md](doc/FW_Optimization_Ideas.md) §2).

### 8.6 Memory management rules

1. **No heap.** `malloc` is never called. The 512 B heap reservation in the linker script is unused.
2. **Everything is static.** Each table and buffer is sized at compile time from the generated catalog.
   Changing the catalog changes the sizes automatically.
3. **Sizes are locked.** `_Static_assert` fails the build if a catalog record, a scheduler entry or the storage image changes size unexpectedly.
4. **Constants stay in Flash.** The catalog and customer profiles are `const` and are read in place, never copied to RAM.
5. **The configuration slots are protected.** `ASSERT`s in the linker script stop code from growing into `PROFILE_A`.

### 8.7 Room to grow

If a future feature needs more memory, these savings are available without changing behaviour:

| Change | Saves |
|---|---:|
| Write the configuration to Flash directly instead of through `s_image` | 1.5 KiB RAM |
| Use a 256 B console buffer in release builds | 768 B RAM |
| Build the payload when loading the mailbox, and shrink the TX queue to 16 × 8 B | 512 B RAM |
| Store the 10 distinct raw-range pairs in a shared table (catalog record 24 → 16 B) | 1.4 KiB Flash |

---

## 9. Scope and next steps

| Area | Status |
|---|---|
| 179-record catalog, generated firmware and UI tables | ✅ Done |
| Customer profiles (11) + Custom | ✅ Done |
| Transactional configuration, dedup, protocol v2.5 | ✅ Done |
| Flash persistence A/B, autostart, safe mode | ✅ Done |
| TP BAM transmit (J1939-21) | ✅ Done |
| Host tests | ✅ Present; CI not set up |
| Global TX phase assignment (burst cap 9 → 2 frames/ms) | Proposed, high priority |
| Request PGN 59904: ON_REQUEST PGNs are currently broadcast periodically | Missing |
| ON_CHANGE transmission: currently sent on the fallback period | Missing |
| EEC1 engine-speed-dependent period: currently 5,000 ms (demo value) | Missing |
| TP RTS/CTS (connection mode), TP receive | Missing; BAM transmit only |
| Address Claim (J1939-81): fixed addresses are used | Missing |
| DM1 (J1939-73) DTC list: lamp status only | Missing |
| TX mailbox interrupt refill, hardware-timer scheduling | Proposed |

---

## 10. References

**STM32F103**
- RM0008 Rev 21: STM32F101/102/103/105/107 Reference Manual (local copy in `.Agent/.doc/`). Sections used:
  §3.3.3 FLASH_ACR, §3.3.4 Flash programming, §7 RCC, §9 GPIO/AFIO, §19 IWDG, §24 bxCAN, §27 USART, §31.16 DBGMCU.
- DS5319: STM32F103x8/xB datasheet (pinout, electrical limits, LSI range, Flash endurance).
- PM0056: Cortex-M3 programming manual (SysTick, NVIC, SCB, fault status registers).

**CAN / J1939**
- SAE J1939-11 / J1939-14: physical layer, 250 / 500 kbit/s bit timing.
- SAE J1939-21: data link layer (29-bit ID, PDU1/PDU2, Proprietary A, transport protocol).
- SAE J1939-71: vehicle application layer (PGN/SPN definitions). Local copy: `doc/j1939-71.pdf`.
- SAE J1939-73: diagnostics (DM1).
- SAE J1939-81: network management (address claim).
- CiA 601-3: CAN bit timing recommendations.
- CAN bit timing calculator: http://www.bittiming.can-wiki.info/
