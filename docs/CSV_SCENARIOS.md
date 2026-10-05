# CSV-defined signal scenarios

The CSV is build input on your computer. A Python converter validates it against the actual C signal database, then emits a constant firmware table. The STM32 reads that compiled table; it does not open CSV files or require a computer to stream values during execution.

## Build and choose a scenario

Requirements: Python 3.8+, a native C compiler (`cc` by default), GNU make, and `arm-none-eabi-gcc` with its newlib libraries for the firmware build. Host tests additionally use AddressSanitizer and UndefinedBehaviorSanitizer. They cover both production C behavior and the Make dependency graph; compiler stubs in the dependency regression test do not substitute for an ARM cross-build. No Python packages are required.

```sh
make scenario                                      # Validate and export, without the ARM toolchain
make                                                # Build the default continuous scenario
make SCENARIO=input/windowed_signals.csv             # Build a finite scenario
make expected SCENARIO=input/windowed_signals.csv HORIZON_MS=12000
make test                                           # Python tests and production C with mocked I/O
```

`PYTHON`, `HOST_CC`, and the existing ARM `PREFIX` can be overridden. Paths containing spaces are not supported by Make prerequisites. All generated data is under the ignored `build/` directory:

| File | Purpose |
| --- | --- |
| `generated/scenario.h` | Constant C configuration compiled into firmware |
| `generated/scenario.json` | Scenario identity, resolved definitions, PGN schedules, and timing policy |
| `generated/scenario.dbc` | Extended-ID Classic CAN definitions for the selected signals |
| `generated/expected.csv` | Ideal scheduled values for a finite export horizon |
| `generated/expected.json` | Export horizon and associated scenario identity |

Changing the CSV, signal metadata, converter, or selected scenario regenerates the build input. Switching scenarios does not require `make clean`. The small scenario translation unit and final firmware artifacts are rebuilt on every firmware build, preventing stale data even with whole-second Make timestamps. Expected exports from a different scenario are removed rather than left beside a new manifest. Archive your manifest, DBC, expected files, and firmware with each run; rebuilding replaces these artifacts.

## CSV contract

Use the exact header below; each row configures one unique known SPN:

```csv
spn,start_ms,period_ms,duration_ms,pattern,value,min,max,pattern_period_ms,step_count
190,0,20,0,constant,1500,,,,
84,2000,100,0,ramp,,0,60,8000,
91,2000,50,0,constant,40,,,,
```

- `start_ms`: relative to the `$RUN` origin, not computer wall-clock time.
- `period_ms`: PGN transmission period. Signals in the same PGN must specify the same period; conflicts fail validation.
- `duration_ms`: active-window length; zero means continuous. A finite signal is active on `[start_ms, start_ms + duration_ms)`.
- `constant`: requires only `value` among the waveform parameters.
- Other patterns require `min`, `max`, and `pattern_period_ms`; `step` also requires `step_count` from 2 to 50. All unused parameter cells must be blank.
- Values use the firmware database's engineering units. Invalid/nonfinite/out-of-range values and duplicate or unknown SPNs fail the build with filename/row diagnostics.
- Millisecond fields are integers in `[0, 2147483647]`; periods must be positive, and finite start-plus-duration must fit that horizon. Existing firmware capacities apply.

Waveforms repeat from each signal's activation time:

| Pattern | Behavior during one waveform period |
| --- | --- |
| constant | Hold `value` |
| ramp | Rise linearly from min toward max, then reset to min |
| sine | Begin at midpoint rising; reach max at one quarter and min at three quarters |
| triangle | Begin at min, reach max halfway, return toward min |
| square | Max for the first half, min for the second |
| step | Equal-duration ascending levels including min and max, then reset |

`period_ms` and `pattern_period_ms` are different. The first controls messages; the second controls how fast values change. Sparse configuration rows can generate millions of frames without storing a value for every transmission. These patterns are synthetic stimuli, not a vehicle dynamics model.

## Firmware timing and controls

The scenario auto-starts after successful CAN initialization, including delayed initialization recovery. Configuration failure leaves it idle, with no fallback to the old defaults. There is no hidden five-second lead-in.

Each PGN's first nominal deadline is its earliest signal start. Later deadlines are that offset plus whole transmission periods. All active fields are evaluated at one common timestamp per frame. Before and after a field's active window, its bits remain all ones. If every field in a PGN is inactive, no frame is sent. A DBC decoder may display the all-ones field numerically; do not treat that as an expected physical measurement.

A later-starting field sharing a PGN appears at that PGN's next scheduled transmission. For example, a PGN starting at 0 with a 20 ms period cannot transmit a new field exactly at 7 ms; the field first appears at the 20 ms slot.

The main loop is not a hardware-timed transmitter. Actual send attempts may be late, and bus arbitration can delay reception further. Deadlines stay anchored rather than drifting. An overdue PGN is attempted once with the current values, without catch-up bursts; skipped slots that contained active signals are counted as missed deadlines. Millisecond tick differences are accumulated to handle clock rollover, provided processing continues at least once within each full 32-bit tick cycle.

| Serial command | Behavior |
| --- | --- |
| `STOP` | Stop scheduling; emit `$END` and run statistics. No additional zero-valued frame is sent. Already queued CAN frames can still complete. |
| `START` | Restart current configuration with a fresh common origin; optional existing duration/mode arguments still work. |
| `SCENARIO` | Clear manual changes, reload the compiled CSV configuration, enable TX logging, and restart. |
| `CONFIG ...` | Retain the existing serial syntax, with start/duration in seconds. Successful configuration pauses the run; issue `START` afterward. |
| `STATUS` | Show configuration identity, state, queued/busy/error counts, missed deadlines, and UART log drops. |
| `TXLOG 0` / `TXLOG 1` | Disable/enable attempted-frame logging. |

Finite configurations stop once all fields have expired. Any continuous field keeps the run alive until STOP/reset, unless a finite duration was passed to START. A new START resets per-run statistics but preserves the boot-wide `$TX` sequence counter. Existing `$RUN`, `$TX`, and `$END` record formats are unchanged. The separate `[SCENARIO]` record identifies the compiled configuration; manual edits identify it as `manual`.

The EEC2 mapping has been corrected to PGN 61443. Any previously exported DBC using 61442 must be replaced. Other database fields remain the existing definitions; exports are not a complete standards certification.

## TSMaster acceptance procedure

1. Build the selected scenario and expected export. Save their identity and artifacts. Flash through the configured SWD programmer. The existing `make flash` default is a Windows ST-LINK executable; configure a compatible local programmer separately.
2. Use the current STM32F103C8 board mapping: CAN PB8 RX/PB9 TX through a compatible transceiver; USART1 PA9 TX/PA10 RX at 115200 8N1. Prepare CAN termination, common ground, and an active receiving/ACKing CAN adapter.
3. Configure TSMaster for Classic CAN, extended IDs, and 250 kbps. Import `build/generated/scenario.dbc`. Begin raw CAN capture and optional UART capture before resetting the STM32.
4. For the default scenario, confirm `0CF00400` begins at run start, decoding SPN 190 as 1500 RPM at a nominal 20 ms period. Its engine-speed bytes are `E0 2E` at payload bytes 4–5.
5. From approximately 2000 ms, confirm SPN 84 ramps from 0 to 60 km/h over each 8000 ms cycle at a nominal 100 ms PGN period, and `0CF00300` carries pedal SPN 91 at 40% every 50 ms. Pedal byte 2 is `64` hexadecimal.
6. With `input/windowed_signals.csv`, confirm demand-torque SPN 512 appears in the existing EEC1 frame at 2000 ms and returns to all ones at 7000 ms while RPM continues. Pedal ends at 9000 ms; all transmission ends at 10000 ms.
7. Confirm STOP prevents new attempts and START repeats the same configured timeline. SCENARIO restores the compiled values after manual edits. Confirm finite runs produce `$END` and an all-windows-complete message.
8. Retain raw IDs/payloads/timestamps, decoded values, UART logs, and status/end statistics. Do not declare delivery verified from UART records alone.

For manual comparison, align capture timestamps to the STM32 run origin before comparing dynamic values. `expected.csv` uses ideal deadlines, while `$TX` uses actual send-attempt timestamps: evaluate the waveform at the attempt time if jitter matters. Requested values are an independent mathematical reference; quantization models the encoder's float32 scaling and half-up rounding. Use half a signal resolution plus a small numerical margin when comparing requested physical values. Values near a rounding boundary may fall into adjacent raw bins because firmware waveform arithmetic uses single precision.

Separate byte-delivery checks (attempted payload versus received payload) from physical-value checks (scenario versus decoded values). `$TX` status `Q` only means queued; `B` and `E` mean busy/error. UART sequence gaps can be log drops. The 115200 8N1 console has a theoretical 11520 bytes/s per direction, and frequent text logs or STATUS output can affect timing. The default scenario is deliberately low load; check counters before drawing conclusions from larger runs.

Automatic TSMaster capture comparison, live CSV upload, random patterns, external storage, and reactive vehicle simulation are outside this version.
