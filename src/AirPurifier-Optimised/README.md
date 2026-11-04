# Air purifier: optimised, organised firmware

The candidate adds independent fan/LED delay-off timers, automatic AHT20/BMP280 recovery, and reliable clock-format/settings cancellation. It targets the ATmega32U4 **5 V / 16 MHz** board using the Leonardo definition. The original project at `C:\Dev\_test\DIYAirPurifier_Code` has not been edited.

The final main firmware uses **26,372 / 28,672 bytes**, leaving **2,300 bytes**. It is 34 bytes smaller than the preceding timer package, with the repairs included, and 2,036 bytes smaller than the original project. PM measurement/display remains available; PM-based automatic switching is excluded.

## Two verified build profiles

| Profile | Flash bytes | Flash free | Static SRAM | Text cache |
|---|---:|---:|---:|---:|
| **Smooth — default** | **26,372** | **2,300** | **1,310** | Six entries |
| MinimumFlash | 26,236 | 2,436 | 1,142 | One entry |

Both profiles have the same features, bug fixes, timing and external text image. The default uses 136 extra flash bytes and 168 extra SRAM bytes to eliminate repeated EEPROM text reads on an unchanged page. The minimum profile prioritises programme storage and performs more asset reads. Both remain below the 26,406-byte ceiling.

`binaries/AirPurifier.hex` is the default; `binaries/AirPurifier-minimum.hex` is the minimum profile. In source, choose `UI_TEXT_CACHE_SLOTS` (6 or 1) in Hardware.h, or use the build options below. There is no runtime menu for this compile-time choice.

## Upload

**Upgrading from the previous package:** the external text image is byte-for-byte unchanged. If that image is already loaded and valid, upload only the new main firmware.

For a first installation:

1. Open `UiEepromProgrammer/UiEepromProgrammer.ino`, select the correct Leonardo-compatible ATmega32U4 board/port, and upload it. It holds fan, LED and PMS outputs off.
2. Wait for **UI load verified** on the LCD. Serial also reports the result at 9600 baud if connected at programming time. If verification fails, correct the EEPROM wiring/address and retry.
3. Open and upload `AirPurifier/AirPurifier.ino`. Keep **the entire AirPurifier folder** together: its small `.ino` includes the interface, and Arduino compiles the implementation in `Firmware.cpp`.

Do not upload `ui-assets.bin` as the MCU's internal EEPROM file. It belongs in the external AT24C32. The supplied main HEX contains no bootloader; source builds are preferable for a different board definition.

Original wiring assumptions are retained: LCD `0x27`, AT24C32 `0x57`, active-low relay D8, LED D5, fan PWM D10, PMS enable D4. Addresses and polarities must match the device.

## Controls and cancellation

| Action | Result |
|---|---|
| Hold Button 1 for two seconds outside Settings | Open fan delay-off editor |
| Hold Button 2 for two seconds outside Settings | Open LED delay-off editor |
| Turn encoder | Choose **Off, 5, 10, …, 240 minutes** |
| Press the same button | Confirm and start/restart its timer, or disable it with Off |
| Other button, short Button 3, or ten seconds inactive | Discard the timer draft; leave an existing deadline unchanged |
| Hold Button 3 | Enter standby; cancel both countdowns |

LCD labels remain `Y` = Button 1, `A` = Button 2, `X` = Button 3. Both duration selections start at 30 minutes after power-up and are session-only. Timers never turn an output on. Adjusting a running output preserves its deadline; manually setting it to zero cancels its timer. Expiry switches off only the associated output and the fan relay when appropriate. Existing automatic standby follows when both outputs are off.

The adjustment screen shows rounded-up remaining minutes. Starting a timer for an already-off output shows **Output is off**. Short Button 1/2 actions happen on release; a long press never also cycles a preset. Backlight wake consumes the whole first press.

Clock-format editing now backs up the previous 12/24-hour choice. Cancel, Button 3 exit, Settings timeout and standby discard an unconfirmed change. Abandoned ordinary settings drafts also roll back before a later save. Cancelling an unrelated setting preserves healthy PMS sampling/averages and does not postpone environmental sampling.

Only explicit clock-format confirmation writes that preference to EEPROM. Failed write/readback leaves editing active and shows **ERR**, so it can be retried or cancelled. This workflow never writes RTC time registers. The separate Date & Time confirmation still sets the RTC.

## Sensor recovery and responsiveness

- AHT20/BMP280 failures at startup or during measurements invalidate the affected reading and schedule a retry after ten seconds. Healthy devices are not reinitialised.
- Recovery handles one device per loop pass. AHT waits start after command completion: 21 ms after reset, 11 ms after calibration, and a 90 ms conversion polling threshold, including millisecond-quantisation margins. These are staged timestamps, with no new operating-loop delays. BMP configuration is acknowledged and read back, both addresses are probed, and normal acquisition resumes after settling.
- A successful reinitialisation queues a fresh reading immediately rather than waiting a full configured sampling interval. Readings remain unavailable until measurement succeeds. The existing short startup notice can show ERR while staged initialisation is pending; the live readings recover automatically.
- Standby progresses recovery on nominal eight-second watchdog wakes and returns to sleep between steps. Consequently, its retry/wait timing is rounded up to wake opportunities. The existing standby AHT conversion wait is retained.
- An 80-character cache sends only changed LCD characters. Complete value rows avoid clearing and rewriting unchanged values. The default six-slot text cache reuses a complete page of templates; 32-byte EEPROM reads reduce cold-read transactions. The minimum profile keeps one cached entry.
- Recognised input requests rendering immediately, bypassing the normal 250 ms background refresh throttle. Sensor, serial and output-timer handling remain in the operating loop.
- I2C transactions remain bounded at 25 ms with bus reset. A text read failure displays TEXT ERR where LCD communication works. An LCD write timeout disables normal rendering until restart while controls and countdowns continue.

The AHT waits follow the [AHT20 manual](https://cdn-learn.adafruit.com/assets/assets/000/091/676/original/AHT20-datasheet-2020-4-16.pdf). BMP readiness/configuration checks follow the [Bosch BMP280 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf). The existing 64-bit pressure compensation is unchanged.

## Source layout

| Files | Responsibility |
|---|---|
| `AirPurifier.ino`, `Firmware.h`, `Firmware.cpp` | Arduino entry interface and ordered implementation includes |
| `Drivers.h`, `Hardware.h`, `State.h` | Sensor/RTC drivers, editable defaults/pins, runtime state |
| `Storage.h`, `Sensors.h`, `Outputs.h` | Configuration/assets, measurement/recovery, PWM/relay control |
| `InputAndPower.h`, `UiActions.h`, `Menu.h`, `Display.h`, `UiHelpers.h` | Input, power transitions, navigation, rendering/formatting |
| `Internal.h`, `CachedLcd.h`, existing small type headers | Cross-section declarations, display cache and types |

Private implementation headers contain definitions and are included **only by Firmware.cpp**, keeping one implementation translation unit and whole-unit optimisation. Include `Firmware.h` from new external code, and add cross-section helper declarations to `Internal.h` as needed. The guarded `.h` files use [Arduino-supported sketch extensions](https://docs.arduino.cc/arduino-cli/sketch-specification/).

Edit defaults in `Hardware.h`; `DEFAULT_PRESET_VALUES` supplies both default preset arrays from one list. Persistent setting limits/field offsets are centralised in `Storage.h`. Changing the persistent schema requires an explicit storage-version/migration decision. Existing Config layout and EEPROM version are unchanged.

`reference-original/` preserves the user's original source. `reference-before-repairs/` preserves the previous timer package. Display comparison fixtures are under `tests/fixtures/`; they are reference experiments, not the application to upload.

## EEPROM assets

Addresses **0–255** are reserved for settings. The image starts at 256 with an eight-byte little-endian format/count/checksum header, then 32-byte text slots. The current 103-slot image occupies **3,304 bytes**, ending at address 3,559, leaving **536 bytes** above it free. The capacity limit is 119 slots.

`ui-texts.json` is the catalogue; positions are stable IDs used by `uiText()`. Preserve positions and formatter placeholders when editing. Entries must be ASCII, at most 31 bytes, without embedded NUL. Screen rows are 20 characters wide. Run `python tools/generate_ui_assets.py` to regenerate matching firmware/loader headers and the binary, then rebuild and upload both sketches if the image changed.

The loader invalidates the header, writes/verifies the payload, then commits/verifies the final header. It respects page and Wire-buffer limits, never writes the settings region, and factory reset preserves assets. Main firmware validates the entire image before enabling outputs; missing, corrupt or mismatched assets leave outputs off with **UI EEPROM error / Load UI programmer**. There is no fallback copy of the catalogue in main flash; short status/fault tokens remain built in.

## Build and verify

Pinned environment: Arduino AVR core **1.8.8**, AVR GCC **7.3.0-atmel3.6.1-arduino7**, LiquidCrystal I2C **1.1.2**, `arduino:avr:leonardo`, 16 MHz, standard `-Os` and LTO. Flash allowance is 28,672 bytes and SRAM is 2,560 bytes.

```powershell
# Install pinned dependencies and build both sketches; never uploads.
.\tools\build.ps1 -InstallDependencies
# Subsequent default builds:
.\tools\build.ps1
# Minimum-flash build:
.\tools\build.ps1 -CacheProfile MinimumFlash
```

The measured builds used the included direct AVR driver because CLI package discovery could not resolve paths in this sandbox. It compiles the sketch and its C++ implementation with the pinned platform flags:

```powershell
python tools/build_avr.py AirPurifier build/main --arduino-data "C:\path\to\Arduino15" --lcd-library "C:\path\to\libraries\LiquidCrystal_I2C" --max-flash 26406
python tools/build_avr.py UiEepromProgrammer build/loader --arduino-data "C:\path\to\Arduino15" --lcd-library "C:\path\to\libraries\LiquidCrystal_I2C"
```

For the minimum profile, add `--extra=-DUI_TEXT_CACHE_SLOTS=1` to the direct main-firmware build command and use a different output directory. The default uses six slots.

Each build produces ELF, HEX, linker map, sizes and sorted symbols. Different tool versions/link order can change byte counts. Native tests use the actual delivered source with fake hardware, Python 3 and a C++17 compiler:

```powershell
python tools/test.py --compiler clang++
# Or:
python tools/test.py --compiler "C:\path\to\zig.exe" --zig
```

To reproduce the display-operation comparison:

```powershell
python tools/benchmark_display.py --compiler clang++ --compare previous=reference-before-repairs/AirPurifier --compare dirty=tests/fixtures/dirty-display/AirPurifier --compare final=AirPurifier
```

Run the native suite for MinimumFlash with `--extra=-DUI_TEXT_CACHE_SLOTS=1`. The same option selects that profile in the benchmark; label it `--compare minimum=AirPurifier` when comparing profiles.

Counts and limitations are recorded in `VALIDATION.md` and `validation/`. No device was flashed during development. Physical sensor reconnection, encoder response, PWM/relay levels, USB upload/reset, EEPROM retention and watchdog/standby behaviour still need on-device checks.
