#pragma once
// ============================================================================
// 1. USER-EDITABLE DEFAULTS AND LIMITS
// ============================================================================

// Hardware pins (Arduino digital pin numbers)
const uint8_t PIN_PMS_EN  = 4;
const uint8_t PIN_LED     = 5;   // OC3A / Timer3
const uint8_t PIN_BUTTON3 = 7;   // external interrupt-capable pin on Leonardo/32U4
const uint8_t PIN_RELAY   = 8;
const uint8_t PIN_FAN     = 10;  // OC1B / Timer1
const uint8_t PIN_ENC_A   = A0;
const uint8_t PIN_ENC_B   = A1;
const uint8_t PIN_BUTTON1 = 14;
const uint8_t PIN_BUTTON2 = 15;

const uint8_t LCD_I2C_ADDRESS = 0x27;
const uint8_t EEPROM_I2C_ADDRESS = 0x57; // Assumed AT24C32 address; verify on hardware.
const uint8_t AHT20_I2C_ADDRESS = 0x38;
const uint8_t BMP280_I2C_ADDRESS_1 = 0x76;
const uint8_t BMP280_I2C_ADDRESS_2 = 0x77;

const bool RELAY_ACTIVE_LOW = true;
const bool PMS_ENABLE_ACTIVE_HIGH = true;
const bool BUTTONS_ACTIVE_LOW = true;

// PWM configuration
const uint16_t PWM_TOP = 639;               // 16 MHz / 640 = 25 kHz, prescaler 1
const uint8_t PWM_FREQUENCY_KHZ = 25;       // Informational; timer TOP determines actual frequency.

// Editable initial configuration defaults (used if EEPROM is blank/invalid)
const uint16_t DEFAULT_BACKLIGHT_TIMEOUT_SECONDS = 30;
const uint16_t DEFAULT_MAIN_SCREEN_ROTATION_SECONDS = 5;
const uint8_t  DEFAULT_STANDBY_LCD_INTERVAL_N = 5;
const bool     DEFAULT_PMS7003_ENABLED = true;
const uint8_t  DEFAULT_PM_AVERAGE_WINDOW = 5;
const uint16_t DEFAULT_OPERATING_SENSOR_INTERVAL_SECONDS = 2;
const uint16_t DEFAULT_STANDBY_SENSOR_INTERVAL_SECONDS = 32;
const uint16_t DEFAULT_AUTO_STANDBY_TIMEOUT_MINUTES = 5;
const uint8_t  DEFAULT_FAN_SPEED = 50;
const uint8_t  DEFAULT_LED_BRIGHTNESS = 50;

// Editable setting limits
const uint16_t MIN_BACKLIGHT_TIMEOUT_SECONDS = 5;
const uint16_t MAX_BACKLIGHT_TIMEOUT_SECONDS = 300;
const uint16_t MIN_MAIN_SCREEN_ROTATION_SECONDS = 2;
const uint16_t MAX_MAIN_SCREEN_ROTATION_SECONDS = 60;
const uint8_t MIN_STANDBY_LCD_INTERVAL_N = 1;
const uint8_t MAX_STANDBY_LCD_INTERVAL_N = 20;
const uint8_t MIN_PM_AVERAGE_WINDOW = 1;
const uint8_t MAX_PM_AVERAGE_WINDOW = 10;
const uint16_t MIN_OPERATING_SENSOR_INTERVAL_SECONDS = 1;
const uint16_t MAX_OPERATING_SENSOR_INTERVAL_SECONDS = 60;
const uint16_t MIN_STANDBY_SENSOR_INTERVAL_SECONDS = 8;
const uint16_t MAX_STANDBY_SENSOR_INTERVAL_SECONDS = 320;
const uint16_t MIN_AUTO_STANDBY_TIMEOUT_MINUTES = 1;
const uint16_t MAX_AUTO_STANDBY_TIMEOUT_MINUTES = 60;

// Fixed firmware timing constants (edit here, not in the menu)
const uint32_t BUTTON_DEBOUNCE_MS = 35;
const uint32_t BUTTON3_LONG_PRESS_MS = 2000;
const uint32_t PMS7003_STARTUP_GRACE_MS = 10000UL;
const uint32_t UI_INACTIVITY_TIMEOUT_MS = 10000UL;
const uint32_t MENU_INACTIVITY_TIMEOUT_MS = 60000UL;
const uint32_t SENSOR_STALE_TIMEOUT_MS = 10000UL;
const uint32_t PMS_STALE_TIMEOUT_MS = 10000UL;
const uint8_t PM_BUFFER_CAPACITY = 10;
#define DEFAULT_PRESET_VALUES {0, 20, 40, 60, 80, 100}

// About-page text


// 6: cache a complete screen for smooth input; 1: minimum-flash profile.
// Both profiles retain the same features and external EEPROM image.
#ifndef UI_TEXT_CACHE_SLOTS
#define UI_TEXT_CACHE_SLOTS 6
#endif
static_assert(UI_TEXT_CACHE_SLOTS == 1 || UI_TEXT_CACHE_SLOTS == 6, "Choose a validated text cache profile: 1 or 6");
