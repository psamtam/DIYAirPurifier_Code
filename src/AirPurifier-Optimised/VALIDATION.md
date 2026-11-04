# Validation and optimisation report

## Programme size

The AVR compiler, core, LCD library, board definition and compile/link flags were held constant. Flash is `.text + .data`; static SRAM is `.data + .bss + .noinit`.

| Stage | Flash bytes | Flash free | Static SRAM bytes |
|---|---:|---:|---:|
| Original source | 28,408 | 264 | 1,114 |
| Previous timer/EEPROM package | 26,406 | 2,266 | 1,083 |
| File split alone | 26,406 | 2,266 | 1,083 |
| First recovery/rollback implementation | 27,216 | 1,456 | 1,095 |
| Final Smooth profile (default) | **26,372** | **2,300** | **1,310** |
| Final MinimumFlash profile | 26,236 | 2,436 | 1,142 |
| Separate EEPROM programmer | 11,068 | 17,604 | 431 |

The split added zero flash bytes. The final firmware includes the repairs and is **34 bytes smaller than the preceding package**, **2,036 bytes smaller than the original**, and below the requested 26,406-byte ceiling. Default static SRAM increased by 227 bytes over the preceding package for LCD/text caches and recovery state; 1,250 bytes remain before stack/runtime use. MinimumFlash leaves 1,418 bytes before stack use. That is not a measured stack guarantee.

Measured reductions include shared settings metadata/rendering, flash-resident default/calendar data, bounded 16-bit debounce/hold timestamps, exact AHT arithmetic factoring, date validation specialised to its unchanged 2000–2099 range, direct clock-row formatting, safe 16-bit PWM arithmetic, and fewer duplicate operations. Long countdowns/retry timestamps remain 32-bit. The default preset list has one editable source. The pressure compensation arithmetic is source-identical to the preceding package's 64-bit algorithm.

At the display comparison point, the character-cache candidate used 26,398 bytes versus 26,400 for dirty scheduling. Subsequent row composition, correctness and responsiveness work produced the final build. A six-slot cache initially exceeded the ceiling at 26,484 bytes. Further exact calendar/arithmetic reductions and removing unrelated sampling-timer resets made room for it. The selected default is 136 flash bytes and 168 SRAM bytes larger than the 26,236-byte MinimumFlash profile; it removes repeated idle asset reads. Both profiles are delivered and tested. Inlining changes that increased size were discarded. The remaining large blocks are the main/interrupt framework, environmental acquisition with 64-bit compensation, and menu/rendering handlers. LTO inlines code, so symbol sizes are not independent feature budgets; see the symbol list and linker map. Recorded stages/decisions are in the JSON files; no mathematically proven global minimum is claimed.

## Display and responsiveness checks

The benchmark renders a stable, valid environmental page for four seconds, with a 60-second sampling/rotation interval and RTC unavailable, then changes temperature from 23.0 to 22.0 C. It executes production rendering/loop code using fake I/O counters.

| Operation | Previous package | Dirty experiment | Smooth default | MinimumFlash |
|---|---:|---:|---:|---:|
| Idle LCD characters, four seconds | 1,280 | 320 | **0** | **0** |
| Idle cursor commands | 64 | 16 | **0** | **0** |
| Idle EEPROM read requests | 192 | 20 | **0** | **80** |
| LCD characters for temperature update | 80 | 80 | **1** | **1** |
| Cursor commands for that update | 4 | 4 | **1** | **1** |
| EEPROM reads for that update | 12 | 5 | **0** | **5** |

These are operation counts, not hardware timings. The character cache reduces LCD work more than the dirty scheduler. The default now also caches the page templates; MinimumFlash retains the smaller one-slot reader. Clock/sensor reads still occur when scheduled, and page changes can require cache fills. Final input handling bypasses the background refresh throttle once an action is recognised. Tests also verify zero idle LCD writes in adjustment/menu screens, complete-row padding, cursor jumps and cancellation of the previous clear/rewrite pattern.

Physical 250 ms response, clock stretching, missed encoder transitions and AVR execution time still require measurement on the board. The tests prove scheduling behaviour and transaction-count reductions, not a real-time hardware latency guarantee.

## Automated verification

- **3,795 firmware assertions** and **7,145 EEPROM programmer assertions** passed for each profile, plus catalogue-generation/compatibility checks.
- Clock tests cover both initial 12/24-hour choices, confirm/reload, Cancel, Button 3 exit, inactivity timeout, standby, NACK/corrupt-write retry, error display and later unrelated saves. RTC time-write attempts are counted and remain zero for format edits; explicit Date & Time confirmation still writes time.
- Recovery tests cover absent startup devices, runtime loss/reset, calibration still unset after an acknowledged command, NACK/ignored BMP configuration writes, NVM busy, both BMP addresses, retry spacing, fresh reading after recovery, healthy-device isolation, standby-sized wake steps and full `millis()` rollover. Recovery steps do not advance fake time through delays. Extra tests include nonzero command-transmission time, post-command reset/calibration timestamps, and conversion polling boundaries.
- Existing timer tests cover independent/simultaneous expiry, Off, 5–240-minute bounds/steps, draft cancellation, restart, manual adjustments/zero, standby, backlight wake and countdown rollover. Additional tests cover 16-bit debounce/hold rollover, full millis rollover and a hold lasting beyond one short-timer wrap.
- Every selectable PWM percentage was compared with the previous integer duty mappings. True LED LOW at zero is checked in fake registers; the physical signal remains a hardware check.
- The exact AHT fixed-point transformation was compared against both previous rounding formulas for **all 1,048,576 raw input values**. Existing transfer-function/negative-format tests and the Bosch BMP calibration/raw reference sample passed. Pressure compensation source was compared directly and is unchanged.
- Both clock layouts were checked at every hour/minute, including noon and midnight. Calendar validation was compared with Gregorian rules over 39,600 year/month/day candidates across every accepted year, plus rejected year/time boundaries. Display-cache tests cover changed rows, spaces, cursor gaps, text read failure/retry, eviction after failed reads and immediate input rendering. Temperature/humidity text buffers now match the formatter capacity, including the eight-character TEXT ERR fallback.
- The programmer tests verify region/page/buffer bounds, payload/header readback, corrupt writes, interrupted writes, retry and safe output states. Factory reset/settings saves preserve the asset region.
- Main firmware and programmer compile/link for ATmega32U4. Both packaged profiles were rebuilt and the budget enforced. No USB capability was removed. The final sketch emits no new application warnings under the recorded compiler settings.
- Original source files were compared byte-for-byte with the read-only project. The external catalogue/image are byte-identical to the previous package, and the persistent Config definition/layout/version remain unchanged.

Native tests use Zig 0.16.0's C++17 driver at `-O0 -g0` and fake GPIO/I2C/LCD/time/registers. Host integer widths differ from AVR; compilation/size checks use the real AVR compiler separately. Native debug-symbol linking stalled in one tool run; disabling native symbols and isolating benchmark output names allowed the recorded full suites to finish. This did not affect AVR build flags or firmware.

## On-device checks and retained issues

No hardware was uploaded or exercised. Verify sensor unplug/replug and power-reset recovery, both timer expiries together, encoder response during display updates, actual PWM/relay LOW/OFF states, EEPROM retention/readback, USB bootloader entry and standby current/watchdog wakes. Standby recovery waits are quantised to watchdog wakes; its existing 80 ms AHT acquisition wait and one-time startup notice remain.

Two earlier issues remain outside this change: the environmental stale timeout is still ten seconds even if a longer sampling interval is selected, and ordinary settings/startup-default save callers do not all surface write failures. Clock-format save failures, clock/config cancellation and AHT/BMP recovery are fixed. RTC runtime recovery and PMS protocol behaviour were not redesigned.
