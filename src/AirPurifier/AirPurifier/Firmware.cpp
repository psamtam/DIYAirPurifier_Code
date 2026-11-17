/*
  DIY AIR PURIFIER - Arduino Pro Micro / Leonardo (ATmega32U4, 5V/16MHz)
  Firmware for ATmega32U4; matching external EEPROM UI assets are required.
  Compact register-level sensor/RTC drivers and EEPROM-resident UI text.

  Libraries:
    - LiquidCrystal_I2C (the marcoschwartz-compatible API used by the prototype)

  AHT20, BMP280 and DS3231 use compact register-level drivers below.
  The AT24C32 is accessed directly over I2C at EEPROM_I2C_ADDRESS.
  Standby uses ATmega32U4 Power-down sleep with 8-second watchdog wake-ups
  and Button 3 external-interrupt wake-up.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#include "Config.h"
#include "StoredConfig.h"
#include "ButtonState.h"
#include "SettingId.h"
#include "UiAssets.h"
#include "DelayOffTimer.h"
#include "CachedLcd.h"


#include "Firmware.h"
#include "Internal.h"

// Keep one translation unit: the split adds no runtime layer.
#include "Drivers.h"
#include "Hardware.h"
#include "State.h"
#include "UiHelpers.h"
#include "Outputs.h"
#include "Storage.h"
#include "Sensors.h"
#include "InputAndPower.h"
#include "Menu.h"
#include "UiActions.h"
#include "Display.h"
#include "Application.h"
