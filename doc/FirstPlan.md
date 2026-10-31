# DIY Air Purifier — Arduino Firmware Specification

**Document status:** Semi-finalised — ready for implementation planning
**Purpose:** Define the firmware architecture, behaviour, user interface, configuration, and storage requirements before writing the Arduino sketch.

## 1. Project Requirements

Develop Arduino firmware for a DIY air purifier with:

- Adjustable fan speed.
- Adjustable LED brightness.
- PM1.0, PM2.5, and PM10 monitoring.
- Temperature, humidity, and atmospheric pressure monitoring.
- Real-time clock.
- LCD-based user interface.
- Persistent, user-configurable settings.
- Low-power standby mode.
- Automatic standby when both fan and LED are off.
- Three buttons and one rotary encoder for all user interactions.

### Implementation requirements

- Do not write blocking code for UI navigation, sensor updates, or timers.
- Use explicit state management.
- Keep power states, UI states, and output states conceptually separate.
- Keep hardware-specific details separate from application logic.
- Apply confirmed configuration changes immediately where applicable.
- Save persistent settings to EEPROM.
- Do not change the intended behaviour without discussing it first.

---

## 2. Hardware Components

| Component                  | Function                                        |
| -------------------------- | ----------------------------------------------- |
| Arduino                    | Main controller                                 |
| Button 1                   | Fan control / menu selection                    |
| Button 2                   | LED control / menu back                         |
| Button 3                   | Menu toggle / standby / wake-up                 |
| Rotary encoder             | Navigation and value adjustment                 |
| LCD2004 with I²C backpack  | Display                                         |
| PMS7003                    | Particulate matter measurements                 |
| AHT20                      | Temperature and humidity                        |
| BMP280                     | Atmospheric pressure                            |
| DS3231 RTC                 | Timekeeping                                     |
| AT24C32 EEPROM | Persistent configuration storage                |
| 5 V relay                  | Switch the 12 V fan power completely off        |
| 4-pin PC fans              | Air circulation, controlled by fixed 25 kHz PWM |
| LED light                  | Brightness controlled by fixed 25 kHz PWM       |

### Hardware assumptions

- Button 3 is connected to the planned external interrupt pin.
- The low-power implementation will use the Arduino Low Power library.
- The exact MCU, interrupt mapping, and wake-up implementation will be addressed separately.
- The DS3231 module is expected to include an AT24C32 EEPROM. 
- The AT24C32 has 4 KiB of storage (4,096 bytes).
- All components have been prototyped, except for the low-power behaviour and external-interrupt wake-up.

### Output control

**Fan**

- Fixed 25 kHz PWM.
- Relay controls the 12 V fan power.
- Relay must be OFF in standby.

**LED**

- Fixed 25 kHz PWM.
- 0% means OFF.

**PMS7003**

- Controlled using its enable pin.
- Can be enabled or disabled through a persistent configuration setting.

---

## 3. Firmware Architecture

Separate the firmware into three conceptual layers.

### 3.1 Power state

- `OPERATING`
- `STANDBY`

### 3.2 UI state

- `MAIN_DISPLAY`
- `FAN_ADJUSTMENT`
- `LED_ADJUSTMENT`
- `MENU`

The menu may have additional internal states for navigation and editing.

### 3.3 Output state

Track the current state of:

- Fan speed.
- LED brightness.
- Fan relay.
- PMS7003 enable state.
- LCD backlight.

These layers must remain conceptually separate.

For example, the purifier can be in `OPERATING` with both outputs off and the LCD backlight off. It does not enter `STANDBY` until the standby transition occurs.

---

## 4. Startup Behaviour

On initial power-up:

1. Initialise the hardware.
2. Initialise the RTC and EEPROM.
3. Load the saved configuration.
4. Validate the configuration.
5. Load safe defaults if the stored configuration is invalid.
6. Check whether the RTC time is valid.
7. Initialise the LCD.
8. Initialise the sensors according to the configuration.
9. Set the fan and LED to their saved startup defaults.
10. Enter `OPERATING` and display the main screen.

### Invalid RTC time

If the RTC has lost valid timekeeping information:

- Silently assume the time is correct.

The system should otherwise continue operating where possible.

### Startup defaults

Store these values persistently:

- `defaultFanSpeed`
- `defaultLedBrightness`

On startup, restore these values as the current output settings.

These defaults are not directly editable through the menu.

They are updated only when the user explicitly holds Button 3 to enter standby.

---

## 5. Operating Mode

In `OPERATING`:

- Read sensors periodically.
- Update particulate matter averages.
- Update the LCD as required.
- Handle buttons and rotary encoder input.
- Manage UI inactivity timers.
- Manage the LCD backlight.
- Check automatic standby conditions.
- Maintain the current fan and LED settings.

All periodic operations should be non-blocking.

### Sensor updates

The environmental sensors and PMS7003 should normally update according to the configured operating-mode sensor update interval.

The PMS7003 can be disabled through the configuration.

When disabled:

- Do not treat its readings as current measurements.
- Display `ERR` or `--` according to the defined display convention in Section 14.
- Do not attempt to use unavailable readings in the averaging calculation.
- Do not enable the sensor until the configuration is changed.

### PMS7003 startup

When the PMS7003 is enabled:

- Start the sensor.
- Treat its readings as unavailable for the first 10 seconds.
- Display `--` for particulate matter during this period.
- After the 10-second startup period, display valid readings once available.

The 10-second startup period is a fixed firmware constant, not a user-configurable setting.

### Sensor errors

If a sensor fails to respond or produces invalid readings:

- Display `ERR` in place of the affected reading.
- Do not silently replace failed readings with zero.
- Do not display stale readings as though they are current.
- Continue updating other functioning sensors where possible.
- Restore the normal reading when the sensor recovers.

Do not show a separate error message screen.

---

## 6. Standby Mode

### 6.1 Entering standby

Standby can be entered by:

1. Holding Button 3 for the fixed long-press duration.
2. Automatic standby after both fan speed and LED brightness have remained at 0% for the configured timeout.

### 6.2 Explicit standby: Long press Button 3

When the user explicitly enters standby:

1. Save the current fan speed as `defaultFanSpeed`.
2. Save the current LED brightness as `defaultLedBrightness`.
3. Persist these values to EEPROM.
4. Turn off the LED.
5. Switch off the fan relay.
6. Disable the PMS7003.
7. Turn off the LCD backlight.
8. Clear temporary UI states.
9. Enter `STANDBY`.

A long press must not also trigger the short-press action on release.

The long-press duration is a fixed firmware constant and is not configurable through the menu.

### 6.3 Automatic standby

Automatic standby occurs when:

- Fan speed is 0%.
- LED brightness is 0%.
- Both conditions have remained true for `AUTO_STANDBY_TIMEOUT`.

On automatic standby:

- Do not change the saved startup defaults.
- Turn off the fan relay.
- Ensure the LED is off.
- Disable the PMS7003.
- Turn off the LCD backlight.
- Enter `STANDBY`.

The explicit long-press and automatic-standby paths must remain distinct because only the explicit action updates the startup defaults.

### 6.4 Standby sensor updates

During standby:

- Keep the RTC running.
- Keep the LCD backlight off.
- Keep the fan relay off.
- Keep the LED off.
- Keep the PMS7003 disabled.
- Periodically read the AHT20 and BMP280.
- Periodically refresh the LCD contents without turning on the backlight.

Use the configured `STANDBY_SENSOR_INTERVAL` for environmental sensor sampling.

Use the following relationship for LCD refreshes:

`STANDBY_LCD_REFRESH_INTERVAL = STANDBY_LCD_INTERVAL_N × STANDBY_SENSOR_INTERVAL`

This allows the LCD contents to refresh less frequently than the environmental sensors.

The LCD should continue to show:

- Current date and time.
- Temperature.
- Humidity.
- Atmospheric pressure.

Particulate matter readings should be displayed as unavailable in standby.

### 6.5 Waking from standby

Button 3 is the intended wake-up control and will use the external interrupt mechanism.

On wake-up:

1. Resume normal execution.
2. Restore the operating state.
3. Turn on the LCD backlight.
4. Restore the saved startup fan speed.
5. Restore the saved startup LED brightness.
6. Switch on the fan relay if the restored fan speed is greater than 0%.
7. Enable the PMS7003 if configured to do so.
8. Start a new 10-second startup grace period if the PMS7003 has been enabled.
9. Resume sensor updates.
10. Return to the main display.

Do not restore the previous UI screen.

The system should return to `MAIN_DISPLAY`.

---

## 7. Button and Rotary Encoder Behaviour

### 7.1 Button 1

**Outside the menu**

- From `MAIN_DISPLAY`, enter `FAN_ADJUSTMENT`.
- From `LED_ADJUSTMENT`, switch directly to `FAN_ADJUSTMENT`.
- In `FAN_ADJUSTMENT`, advance to the next fan preset.

**Inside the menu**

- Enter a submenu.
- Begin editing a selected setting.
- Confirm an edited value.

### 7.2 Button 2

**Outside the menu**

- From `MAIN_DISPLAY`, enter `LED_ADJUSTMENT`.
- From `FAN_ADJUSTMENT`, switch directly to `LED_ADJUSTMENT`.
- In `LED_ADJUSTMENT`, advance to the next LED preset.

**Inside the menu**

- Go back one level.
- Cancel the current edit and restore the previous value.

### 7.3 Button 3

**Short press**

- From `MAIN_DISPLAY`, enter `MENU`.
- From `FAN_ADJUSTMENT`, return to `MAIN_DISPLAY`.
- From `LED_ADJUSTMENT`, return to `MAIN_DISPLAY`.
- From `MENU`, exit to `MAIN_DISPLAY`.

**Long press**

- Enter standby from any operating UI state.
- Save the current fan and LED values as startup defaults before shutting down.

**During standby**

- Use Button 3 as the wake-up control through the external interrupt mechanism.

The short-press and long-press actions must be mutually exclusive.

### 7.4 Rotary encoder

Outside the menu:

- Adjust fan speed in `FAN_ADJUSTMENT`.
- Adjust LED brightness in `LED_ADJUSTMENT`.
- No output adjustment occurs on the main display.

Inside the menu:

- Navigate between menu items.
- Change values while editing a setting.

### 7.5 Backlight wake-up

When the backlight is off during operating mode:

- The first user interaction (including rotary encoder) turns the backlight on.
- The first interaction must not change an output value or menu selection.
- The backlight inactivity timer resets.

**Button 3 special case:**

- If Button 3 is pressed while the backlight is off, the first press only wakes the backlight.
- A separate press is required to open the menu or initiate a long-press standby action.
- The first press must not enter standby, even if held.
- This rule keeps Button 3 consistent with the backlight-wake behaviour of the other controls.

---

## 8. Main Display

The main display has two screens.

Both screens display the current date and time.

The screens alternate automatically every `MAIN_SCREEN_ROTATION_INTERVAL`.

### Screen 1: Environmental readings

Required data:

- Date.
- Time.
- Temperature.
- Humidity.
- Atmospheric pressure.

### Screen 2: Particulate matter

Required data:

- Date.
- Time.
- PM1.0.
- PM2.5.
- PM10.

The exact LCD layout is intentionally unspecified.

The layout will be implemented separately once the firmware structure is established.

The main display does not need to show fan speed or LED brightness because each output has a dedicated adjustment mode.

### Screen rotation

- Rotate automatically while `MAIN_DISPLAY` is active.
- Continue rotating even when the LCD backlight is off.
- Reset the rotation timer when the rotation interval is changed.
- Do not rotate screens while the user is in fan adjustment, LED adjustment, or the menu.

---

## 9. Fan Adjustment Mode

Entered by pressing Button 1 from the main display.

Display the current fan speed and any other relevant information.

### Rotary encoder

- Adjust fan speed continuously.
- Use percentage values from 0% to 100%.
- Stop at the limits rather than wrapping around.
- Do not automatically snap to a preset after continuous adjustment.

### Button 1

Advance to the next configured fan preset.

Preset selection should:

- Use the current fan speed as the reference.
- Advance to the next preset above the current value.
- Wrap to the first preset if there is no higher preset.

### Button 2

Switch directly to LED adjustment mode.

### Button 3

Return to the main display.

### Inactivity

Exit fan adjustment mode after `UI_INACTIVITY_TIMEOUT` without user interaction.

Return to the main display.

---

## 10. LED Adjustment Mode

Entered by pressing Button 2 from the main display.

Display the current LED brightness and any other relevant information.

### Rotary encoder

- Adjust LED brightness continuously.
- Use percentage values from 0% to 100%.
- Stop at the limits rather than wrapping around.
- Do not automatically snap to a preset after continuous adjustment.

### Button 2

Advance to the next configured LED preset.

Preset selection should:

- Use the current LED brightness as the reference.
- Advance to the next preset above the current value.
- Wrap to the first preset if there is no higher preset.

### Button 1

Switch directly to fan adjustment mode.

### Button 3

Return to the main display.

### Inactivity

Exit LED adjustment mode after `UI_INACTIVITY_TIMEOUT` without user interaction.

Return to the main display.

---

## 11. Menu System

### 11.1 Navigation

The menu uses the following controls:

- Button 1: Enter / select / confirm.
- Button 2: Back / cancel.
- Button 3: Exit to main display, unless held for standby.
- Rotary encoder: Navigate or change values.

### 11.2 Editing behaviour

Use distinct navigation and editing states.

**Navigation**

- Rotate to select a menu item.
- Press Button 1 to enter or begin editing.

**Editing**

- Rotate to change the value.
- Press Button 1 to confirm.
- Press Button 2 to cancel and restore the previous value.

Confirmed configuration changes take effect immediately unless explicitly described otherwise.

### 11.3 Menu structure

```text
Main Menu
│
├── Date & Time
│   ├── Year
│   ├── Month
│   ├── Day
│   ├── Hour
│   ├── Minute
│   ├── Second
│   └── Confirm and set RTC
│
├── Display Settings
│   ├── Backlight Timeout
│   ├── Main Screen Rotation Interval
│   └── Standby LCD Refresh Interval Multiplier
│
├── Sensor Settings
│   ├── PMS7003 Enabled
│   ├── PM Average Window
│   ├── Operating Mode Sensor Update Interval
│   └── Standby Mode Sensor Update Interval
│
├── Power Settings
│   └── Automatic Standby Timeout
│
├── Fan Presets
│   ├── Preset 1
│   ├── Preset 2
│   ├── Preset 3
│   ├── Preset 4
│   ├── Preset 5
│   └── Preset 6
│
├── LED Presets
│   ├── Preset 1
│   ├── Preset 2
│   ├── Preset 3
│   ├── Preset 4
│   ├── Preset 5
│   └── Preset 6
│
├── Status
│
└── About
```

The precise arrangement and layout of menu items can be adjusted during implementation without changing their behaviour.

### 11.4 Menu inactivity

If the user stops interacting with the menu for `MENU_INACTIVITY_TIMEOUT`:

1. Exit the menu.
2. Return to `MAIN_DISPLAY`.
3. Turn off the LCD backlight.
4. Reset the automatic-standby timer.
5. Allow automatic standby timing to resume if both fan speed and LED brightness are 0%.

While the user actively interacts with the menu, automatic standby must not occur.

### 11.5 Date and time

The user can edit:

- Year.
- Month.
- Day.
- Hour.
- Minute.
- Second.

When confirmed:

- Validate the entered date and time.
- Set the RTC to the confirmed values.
- Apply the change immediately.

The RTC itself retains the current date and time; the EEPROM stores configuration, not the live clock value.

---

## 12. Configuration Settings

All user-configurable settings must be persisted to EEPROM.

### 12.1 Display settings

| Setting                         | Description                                                  |
| ------------------------------- | ------------------------------------------------------------ |
| `BACKLIGHT_TIMEOUT`             | Time without interaction before the LCD backlight turns off |
| `MAIN_SCREEN_ROTATION_INTERVAL` | Time between switching the two main display screens         |
| `STANDBY_LCD_INTERVAL_N`        | Multiplier used to calculate the standby LCD refresh interval |

**Menu mapping:**

- **Backlight Timeout** → `BACKLIGHT_TIMEOUT`
- **Main Screen Rotation Interval** → `MAIN_SCREEN_ROTATION_INTERVAL`
- **Standby LCD Refresh Interval Multiplier** → `STANDBY_LCD_INTERVAL_N`

The standby LCD refresh interval is calculated as:

`STANDBY_LCD_REFRESH_INTERVAL = STANDBY_LCD_INTERVAL_N × STANDBY_SENSOR_INTERVAL`

The multiplier determines how often the LCD contents refresh relative to the standby environmental sensor update interval.

These settings represent intervals or timeouts, not frequencies in hertz.

### 12.2 Sensor settings

| Setting                     | Description                                                                         |
| --------------------------- | ----------------------------------------------------------------------------------- |
| `PMS7003_ENABLED`           | Enables or disables the PMS7003 through the menu                                   |
| `PM_AVERAGE_WINDOW`         | Number of valid readings used to calculate the displayed particulate matter average |
| `OPERATING_SENSOR_INTERVAL` | Sensor update interval during operating mode                                        |
| `STANDBY_SENSOR_INTERVAL`   | Environmental sensor update interval during standby                                |

**Menu mapping:**

- **PMS7003 Enabled** → `PMS7003_ENABLED`
- **PM Average Window** → `PM_AVERAGE_WINDOW`
- **Operating Mode Sensor Update Interval** → `OPERATING_SENSOR_INTERVAL`
- **Standby Mode Sensor Update Interval** → `STANDBY_SENSOR_INTERVAL`

Both sensor update intervals determine how frequently the firmware attempts to update the relevant sensor readings in their respective power states.

When `PM_AVERAGE_WINDOW` changes:

- Clear the existing particulate matter averaging buffer.
- Reset its sample count and index.
- Begin collecting readings for the new window.
- Display `--` until valid readings are available for the new averaging window.

When `PMS7003_ENABLED` changes:

- Apply the change immediately.
- If disabled, disable the PMS7003 and display `--` for particulate matter readings.
- If enabled, start the PMS7003 and begin a new 10-second startup grace period.
- Do not treat intentional disablement as a sensor fault.

### 12.3 Power settings

| Setting                | Description                                                |
| ---------------------- | ---------------------------------------------------------- |
| `AUTO_STANDBY_TIMEOUT` | Time both outputs must remain off before automatic standby |

The long-press duration for Button 3 is a fixed firmware constant and is not configurable through the menu.

### 12.4 Output defaults

Persist:

- `defaultFanSpeed`
- `defaultLedBrightness`

These are not editable through the menu.

They are updated only by the explicit long-press standby action.

### 12.5 Presets

Store six independently configurable fan presets and six independently configurable LED presets.

Initial values for both:

- 0%
- 20%
- 40%
- 60%
- 80%
- 100%

Preset rules:

- Values must remain within 0–100%.
- Presets must be unique.
- Presets must be maintained in ascending order.
- Zero must be included.
- Continuous adjustment remains independent of preset values.

The number of presets remains fixed at six.

### 12.6 Fixed firmware constants

The following are not user-configurable:

- PWM frequency.
- Hardware pin assignments.
- LCD dimensions.
- Long-press duration.
- PMS7003 startup grace period of 10 seconds.
- Maximum supported PM averaging window.
- Maximum number of presets.

---

## 13. PM Averaging

`PM_AVERAGE_WINDOW` represents a number of readings, not a duration.

The displayed particulate matter values should be calculated from a rolling window of valid readings.

Requirements:

- Keep the number of readings bounded.
- Define a fixed maximum averaging window in firmware.
- Do not allow the configured window to exceed that maximum.
- Do not include invalid readings in the average.
- Do not treat missing readings as zero.
- Keep the implementation compatible with the MCU's memory constraints.

When `PM_AVERAGE_WINDOW` changes:

- Clear the existing averaging buffer.
- Reset the buffer's sample count and index.
- Begin collecting readings for the new window.
- Display `--` until valid readings are available for the new averaging period.

The exact maximum should be chosen during implementation based on the intended array size and available memory. (Use 10)

---

## 14. Display Values and Error Handling

Use distinct display values for different conditions.

| Condition                                          | Display                           |
| -------------------------------------------------- | --------------------------------- |
| PMS7003 is in its initial 10-second startup period | `--`                              |
| PMS7003 is disabled in configuration               | `OFF`                             |
| No valid PM readings are available yet             | `--`                              |
| A sensor has failed or reports an error            | `ERR`                             |
| A sensor is recovering after an error              | `ERR` until valid readings resume |

For environmental sensors:

- Display `ERR` in place of the affected temperature, humidity, or pressure reading when the corresponding sensor fails.
- Do not replace failed readings with zero.
- Do not show a separate error message screen.
- Do not display stale values as though they are current.
- Restore the normal reading when the sensor recovers.

For the PMS7003:

- Display `--` during its initial 10-second startup grace period.
- Display `OFF` when it is intentionally disabled.
- Display `ERR` if an actual sensor failure is detected after the startup grace period.
- Restore particulate readings when the sensor recovers and valid readings become available.

An intentionally disabled sensor must not be treated as a failed sensor.

---

### 15. Timer Management

Use independent, non-blocking timers for sensor updates, display behaviour, inactivity, and power-state transitions.

| Timer / Interval                | Purpose                                                   | Configuration source |
| ------------------------------- | --------------------------------------------------------- | -------------------- |
| `OPERATING_SENSOR_INTERVAL`     | Sensor updates during operating mode                      | Sensor Settings      |
| `STANDBY_SENSOR_INTERVAL`       | Environmental sensor updates during standby              | Sensor Settings      |
| `STANDBY_LCD_REFRESH_INTERVAL`  | Standby LCD content refreshes                             | Calculated from `STANDBY_LCD_INTERVAL_N` and `STANDBY_SENSOR_INTERVAL` |
| `MAIN_SCREEN_ROTATION_INTERVAL` | Switching between main display screens                   | Display Settings     |
| `BACKLIGHT_TIMEOUT`             | Turning off the LCD backlight after inactivity            | Display Settings     |
| `UI_INACTIVITY_TIMEOUT`         | Exiting fan or LED adjustment mode                        | Fixed firmware setting |
| `MENU_INACTIVITY_TIMEOUT`       | Exiting an abandoned menu                                 | Fixed firmware setting |
| `AUTO_STANDBY_TIMEOUT`          | Entering automatic standby                                | Power Settings       |
| `PMS7003_STARTUP_GRACE_PERIOD`  | Fixed 10-second PMS7003 startup period                    | Fixed firmware constant |
| Button 3 long-press duration    | Recognising an intentional long press                     | Fixed firmware constant |

`UI_INACTIVITY_TIMEOUT` and `MENU_INACTIVITY_TIMEOUT` are fixed firmware settings unless they are made user-configurable in a future revision.

#### 15.1 General timer rules

- Each timer must have a distinct responsibility.
- Read user-configurable intervals and timeouts from the active configuration.
- Avoid blocking delays.
- Use wraparound-safe elapsed-time calculations appropriate for the MCU's timer representation.
- When a configurable interval or timeout is changed and confirmed, apply the new value immediately and reset the corresponding timer.
- Keep timer handling independent of LCD rendering and sensor acquisition so that one task does not unnecessarily delay another.

#### 15.2 Sensor update timers

**Operating mode**

- Use `OPERATING_SENSOR_INTERVAL` to schedule sensor updates while the system is in `OPERATING`.
- Apply a confirmed change immediately.
- Reset the operating sensor update timer when the interval changes.

**Standby mode**

- Use `STANDBY_SENSOR_INTERVAL` to schedule AHT20 and BMP280 updates while the system is in `STANDBY`.
- The PMS7003 remains disabled during standby.
- Apply a confirmed change immediately.
- Recalculate `STANDBY_LCD_REFRESH_INTERVAL` if the sensor update interval changes.
- Reset the standby sensor update timer when the interval changes.

#### 15.3 Standby LCD refresh timer

Calculate the LCD refresh interval using:

`STANDBY_LCD_REFRESH_INTERVAL = STANDBY_LCD_INTERVAL_N × STANDBY_SENSOR_INTERVAL`

- Refresh the LCD contents at the calculated interval while in standby.
- Keep the LCD backlight off during standby.
- Refreshing the LCD must not turn on the backlight.
- When `STANDBY_LCD_INTERVAL_N` changes, recalculate the interval and reset the LCD refresh timer.
- When `STANDBY_SENSOR_INTERVAL` changes, recalculate the interval and reset the LCD refresh timer.

#### 15.4 Main screen rotation timer

- Switch between the two main display screens after each `MAIN_SCREEN_ROTATION_INTERVAL`.
- Rotate screens only while `MAIN_DISPLAY` is active.
- Continue rotating even when the LCD backlight is off.
- Do not rotate screens while the user is in fan adjustment, LED adjustment, or the menu.
- When `MAIN_SCREEN_ROTATION_INTERVAL` changes, apply the new interval and reset the rotation timer.

#### 15.5 Backlight inactivity timer

- Reset the backlight timer on user interaction.
- Turn off the LCD backlight after `BACKLIGHT_TIMEOUT` without interaction.
- When the backlight is off during operating mode, the first interaction must wake it without changing output values or UI selection.
- For Button 3, the first press while the backlight is off must only wake the backlight, even if the button remains held.
- A separate press is required to open the menu or initiate long-press standby.
- When `BACKLIGHT_TIMEOUT` changes, apply the new timeout and reset the backlight timer.

#### 15.6 UI and menu inactivity timers

**Fan and LED adjustment**

- Use `UI_INACTIVITY_TIMEOUT` to return to `MAIN_DISPLAY` when the user stops interacting with fan or LED adjustment mode.
- Reset the timer on relevant user interaction.
- This timeout is a fixed firmware setting in the current configuration.

**Menu**

- Use `MENU_INACTIVITY_TIMEOUT` to exit an abandoned menu.
- On timeout, return to `MAIN_DISPLAY` and turn off the LCD backlight.
- Reset the automatic-standby timer when the menu times out.
- This timeout is a fixed firmware setting in the current configuration.

#### 15.7 Automatic-standby timer

- Start timing when both fan speed and LED brightness become 0%.
- Reset the timer whenever either output becomes non-zero.
- Reset the timer when the user begins actively interacting with the menu.
- Do not trigger automatic standby while the menu is actively being used.
- When the menu times out, restart the automatic-standby countdown if both outputs remain at 0%.
- When `AUTO_STANDBY_TIMEOUT` changes, apply the new timeout and reset the automatic-standby timer if both outputs remain off.
- Automatic standby must not update `defaultFanSpeed` or `defaultLedBrightness`.

#### 15.8 PMS7003 startup timer

- Use `PMS7003_STARTUP_GRACE_PERIOD` to represent the fixed 10-second period after the PMS7003 is enabled.
- During this period, display `--` for particulate matter readings.
- Start or restart the grace period whenever the PMS7003 is enabled, including after waking from standby.
- Do not display readings as valid until the grace period has elapsed and valid readings are available.
- This period is a fixed firmware constant and is not configurable through the menu.

#### 15.9 Timer behaviour during state transitions

- On entry to standby, stop or suspend operating-mode timers as appropriate.
- During standby, run only the timers needed for standby sensor updates, LCD refreshes, and standby-related behaviour.
- On wake-up, resume operating-mode timers and reset timers whose elapsed time should not carry over from standby.
- On entry to fan adjustment, LED adjustment, or the menu, pause main-screen rotation.
- When returning to `MAIN_DISPLAY`, reset the rotation timer so the next screen change occurs after the full configured interval.
- Ensure that no timer accidentally triggers a delayed action immediately after a state transition.

---

## 16. EEPROM Persistence

Use the AT24C32 EEPROM for persistent configuration, provided the actual module contains it.

### Data to persist

- Configuration format version.
- Configuration validity information.
- Display settings.
- Sensor settings.
- Power settings.
- PM averaging window.
- Fan presets.
- LED presets.
- Startup fan speed.
- Startup LED brightness.

### Save rules

**Confirmed menu setting**

- Update the in-memory configuration.
- Validate the new value.
- Persist the change to EEPROM.
- Apply the setting immediately where applicable.
- Reset the associated timer if it is a timing setting.

**Cancelled menu edit**

- Restore the previous value.
- Do not save the cancelled value.

**Continuous fan or LED adjustment**

- Update the live output immediately.
- Do not write to EEPROM on every encoder movement.

**Explicit standby via long press**

- Save the current fan speed as the new startup fan speed.
- Save the current LED brightness as the new startup LED brightness.
- Persist these values before entering standby.

**Automatic standby**

- Do not update startup defaults.

### Storage robustness

- Validate stored configuration at startup.
- Use a configuration version.
- Provide safe defaults if stored data is invalid.
- Consider a checksum or other integrity check.
- Avoid unnecessary writes.
- Account for EEPROM write-cycle limits.

The storage implementation should use the EEPROM's actual I²C address and capacity.

---

## 17. Status Page

Provide a dedicated Status page in the menu, separate from About.

Its purpose is to show useful diagnostic information when needed.

Potential contents:

- RTC availability.
- EEPROM availability.
- PMS7003 connection and reading status.
- AHT20 connection status.
- BMP280 connection status.
- Current sensor error information.
- Current operating or standby state.

This page should be useful for troubleshooting, without requiring a general sensor-status indicator on the main screen.

Sensor errors should also appear directly in the affected readings.

---

## 18. About Page

Provide a simple About page.

Potential contents:

- Project name.
- Firmware version.
- Hardware revision.
- Author or project nickname.
- A short custom message.

This page is informational and does not modify any settings.

---

## 19. Implementation Principles

### Non-blocking execution

The firmware should continue to handle:

- Button input.
- Rotary encoder input.
- Sensor updates.
- LCD updates.
- PWM output.
- Relay control.
- Timers.
- State transitions.

Avoid long blocking operations that prevent other tasks from running.

### Separation of concerns

Keep separate modules or functions for:

- Hardware initialisation.
- Sensor acquisition.
- PM averaging.
- LCD rendering.
- Button and encoder handling.
- Menu navigation.
- Configuration management.
- EEPROM persistence.
- Operating-mode management.
- Standby and wake-up handling.

### LCD rendering

Initially prioritise correctness of data and state transitions over the exact layout.

The display layout can be refined independently later.

Try to update only the fields or regions that need refreshing. If this creates unnecessary complexity, a simpler refresh strategy can be used initially and optimised later.

### Error handling

- Report sensor errors directly in affected readings.
- Avoid silently treating failed readings as zero.
- Preserve valid readings internally where useful, but do not present stale readings as current.
- Keep unrelated system functions operating when possible.
- Do not display a separate error screen.

### Power management

The low-power implementation and external-interrupt wake-up behaviour are separate implementation tasks.

The firmware specification should define the intended standby behaviour without prematurely committing to a specific low-level sleep implementation.

---

## 20. Required Behaviour Summary

| Event                                          | Expected behaviour                                                                                       |
| ---------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| Power-up                                       | Load configuration and start operating with saved defaults                                               |
| Button 1 on main screen                        | Enter fan adjustment                                                                                     |
| Button 2 on main screen                        | Enter LED adjustment                                                                                     |
| Button 3 short press on main screen            | Open menu                                                                                                |
| Button 3 short press in fan mode               | Return to main screen                                                                                    |
| Button 3 short press in LED mode               | Return to main screen                                                                                    |
| Button 3 short press in menu                   | Exit to main screen                                                                                      |
| Button 3 long press while backlight is on      | Save current output values as startup defaults and enter standby                                         |
| Button 3 press while backlight is off          | Wake backlight only; do not trigger menu or standby                                                      |
| Button 3 wake-up interrupt during standby      | Resume operating mode                                                                                    |
| Both outputs remain off for configured timeout | Enter automatic standby without changing startup defaults                                                |
| Backlight timeout expires                      | Turn off the backlight                                                                                   |
| User interacts while backlight is off          | Wake backlight without unintended value changes                                                          |
| Menu is abandoned                              | Exit menu, return to main screen, turn off backlight, and restart automatic-standby timing if applicable |
| PMS7003 is enabled                             | Wait 10 seconds before displaying particulate readings                                                   |
| PMS7003 is disabled in configuration           | Keep PM readings unavailable and do not treat intentional disablement as an error                        |
| PM averaging window changes                    | Clear the buffer and collect new readings                                                                |
| Confirm a timing setting change                | Apply the new setting and reset the corresponding timer                                                  |
| Confirm a sensor setting change                | Apply the new configuration immediately where applicable                                                 |
| Cancel an edit                                 | Restore the previous value                                                                               |
| Sensor error occurs                            | Replace the affected reading with `ERR`                                                                  |
| Sensor recovers                                | Resume displaying valid readings                                                                         |
| Main display rotation interval expires         | Switch to the other main screen                                                                          |

---

## 21. Remaining Implementation Decisions

The following details may be finalised during implementation:

1. **Timer defaults and bounds:** Define minimum, maximum, and default values for all configurable intervals and timeouts.
2. **PM averaging maximum:** Choose the maximum number of samples based on memory constraints and desired responsiveness, use 10. 
3. **EEPROM layout:** Define the configuration structure, validation mechanism, and address allocation.
4. **LCD layout:** Determine the exact arrangement of values on the 20×4 display.
5. **Menu editing layout:** Determine how multi-field values, such as date and time, are edited on the LCD.
6. **Low-power implementation:** Confirm the sleep mode, interrupt configuration, and wake-up behaviour for the selected MCU.
7. **Sensor error detection:** Define the precise conditions that qualify as a sensor failure rather than a temporarily unavailable reading.

These are implementation details and do not change the overall architecture or intended behaviour described in this specification.

---

**End of specification.**
