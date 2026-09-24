# TEEP-J1939-Patent

Bare-metal J1939 waveform generator firmware for **STM32F103C8T6** (Cortex-M3, 64 KB Flash / 20 KB RAM), register-level drivers, built with `arm-none-eabi-gcc`.

## Architecture

```
OP   op_main       main() + system lifecycle (clock -> board -> CAN -> app -> run)
     op_fault      Fault policy: leave CAN bus, LED on, debugger break or reset
     op_config.h   Watchdog, CAN self test, retry/supervision periods
      │
APP  app_cli       Serial command parser: CONFIG START STOP CLEAR BAUD STATUS RESET
     app_generator Run state, modes, telemetry, LED, CAN diagnostics printing
     app_config.h  Application tunables (console baud, default CAN bitrate, ...)
      │
MID  j1939_tx_scheduler       Groups SPNs into PGNs, broadcasts on period
     j1939_pattern_generator  Waveforms (sine, ramp, step, ...)
     j1939_encode_decode      SPN <-> payload bits
     j1939_signal_definitions SPN/PGN database (J1939-71)
     j1939_pgn_timing         Per-PGN period / priority / source address
     j1939_link               Send/receive PGN via hal_can (only MID -> HAL user)
     j1939_frame              29-bit ID <-> PGN/prio/SA/DA (J1939-21), pure logic
      │
HAL  hal_system.h  → hal_system_stm32f1.c   RCC clock tree, FLASH latency, CSS/NMI, reset cause, IWDG
     hal_time.h    → hal_time_stm32f1.c     SysTick 1 ms
     hal_can.h     → hal_can_stm32f1.c      bxCAN: bit timing, filter, TX/RX, self test
     hal_console.h → hal_console_stm32f1.c  USART1 115200, IRQ ring buffers
     hal_led.h     → hal_led_stm32f1.c      PC13 status LED
     hal_board_cfg.h   Clock plan + pin mapping (private to HAL)
     stm32f103_reg.h   Register map from RM0008 (private to HAL, offsets checked at compile time)

startup_stm32f103xb.s   GNU as vector table + Reset_Handler
STM32F103C8TX_FLASH.ld  Linker script (FLASH 64K @0x08000000, RAM 20K @0x20000000)
```

Dependency rules:

- Dependencies point downward only: OP → APP → MID → HAL.
- OP is the only layer allowed to call every other layer. It owns the boot order.
- HAL does not include any MID, APP or OP header, and it has no J1939 knowledge.
- MID reaches hardware only through `hal_*.h`.
- APP may use `hal_console`, `hal_time` and `hal_led` directly. CAN access goes through MID (`j1939_link`).
- Only `HAL/*_stm32f1.c`, `HAL/stm32f103_reg.h` and `HAL/hal_board_cfg.h` are chip or board specific.

## Hardware / clock plan

| Item | Value | RM0008 reference |
|---|---|---|
| SYSCLK | HSE 8 MHz × PLL 9 = **72 MHz** | §7.2, RCC_CFGR PLLMUL=0111 |
| AHB / APB1 / APB2 | 72 / **36** (CAN, max 36) / 72 MHz | RCC_CFGR HPRE=0, PPRE1=100, PPRE2=0 |
| Fallback (no HSE) | HSI/2 × 16 = 64 MHz, APB1 32 MHz | RCC_CFGR PLLSRC=0 |
| FLASH | 2 wait states + prefetch | §3.3.3 FLASH_ACR |
| CAN pins | PB8 = RX (input pull-up), PB9 = TX (AF push-pull) | §9.3.x, AFIO_MAPR CAN_REMAP=10 |
| Console | USART1 PA9 TX / PA10 RX, 115200 8N1 (use a USB-UART adapter) | §27.6, BRR = PCLK2 / baud |
| LED | PC13, active low | |
| Debug | SWD only (JTAG pins released) | AFIO_MAPR SWJ_CFG=010 |
| Watchdog | IWDG ≈ 2 s (LSI 40 kHz, /64), frozen while halted | §19.4, DBGMCU_CR |

CAN bit timing (target sample point 87.5 %, SJW 1 TQ):

| PCLK1 | Bitrate | BRP | TQ = 1 + TS1 + TS2 | Sample point |
|---|---|---|---|---|
| 36 MHz | 250 kbit/s | 9 | 16 = 1 + 13 + 2 | 87.5 % |
| 36 MHz | 500 kbit/s | 4 | 18 = 1 + 15 + 2 | 88.9 % |
| 32 MHz | 250 kbit/s | 8 | 16 = 1 + 13 + 2 | 87.5 % |
| 32 MHz | 500 kbit/s | 4 | 16 = 1 + 13 + 2 | 87.5 % |

## Boot lifecycle

```
RESET → SYSTEM_INIT → BOARD_INIT → COMM_INIT → APP_INIT → RUN
                                       │                   ▲
                                       └─► COMM_FAULT ─────┘  (retry CAN join every 1 s)
```

1. `Reset_Handler`: `SystemInit()` (RCC to reset state, VTOR), copy `.data`, zero `.bss`, `main()`.
2. **SYSTEM_INIT**: clock tree and SysTick.
3. **BOARD_INIT**: LED, USART1, then a boot report (reset cause, clock source, bus clocks).
4. **COMM_INIT**:
   - CAN self test in loopback + silent mode (does not touch the bus).
   - Join the bus: CAN clock gate, pin remap, bit timing, filter, normal mode.
5. **APP_INIT**: load default signals and auto-start transmission.
6. Start the watchdog.
7. **RUN**: feed the watchdog, poll the CLI, run the generator, supervise the CAN error state. Bus-off recovery is automatic (ABOM=1).

## Build and flash

Requires `arm-none-eabi-gcc` and GNU `make` in `PATH`. On Windows, get `make` from MSYS2 or `winget install ezwinports.make`.

```sh
make            # build/j1939_generator.elf / .hex / .bin
make flash      # ST-LINK_CLI over SWD, verify, reset
make clean
```

Current footprint: Flash ≈ 39 KB / 64 KB, RAM ≈ 12 KB / 20 KB.

## Pending / next steps

| Area | Status |
|---|---|
| CAN RX interrupt + ring buffer, TX software queue (TMEIE) instead of 2 ms polling wait | Proposed |
| Hardware-timer based TX scheduling (jitter) | Proposed |
| Fault record kept across reset (`.noinit` RAM section) | Proposed |
| J1939: fix `PGN_EEC2` (61442 → 61443), implement `t_start`/`t_dur` windows | Open |
| J1939: Request PGN, Address Claim (J1939-81), TP BAM/CMDT (J1939-21), DM1 (J1939-73) | Missing |
| Host unit tests + CI | Missing |

## References

**STM32F103**
- RM0008 Rev 21: STM32F101/102/103/105/107 Reference Manual (local copy in `.Agent/.doc/`). Sections used:
  - §3.3.3 FLASH_ACR
  - §7 RCC
  - §9 GPIO/AFIO
  - §19 IWDG
  - §24 bxCAN
  - §27 USART
  - §31.16 DBGMCU
- DS5319: STM32F103x8/xB datasheet (pinout, electrical limits, LSI range).
- PM0056: Cortex-M3 programming manual (SysTick, NVIC, SCB, fault status registers).

**CAN / J1939**
- SAE J1939-11 / J1939-14: physical layer, 250 / 500 kbit/s bit timing.
- SAE J1939-21: data link layer (29-bit ID, PDU1/PDU2, transport protocol).
- SAE J1939-71: vehicle application layer (PGN/SPN definitions).
- SAE J1939-73: diagnostics (DM1).
- SAE J1939-81: network management (address claim).
- CiA 601-3: CAN bit timing recommendations.
- CAN bit timing calculator: http://www.bittiming.can-wiki.info/
# STM32-J1939-Patent
