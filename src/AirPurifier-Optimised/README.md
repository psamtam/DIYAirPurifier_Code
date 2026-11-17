# DIY Air Purifier

Firmware for a 5 V, 16 MHz ATmega32U4 board with a 20×4 I²C LCD. It controls the fan, LED, and particle sensor, displays environmental and particle readings, and provides clock, display, sensor, power, and preset settings.

## Features

- Reads temperature and humidity from an AHT20, pressure from a BMP280, particle concentrations from a PMS7003, and time from a DS3231.
- Alternates the home display between environmental readings and PM1.0, PM2.5, and PM10. Pressure is shown in hPa to five decimal places. This is display precision; the BMP280 is configured for normal operation with pressure oversampling ×16, whose typical pressure resolution is 0.16 Pa (0.0016 hPa). 
- Provides independent fan and LED delay-off timers, configurable presets, backlight timeout, automatic standby, and sensor recovery after read failures.
- In menus, scrolling down advances through selections; while editing, the encoder changes the selected value.
- Stores settings in the AT24C32 external EEPROM. UI text is stored in the same EEPROM, separately from settings.

## Controls

The LCD labels use **Y** for Button 1, **A** for Button 2, and **X** for Button 3.

| Control | Main screen | Settings and delay editor |
|---|---|---|
| Y / Button 1 | Open or cycle fan adjustment; hold for fan delay-off | Select or confirm; Y sets a delay |
| A / Button 2 | Open or cycle LED adjustment; hold for LED delay-off | Back or cancel |
| X / Button 3 | Open settings; hold to enter standby | Back or exit |
| Rotary encoder | Adjust the active output | Browse menu choices or adjust a value |

Delay choices are Off or 5 to 240 minutes in five-minute steps. Timers are session-only, never turn an output on, and switch off only their associated output. A timer cannot be started while its output is off. Encoder adjustments to a running output preserve its deadline; setting the output to zero cancels the timer.

The Settings menu contains date and time, display, sensor, power, fan preset, LED preset, status, About, and factory reset pages. Cancelled setting edits are discarded. The first interaction after the backlight turns off wakes the display without applying that interaction.

## Build and EEPROM setup

Open each sketch in the Arduino IDE, select a Leonardo-compatible ATmega32U4 board and port, then compile and upload it. The main sketch is `AirPurifier/AirPurifier.ino`; keep the full `AirPurifier` folder together. The EEPROM loader is `UiEepromProgrammer/UiEepromProgrammer.ino`.

For first setup or after changing UI text:

1. Edit `ui-texts.json` if needed and generate matching assets with `python tools/generate_ui_assets.py`.
2. Compile and upload the EEPROM loader sketch. It writes and verifies the image at addresses 256–4095 of the AT24C32, preserving the settings region at addresses 0–255.
3. If the generated text count or checksum changed, compile and upload the main firmware too. Those values are embedded in the main firmware and must match the EEPROM image.

The UI image is data, not an Arduino sketch. The loader sketch embeds the generated `UiEepromProgrammer/UiImage.h`; `ui-assets.bin` is the raw image for external EEPROM programming workflows. The generator also updates `AirPurifier/UiAssets.h` and `ui-assets-manifest.json`.

## Project layout

- `AirPurifier/` contains the main sketch, hardware configuration, sensor drivers, state, menus, display rendering, and application logic. `Firmware.cpp` includes the private implementation headers into one translation unit.
- `UiEepromProgrammer/` contains the sketch that writes the generated UI image to the external EEPROM.
- `ui-texts.json` is the text catalogue. Its positions are stable IDs; preserve existing positions and formatting placeholders. Text must be ASCII and no longer than 31 bytes per entry. LCD rows are 20 characters wide.
- `tools/generate_ui_assets.py` generates the firmware and loader headers, raw binary image, and manifest from the catalogue.

Hardware defaults and pin assignments are in `AirPurifier/Hardware.h`. The persistent settings layout and EEPROM version are in `AirPurifier/Storage.h` and `AirPurifier/StoredConfig.h`; changing that layout requires a migration decision.
