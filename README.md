# TEEP-J1939-Patent

J1939 waveform generator firmware for STM32F103 (NUCLEO-F103RB, STM32duino core).

## Architecture

```
APP  app_main      Entry points App_Init / App_Run (called from the .ino)
     app_cli       Serial command parser: CONFIG START STOP CLEAR BAUD STATUS RESET
     app_generator Run state, modes, telemetry, LED, CAN diagnostics printing
     app_config.h  Application tunables
      │
MID  j1939_tx_scheduler   Groups SPNs into PGNs, broadcasts on period
     j1939_pattern_generator  Waveforms (sine, ramp, step, ...)
     j1939_encode_decode  SPN <-> payload bits
     j1939_signal_definitions  SPN/PGN database (J1939-71)
     j1939_pgn_timing     Per-PGN period / priority / source address
     j1939_link           Send/receive PGN via hal_can  (only MID -> HAL user)
     j1939_frame          29-bit ID <-> PGN/prio/SA/DA (J1939-21), pure logic
      │
HAL  hal_can.h      → hal_can_stm32f1.cpp     bxCAN, bit timing, MSP/GPIO
     hal_console.h  → hal_console_arduino.cpp Serial
     hal_time.h     → hal_time_arduino.cpp    millis/delay
     hal_led.h      → hal_led_arduino.cpp     status LED
     hal_board_cfg.h   Pin / peripheral mapping (private to HAL)
     hal_conf_extra.h  Enables HAL_CAN_MODULE_ENABLED for STM32duino
```

Dependency rules:

- Dependencies point downward only: APP → MID → HAL.
- HAL does not include any MID or APP header, and it has no J1939 knowledge.
- MID does not include `Arduino.h` or `stm32*.h`. It reaches hardware only through `hal_*.h`.
- APP may use `hal_console`, `hal_time` and `hal_led` directly. CAN access goes through MID (`j1939_link`).
- Only the `HAL/*_arduino.cpp` and `HAL/*_stm32f1.cpp` files depend on the framework or the chip. To port to bare-metal or CubeIDE, replace those files and keep the `hal_*.h` headers.

## Build

> **Pending:** the repo has no build configuration yet. Arduino IDE compiles only the sketch folder and its `src/`, so the sibling folders `APP/`, `MID/` and `HAL/` are not picked up.

Recommended: PlatformIO with a `platformio.ini` at the repo root:

```ini
[env:nucleo_f103rb]
platform  = ststm32
board     = nucleo_f103rb
framework = arduino
build_src_filter = +<../APP/> +<../MID/> +<../HAL/>
build_flags = -I APP -I MID -I HAL
monitor_speed = 115200
```

## Pending / missing modules

| Area | Status | Notes |
|---|---|---|
| Build system (`platformio.ini` or CMake + toolchain file) | Missing | Required before the project builds |
| Startup file `startup_stm32f103xb.s` | Provided by STM32duino core | Needed only when moving to bare-metal or CubeIDE |
| Linker script `STM32F103RBTx_FLASH.ld` | Provided by STM32duino core | Same as above |
| `system_stm32f1xx.c` / `SystemClock_Config()` | Provided by core variant | CAN bit timing assumes PCLK1 = 36 MHz (preset table in `hal_can_stm32f1.cpp`) |
| `stm32f1xx_hal_conf.h` (full) | Only `hal_conf_extra.h` | Needed for a CubeIDE / bare-metal build |
| Register-level (LL / direct register) CAN driver | Uses ST HAL | Optional; would replace `hal_can_stm32f1.cpp` |
| Fault handling: HardFault handler, IWDG watchdog, CAN bus-off / error IRQ, RX interrupt | Missing | |
| J1939 address claim (J1939-81), transport protocol BAM/TP (J1939-21), DM1 (J1939-73) | Missing | New MID modules |
| Host unit tests (MID + APP with stub HAL) | Missing | MID and APP already build on a PC with a stub HAL |

## References

**STM32F103 hardware and registers**
- RM0008: STM32F101/102/103/105/107 Reference Manual. Covers bxCAN (bit timing, ESR/BTR registers), RCC and AFIO remap.
- DS5319: STM32F103x8/xB datasheet. Covers pinout, CAN remap to PB8/PB9 and clock tree limits.
- PM0056: Cortex-M3 programming manual. Covers NVIC, SysTick and fault handling.
- UM1850: Description of STM32F1 HAL and low-layer (LL) drivers.
- UM1724: STM32 Nucleo-64 user manual. Covers Arduino header mapping (D14/D15) and the LED.

**Startup, linker and system files**
- https://github.com/STMicroelectronics/cmsis_device_f1, under `Source/Templates/`:
  - `gcc/startup_stm32f103xb.s`
  - `system_stm32f1xx.c`
  - `gcc/linker/`
- https://github.com/STMicroelectronics/STM32CubeF1, under `Drivers/STM32F1xx_HAL_Driver/Inc/`:
  - `stm32f1xx_hal_conf_template.h`
  - CAN examples in `Projects/`
- https://github.com/stm32duino/Arduino_Core_STM32, under `variants/STM32F1xx/F103R(8-B)T/`:
  - `ldscript.ld`
  - the clock configuration used today
- STM32CubeMX can generate clock config, startup and linker files for a chosen board.

**CAN / J1939**
- SAE J1939-21: Data link layer (29-bit ID, PDU1/PDU2, transport protocol).
- SAE J1939-71: Vehicle application layer (PGN/SPN definitions).
- SAE J1939-73: Diagnostics (DM1).
- SAE J1939-81: Network management (address claim).
- CAN bit timing calculator: http://www.bittiming.can-wiki.info/

**Build**
- PlatformIO `build_src_filter`: https://docs.platformio.org/en/latest/projectconf/sections/env/options/build/build_src_filter.html
# STM32-J1939-Patent
