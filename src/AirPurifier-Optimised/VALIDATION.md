# Validation and size report

## Measured programme storage

All stages used the same AVR GCC 7.3.0, Arduino AVR 1.8.8, LCD library 1.1.2 and Leonardo-target compile/link flags, with `-Os`, LTO and section garbage collection. Programme storage is `.text + .data`; static RAM is `.data + .bss + .noinit`.

| Stage | Flash bytes | Free of 28,672 | Static RAM bytes |
|---|---:|---:|---:|
| Original snapshot | 28,408 | 264 | 1,114 |
| Integer sensor calculations | 26,374 | 2,298 | 1,108 |
| Integer calculations + EEPROM text, before timers | 25,486 | 3,186 | 1,157 |
| Final firmware with independent timers | **26,406** | **2,266** | **1,083** |
| Separate EEPROM programmer | 11,068 | 17,604 | 431 |

The final main firmware is **2,002 bytes smaller** than the original despite the new timers. Its static RAM use is 31 bytes lower. Remaining SRAM is not a stack budget guarantee: stack and temporary runtime use must also fit.

The EEPROM text image is 3,304 bytes, stored at 256–3,559. Settings at 0–255 are preserved. Text loading requires the separate programmer; no runtime copy of the full catalogue is kept in programme storage.

## Automated checks passed

- **572 firmware assertions** against the actual main sketch: independent and simultaneous timers, 5–240-minute bounds and steps, Off selection, initial/session selections, restart/cancel/editor timeout, manual adjustment, manual zero, standby/wake, rollover at 240 minutes, rounded-up remaining minutes, and no timer EEPROM writes.
- Button tests exercise debounced press/release, short actions only on release, two-second holds, suppression of the short action after a hold, wake-only consumption for all three buttons, existing Settings navigation, and Button 3 standby from the timer editor.
- The AHT20 integer transfer functions were checked at 81 paired raw boundary/interior samples against the physical double-precision formulas, within 0.05 displayed units. Negative-temperature formatting, near-zero negatives, humidity endpoints and the Bosch BMP280 calibration/raw reference sample were checked. The BMP sample produces 25.1 °C and 1,006.5 hPa.
- Every EEPROM text slot was read back through the actual runtime reader. Tests cover bad magic/count/checksum, payload corruption, read failures, fail-safe startup, non-aliasing date-editor labels and output expiry after a display fault.
- **7,145 programmer assertions** against the actual loader sketch: image contents, reserved-address protection, Wire-buffer/page-boundary limits, complete readback, interrupted writes with an invalid header, corrupt-write rejection, retry success, I²C failure and safe output levels.
- Catalogue generation checks cover byte-for-byte reproducibility, matching firmware checksum, capacity, oversized/invalid text and the 119-slot limit.
- Both sketches compile and link for ATmega32U4. The package main sketch was rebuilt and its recorded size checked. USB core code remains linked; no no-USB size reduction was used.
- Original source files were compared byte-for-byte with the snapshot; no original project files were changed.

Native tests used Zig 0.16.0's C++17 driver and fake hardware. They exercise production handler code, but do not emulate AVR instruction timing, interrupts, bootloader behaviour or electrical characteristics. The debug native run exposed signed left shifts in BMP compensation; these were replaced with equivalent multiplication, retaining the compensation formula.

## Limits and retained behaviour

No hardware was flashed or connected during validation. PWM waveforms, physical LED/relay off states, EEPROM write-cycle timing, USB upload/reset, sensor recovery, standby current, watchdog wake and encoder responsiveness require on-device checks. See the README checklist.

This change does not redesign unrelated settings behaviours found in the earlier review: the environmental stale timeout remains fixed at ten seconds even when longer sampling intervals are selected; clock-format cancellation and abandoning an unconfirmed settings edit retain their existing behaviour; existing save callers do not all report EEPROM write failures; and sensors that fail startup initialisation are not retried automatically. Normal default-interval operation and timer handling were tested; these retained issues are not claimed fixed.

Compiler logs retain the original unused-save-result and signed/unsigned settings-bound warnings. Native tests use a wider host `int`; AVR-specific binary size and compilation are checked separately with the real AVR compiler.
