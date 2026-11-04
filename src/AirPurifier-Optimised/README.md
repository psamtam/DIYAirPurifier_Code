# Air purifier: smaller firmware and independent delay-off timers

This is a separate candidate for the ATmega32U4 **5 V / 16 MHz** board, built using the Arduino Leonardo target. The original project at `C:\Dev\_test\DIYAirPurifier_Code` was not edited. A source snapshot is included in `reference-original/` for comparison.

The main sketch uses integer sensor readings and external EEPROM display text. PM measurement/display remains available; **PM-based automatic switching is not included**.

## Install in this order

1. Open `UiEepromProgrammer/UiEepromProgrammer.ino` in Arduino IDE. Select the appropriate Leonardo-compatible ATmega32U4 board and port, and upload it. The loader holds the fan relay, LED and PMS sensor off.
2. Wait for **UI load verified** on the LCD. A serial message at 9600 baud also reports the result if the monitor is connected at programming time. If loading fails, correct the connection/address and run the loader again.
3. Open and upload `AirPurifier/AirPurifier.ino`. Keep its matching `UiAssets.h` and the other headers in that sketch folder.

The loader and main firmware are separate uploads; the loader is replaced by the main firmware. The text remains in the external EEPROM. Repeat both uploads whenever the text image changes. Do not upload the external text binary as the MCU's internal EEPROM file.

Wiring assumptions follow the original sketch: LCD `0x27`, AT24C32 `0x57`, active-low fan relay on D8, LED on D5, fan PWM on D10, PMS enable on D4. These addresses/polarities must match the device. The included `.hex` files target Leonardo; source builds are preferable when using a different board definition or bootloader.

## Use the timers

| Action | Result |
|---|---|
| Hold Button 1 for two seconds outside Settings | Open fan delay-off editor |
| Hold Button 2 for two seconds outside Settings | Open LED delay-off editor |
| Turn encoder | Select **Off, 5, 10, …, 240 minutes** |
| Press the same button | Confirm; start/restart its countdown, or disable it with Off |
| Press the other button or briefly press Button 3 | Discard the edit and return home |
| Leave the editor inactive for ten seconds | Discard the edit |
| Hold Button 3 | Enter standby as before; cancel both countdowns |

LCD button labels remain `Y` = Button 1, `A` = Button 2, `X` = Button 3.

Both editors start at **30 minutes after power-up**. Confirmed selections are remembered for that session only; active countdowns never persist across standby or power loss. Reopening an editor shows its last confirmed selection. Cancelling an edit leaves an existing countdown and deadline unchanged; confirming restarts it from that moment.

Fan speed and LED brightness adjustments preserve their deadlines. Manually setting an output to zero cancels its timer. Expiry affects only its own output, including switching the fan relay off for the fan timer. The adjustment screen shows rounded-up remaining minutes. Existing automatic standby starts its normal countdown once both outputs are off.

Timers never turn an output on. Trying to start one while its output is off shows **Output is off**; selecting Off or backing out still works. Short Button 1/2 actions now happen on release, so a long press does not also cycle a preset. The first press after backlight timeout is consumed entirely as a wake action.

## EEPROM text and failure handling

- Addresses **0–255** remain reserved for settings; the original settings format and clock-format address are retained.
- The text image starts at **256**. Its eight-byte little-endian header contains magic `0x58545041`, a 16-bit slot count and a 16-bit rolling checksum. Each text slot is 32 bytes, including NUL termination/padding.
- The current 103-slot image occupies **3,304 bytes**, ending at address **3,559**, leaving **536 bytes** above it free. Maximum capacity is 119 text slots.
- `ui-texts.json` is the text catalogue. Its array positions are the IDs used by `uiText()` in the main source; preserve positions when editing text. Entries must be ASCII, at most 31 bytes, without embedded NUL characters. Preserve formatter placeholders and their argument types. Screen lines are 20 characters wide.
- To regenerate the binary, loader header and main header, run `python tools/generate_ui_assets.py`, then rebuild and upload both sketches. There is no duplicate text catalogue embedded in the main firmware. Short status tokens and boot/fault messages remain built in.
- The loader invalidates the header first, writes and verifies the payload, then commits and verifies the final image. It never writes the settings region. Factory reset preserves the text image.
- Main firmware validates the complete image before enabling outputs. A missing, corrupt or mismatched image leaves outputs off and displays **UI EEPROM error / Load UI programmer**.
- A runtime text-read failure produces **TEXT ERR** where communication with the LCD still works. I²C operations use a 25 ms timeout with bus reset. An LCD write timeout disables further normal rendering until restart to avoid repeated stalled frames; input and countdown handling continue.

## Build and test

Pinned measurement environment: Arduino AVR core **1.8.8**, AVR GCC **7.3.0-atmel3.6.1-arduino7**, LiquidCrystal I2C **1.1.2**, target **arduino:avr:leonardo**, 16 MHz, standard `-Os` and LTO. Flash allowance is 28,672 bytes; SRAM is 2,560 bytes.

With Arduino CLI available, run in PowerShell:

```powershell
# Install the pinned dependencies, then build both sketches. No upload is performed.
.\tools\build.ps1 -InstallDependencies
# Subsequent builds:
.\tools\build.ps1
```

The measured builds used the direct AVR driver in `tools/build_avr.py`, reproducing the pinned platform's compile/link flags. Arduino CLI discovery could not resolve the package directory in the sandbox. To reproduce the measured driver build with your installed packages:

```powershell
python tools/build_avr.py AirPurifier build/main --arduino-data "C:\path\to\Arduino15" --lcd-library "C:\path\to\libraries\LiquidCrystal_I2C"
python tools/build_avr.py UiEepromProgrammer build/loader --arduino-data "C:\path\to\Arduino15" --lcd-library "C:\path\to\libraries\LiquidCrystal_I2C"
python tools/build_avr.py reference-original/AirPurifier build/original --arduino-data "C:\path\to\Arduino15" --lcd-library "C:\path\to\libraries\LiquidCrystal_I2C"
```

Each driver build produces an ELF, HEX, linker map, size JSON and sorted symbols. Installed compiler versions must match the pinned paths. Other compiler/core/library versions or link order may produce different byte counts. The main HEX does not contain a bootloader.

Native tests compile the actual delivered sketches with fake GPIO, time, I²C, LCD and AVR registers:

```powershell
python tools/test.py --compiler clang++
# Or with a portable Zig compiler:
python tools/test.py --compiler "C:\path\to\zig.exe" --zig
```

Python 3 and a native C++17 compiler are required for tests. These tests do not connect to hardware. Recorded results and limits are in `VALIDATION.md` and `validation/`.

## Remaining hardware checks

Verify both uploads and EEPROM retention on the actual board; exercise both timers together and standby/wake; observe true LED off and the relay state; check encoder responsiveness while EEPROM/LCD communication is active; measure fan/LED PWM frequency; test unplugged or stuck I²C devices; and confirm real sensor readings against the original firmware. USB bootloader entry and watchdog wake timing still need device verification.
