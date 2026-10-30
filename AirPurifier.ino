/*
  DIY AIR PURIFIER - Arduino Pro Micro / Leonardo (ATmega32U4, 5V/16MHz)
  Single-file firmware implementation for ATmega32U4.
  Compact register-level sensor/RTC drivers and flash-resident UI strings.

  Libraries:
    - LiquidCrystal_I2C (the marcoschwartz-compatible API used by the prototype)

  AHT20, BMP280 and DS3231 use compact register-level drivers below.
  The AT24C32 is accessed directly over I2C at EEPROM_I2C_ADDRESS.
  Logical standby is implemented. MCU deep-sleep is intentionally not enabled yet;
  validate the D7/INT6 wake-up and peripheral behaviour on the actual board first.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <avr/pgmspace.h>

#include "Config.h"
#include "StoredConfig.h"
#include "ButtonState.h"
#include "SettingId.h"


// ============================================================================
// COMPACT SENSOR / RTC DRIVERS
// Register-level replacements for Adafruit_AHTX0, Adafruit_BMP280 and RTClib.
// They retain the same sensor outputs and the BMP280's Adafruit default
// operating profile: T x2, P x16, filter x16, normal mode, 500 ms standby.
// ============================================================================

static uint8_t bcdToDec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t decToBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

class DateTime {
  uint16_t y; uint8_t mo, d, h, mi, se;
public:
  DateTime() : y(2000), mo(1), d(1), h(0), mi(0), se(0) {}
  DateTime(uint16_t yy, uint8_t mm, uint8_t dd, uint8_t hh, uint8_t mn, uint8_t ss)
    : y(yy), mo(mm), d(dd), h(hh), mi(mn), se(ss) {}
  uint16_t year() const { return y; }
  uint8_t month() const { return mo; }
  uint8_t day() const { return d; }
  uint8_t hour() const { return h; }
  uint8_t minute() const { return mi; }
  uint8_t second() const { return se; }
};

class DS3231Lite {
  bool readRegs(uint8_t reg, uint8_t *dst, uint8_t n) {
    Wire.beginTransmission(0x68); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)0x68, n) != n) return false;
    while (n--) *dst++ = Wire.read();
    return true;
  }
public:
  bool begin() {
    uint8_t v;
    return readRegs(0x0F, &v, 1) || probe();
  }
  bool probe() {
    Wire.beginTransmission(0x68); Wire.write((uint8_t)0x0F);
    return Wire.endTransmission() == 0;
  }
  bool lostPower() {
    uint8_t status = 0;
    return !readRegs(0x0F, &status, 1) || (status & 0x80);
  }
  DateTime now() {
    uint8_t r[7];
    if (!readRegs(0x00, r, 7)) return DateTime();
    uint8_t sec = bcdToDec(r[0] & 0x7F);
    uint8_t min = bcdToDec(r[1] & 0x7F);
    uint8_t hour;
    if (r[2] & 0x40) hour = (uint8_t)((bcdToDec(r[2] & 0x1F) % 12) + ((r[2] & 0x20) ? 12 : 0));
    else hour = bcdToDec(r[2] & 0x3F);
    uint8_t day = bcdToDec(r[4] & 0x3F);
    uint8_t month = bcdToDec(r[5] & 0x1F);
    uint16_t year = (uint16_t)(2000 + bcdToDec(r[6]) + ((r[5] & 0x80) ? 100 : 0));
    return DateTime(year, month, day, hour, min, sec);
  }
  void adjust(const DateTime &t) {
    Wire.beginTransmission(0x68); Wire.write((uint8_t)0x00);
    Wire.write(decToBcd(t.second())); Wire.write(decToBcd(t.minute()));
    Wire.write(decToBcd(t.hour())); Wire.write((uint8_t)1);
    Wire.write(decToBcd(t.day())); Wire.write(decToBcd(t.month()));
    Wire.write(decToBcd((uint8_t)(t.year() - 2000)));
    Wire.endTransmission();
    // Clear oscillator-stop flag after setting a valid time.
    Wire.beginTransmission(0x68); Wire.write((uint8_t)0x0F);
    Wire.endTransmission(false);
    if (Wire.requestFrom((uint8_t)0x68, (uint8_t)1) == 1) {
      uint8_t status = Wire.read() & (uint8_t)~0x80;
      Wire.beginTransmission(0x68); Wire.write((uint8_t)0x0F); Wire.write(status); Wire.endTransmission();
    }
  }
};

class AHT20Lite {
  bool writeCommand(uint8_t a, uint8_t b, uint8_t c) {
    Wire.beginTransmission(0x38); Wire.write(a); Wire.write(b); Wire.write(c);
    return Wire.endTransmission() == 0;
  }
public:
  bool begin() {
    Wire.beginTransmission(0x38);
    if (Wire.endTransmission() != 0) return false;
    writeCommand(0xBA, 0, 0); delay(20);
    uint8_t status = 0;
    Wire.requestFrom((uint8_t)0x38, (uint8_t)1);
    if (Wire.available()) status = Wire.read();
    if (!(status & 0x08)) { writeCommand(0xBE, 0x08, 0x00); delay(10); }
    return true;
  }
  bool read(float &temperature, float &humidity) {
    if (!writeCommand(0xAC, 0x33, 0x00)) return false;
    delay(80);
    uint8_t d[6];
    if (Wire.requestFrom((uint8_t)0x38, (uint8_t)6) != 6) return false;
    for (uint8_t i=0; i<6; ++i) d[i] = Wire.read();
    if (d[0] & 0x80) return false;
    uint32_t rawH = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t rawT = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    humidity = (rawH * 100.0f) / 1048576.0f;
    temperature = (rawT * 200.0f) / 1048576.0f - 50.0f;
    return true;
  }
};

class BMP280Lite {
  uint8_t address;
  uint16_t dig_T1, dig_P1;
  int16_t dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
  int32_t tFine;
  bool readBytes(uint8_t reg, uint8_t *dst, uint8_t n) {
    Wire.beginTransmission(address); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(address, n) != n) return false;
    while (n--) *dst++ = Wire.read();
    return true;
  }
  bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address); Wire.write(reg); Wire.write(value);
    return Wire.endTransmission() == 0;
  }
public:
  bool begin(uint8_t addr) {
    address = addr;
    uint8_t id;
    if (!readBytes(0xD0, &id, 1) || (id != 0x58 && id != 0x60)) return false;
    uint8_t c[24]; if (!readBytes(0x88, c, 24)) return false;
    dig_T1 = (uint16_t)(c[1]<<8 | c[0]); dig_T2 = (int16_t)(c[3]<<8 | c[2]); dig_T3 = (int16_t)(c[5]<<8 | c[4]);
    dig_P1 = (uint16_t)(c[7]<<8 | c[6]); dig_P2 = (int16_t)(c[9]<<8 | c[8]); dig_P3 = (int16_t)(c[11]<<8 | c[10]);
    dig_P4 = (int16_t)(c[13]<<8 | c[12]); dig_P5 = (int16_t)(c[15]<<8 | c[14]); dig_P6 = (int16_t)(c[17]<<8 | c[16]);
    dig_P7 = (int16_t)(c[19]<<8 | c[18]); dig_P8 = (int16_t)(c[21]<<8 | c[20]); dig_P9 = (int16_t)(c[23]<<8 | c[22]);
    if (!dig_P1) return false;
    // Adafruit_BMP280 default sampling/filter/standby profile.
    writeReg(0xF5, 0x90); // standby 500 ms, filter x16
    writeReg(0xF4, 0x57); // temperature x2, pressure x16, normal mode
    delay(2);
    return true;
  }
  bool read(float &temperature, float &pressureHpa) {
    uint8_t d[6]; if (!readBytes(0xF7, d, 6)) return false;
    int32_t adcP = ((int32_t)d[0]<<12) | ((int32_t)d[1]<<4) | (d[2]>>4);
    int32_t adcT = ((int32_t)d[3]<<12) | ((int32_t)d[4]<<4) | (d[5]>>4);
    int32_t v1 = ((((adcT >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
    int32_t v2 = (((((adcT >> 4) - (int32_t)dig_T1) * ((adcT >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
    tFine = v1 + v2;
    int32_t temp100 = (tFine * 5 + 128) >> 8;
    temperature = temp100 / 100.0f;
    int64_t p1 = (int64_t)tFine - 128000;
    int64_t p2 = p1 * p1 * (int64_t)dig_P6;
    p2 += (p1 * (int64_t)dig_P5) << 17;
    p2 += ((int64_t)dig_P4) << 35;
    p1 = ((p1 * p1 * (int64_t)dig_P3) >> 8) + ((p1 * (int64_t)dig_P2) << 12);
    p1 = (((((int64_t)1) << 47) + p1) * (int64_t)dig_P1) >> 33;
    if (!p1) return false;
    int64_t p = 1048576 - adcP;
    p = (((p << 31) - p2) * 3125) / p1;
    p1 = ((int64_t)dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    p2 = ((int64_t)dig_P8 * p) >> 19;
    p = ((p + p1 + p2) >> 8) + (((int64_t)dig_P7) << 4);
    pressureHpa = (float)p / 25600.0f;
    return true;
  }
};

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
const uint16_t DEFAULT_STANDBY_SENSOR_INTERVAL_SECONDS = 30;
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
const uint16_t MIN_STANDBY_SENSOR_INTERVAL_SECONDS = 5;
const uint16_t MAX_STANDBY_SENSOR_INTERVAL_SECONDS = 300;
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
const uint8_t DEFAULT_PRESETS[PRESET_COUNT] = {0, 20, 40, 60, 80, 100};

// About-page text

// ============================================================================
// 2. HARDWARE OBJECTS AND APPLICATION DATA
// ============================================================================

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 20, 4);
AHT20Lite aht;
BMP280Lite bmp;
DS3231Lite rtc;

bool lcdOK = false;
bool ahtOK = false;
bool bmpOK = false;
bool rtcOK = false;
bool eepromOK = false;

const uint32_t EEPROM_MAGIC = 0x41505246UL; // "APRF"
const uint16_t EEPROM_VERSION = 1;

Config config;
uint8_t fanSpeed = DEFAULT_FAN_SPEED;
uint8_t ledBrightness = DEFAULT_LED_BRIGHTNESS;

// Current readings and freshness/error state
float temperatureC = NAN;
float humidityPercent = NAN;
float pressureHpa = NAN;
bool ahtReadingValid = false;
bool bmpReadingValid = false;
uint32_t lastAhtSuccessMs = 0;
uint32_t lastBmpSuccessMs = 0;

uint16_t pm1 = 0, pm25 = 0, pm10 = 0;
uint16_t pmBuffer1[PM_BUFFER_CAPACITY];
uint16_t pmBuffer25[PM_BUFFER_CAPACITY];
uint16_t pmBuffer10[PM_BUFFER_CAPACITY];
uint8_t pmBufferCount = 0;
uint8_t pmBufferIndex = 0;
bool pmsFrameEverReceived = false;
bool pmsError = false;
uint32_t lastPmsFrameMs = 0;
uint32_t pmsEnabledAtMs = 0;

// State separation
 enum PowerState : uint8_t { OPERATING, STANDBY };
 enum UiState : uint8_t { MAIN_DISPLAY, FAN_ADJUSTMENT, LED_ADJUSTMENT, MENU };
PowerState powerState = OPERATING;
UiState uiState = MAIN_DISPLAY;

bool lcdBacklightOn = true;
bool fanRelayOn = false;
bool pmsPowered = false;
uint8_t mainDisplayPage = 0;

// Timers (all millis()-based and rollover-safe)
uint32_t lastOperatingSensorMs = 0;
uint32_t lastStandbySensorMs = 0;
uint32_t lastStandbyLcdMs = 0;
uint32_t lastMainRotationMs = 0;
uint32_t lastBacklightActivityMs = 0;
uint32_t lastUiActivityMs = 0;
uint32_t lastMenuActivityMs = 0;
uint32_t outputsOffSinceMs = 0;
uint32_t lastDisplayRenderMs = 0;

// Encoder decoder
uint8_t previousEncoderState = 0;
int8_t encoderAccumulator = 0;
int16_t encoderDetentsPending = 0;
const int8_t encoderTable[16] PROGMEM = {
  0, -1, 1, 0, 1, 0, 0, -1,
  -1, 0, 0, 1, 0, 1, -1, 0
};

// Debounced button state

ButtonState button1 = {PIN_BUTTON1, false, false, 0, 0, false};
ButtonState button2 = {PIN_BUTTON2, false, false, 0, 0, false};
ButtonState button3 = {PIN_BUTTON3, false, false, 0, 0, false};

// Button 3 wake-backlight-only latch
bool button3WakeOnlyUntilRelease = false;

// Menu navigation/editing
uint8_t menuIndex = 0;
bool menuEditing = false;
uint8_t editingPresetIndex = 0;
bool editingFanPreset = true;
Config configBeforeEdit;
uint8_t dateEditField = 0;
int editYear = 2026;
int editMonth = 1;
int editDay = 1;
int editHour = 0;
int editMinute = 0;
int editSecond = 0;
uint8_t dateMenuIndex = 0;
bool dateEditing = false;

// Menu pages: Date & Time, Display, Sensors, Power, Fan Presets, LED Presets, Status, About
const uint8_t MENU_ITEM_COUNT_ACTUAL = 8;

// PMS parser
uint8_t pmsFrame[32];
uint8_t pmsIndex = 0;

// ============================================================================
// 3. GENERAL HELPERS
// ============================================================================

bool elapsed(uint32_t now, uint32_t then, uint32_t interval) {
  return (uint32_t)(now - then) >= interval;
}

uint32_t secondsToMs(uint32_t seconds) { return seconds * 1000UL; }

void lcdLine(uint8_t row, const char *text) {
  if (!lcdOK) return;
  lcd.setCursor(0, row);
  uint8_t n = 0;
  while (*text && n < 20) { lcd.print(*text++); ++n; }
  while (n++ < 20) lcd.print(' ');
}

// Print flash-resident text without copying it into scarce SRAM.
void lcdLine(uint8_t row, const __FlashStringHelper *text) {
  if (!lcdOK) return;
  lcd.setCursor(0, row);
  lcd.print(text);
  uint8_t n = strlen_P((PGM_P)text);
  if (n > 20) n = 20;
  while (n++ < 20) lcd.print(' ');
}

// Small printf-like formatter for the handful of integer/string formats used
// by this UI. It deliberately avoids pulling the much larger avr-libc printf
// implementation into the firmware. Supported: %s, %u, %d, %% and widths
// such as %02u, %04d and %5u. No floating-point formatting is performed here.
void tinyFormat_P(char *out, uint8_t capacity, PGM_P fmt, ...) {
  if (!capacity) return;
  uint8_t pos = 0;
  va_list ap;
  va_start(ap, fmt);
  char ch = (char)pgm_read_byte(fmt++);
  while (ch && pos < capacity - 1) {
    if (ch != '%') { out[pos++] = ch; ch = (char)pgm_read_byte(fmt++); continue; }
    ch = (char)pgm_read_byte(fmt++);
    if (ch == '%') { out[pos++] = '%'; ch = (char)pgm_read_byte(fmt++); continue; }
    char pad = ' ';
    if (ch == '0') { pad = '0'; ch = (char)pgm_read_byte(fmt++); }
    uint8_t width = 0;
    while (ch >= '0' && ch <= '9') {
      width = (uint8_t)(width * 10 + (ch - '0'));
      ch = (char)pgm_read_byte(fmt++);
    }
    char spec = ch;
    ch = (char)pgm_read_byte(fmt++);
    if (spec == 's') {
      const char *v = va_arg(ap, const char *);
      if (!v) v = "";
      while (*v && pos < capacity - 1) out[pos++] = *v++;
    } else if (spec == 'u' || spec == 'd') {
      // AVR int and unsigned int are both 16 bits. Reading the promoted
      // argument as unsigned preserves all uint16_t values (including PM).
      bool negative = false;
      uint16_t value;
      if (spec == 'u') {
        value = va_arg(ap, unsigned int);
      } else {
        int16_t signedValue = va_arg(ap, int);
        negative = signedValue < 0;
        value = negative ? (uint16_t)(-signedValue) : (uint16_t)signedValue;
      }
      char digits[6];
      uint8_t count = 0;
      do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < sizeof(digits));
      uint8_t total = count + (negative ? 1 : 0);
      while (total < width && pos < capacity - 1) { out[pos++] = pad; ++total; }
      if (negative && pos < capacity - 1) out[pos++] = '-';
      while (count && pos < capacity - 1) out[pos++] = digits[--count];
    }
  }
  out[pos] = '\0';
  va_end(ap);
}

// Compact fixed-point formatter. Avoids pulling avr-libc's relatively large
// dtostrf floating-point formatting routine into the 32U4 firmware.
void formatFloat(char *out, size_t len, float value, uint8_t decimals) {
  if (!len) return;
  if (value != value || value > 32000.0f || value < -32000.0f) {
    strncpy(out, "ERR", len);
    out[len - 1] = '\0';
    return;
  }
  if (decimals > 2) decimals = 2;
  uint16_t scale = decimals == 2 ? 100 : (decimals == 1 ? 10 : 1);
  int32_t scaled = (int32_t)(value * scale + (value >= 0 ? 0.5f : -0.5f));
  bool negative = scaled < 0;
  uint32_t magnitude = negative ? (uint32_t)(-scaled) : (uint32_t)scaled;
  uint32_t whole = magnitude / scale;
  uint16_t fraction = magnitude % scale;
  char digits[6];
  uint8_t count = 0;
  do { digits[count++] = (char)('0' + whole % 10); whole /= 10; } while (whole && count < sizeof(digits));
  uint8_t pos = 0;
  if (negative && pos < len - 1) out[pos++] = '-';
  while (count && pos < len - 1) out[pos++] = digits[--count];
  if (decimals && pos < len - 1) {
    out[pos++] = '.';
    if (decimals == 2 && pos < len - 1) out[pos++] = (char)('0' + fraction / 10);
    if (pos < len - 1) out[pos++] = (char)('0' + fraction % 10);
  }
  out[pos] = '\0';
}

bool validDate(int y, int m, int d, int hh, int mm, int ss) {
  if (y < 2000 || y > 2099 || m < 1 || m > 12 || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return false;
  const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int maxDay = days[m - 1];
  bool leap = ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
  if (m == 2 && leap) maxDay = 29;
  return d >= 1 && d <= maxDay;
}

void setBacklight(bool on) {
  lcdBacklightOn = on;
  if (lcdOK) { if (on) lcd.backlight(); else lcd.noBacklight(); }
}

// ============================================================================
// 4. PWM, RELAY AND SENSOR POWER
// ============================================================================

void setupFanPWM() {
  pinMode(PIN_FAN, OUTPUT);
  digitalWrite(PIN_FAN, LOW);
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0;
  ICR1 = PWM_TOP; OCR1B = 0;
  TCCR1A = _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
}

void setupLedPWM() {
  pinMode(PIN_LED, OUTPUT);
  TCCR3A = 0; TCCR3B = 0; TCCR3C = 0; TCNT3 = 0;
  ICR3 = PWM_TOP; OCR3A = 0;
  TCCR3A = _BV(COM3A1) | _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);
}

void setFanPWM(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  uint16_t duty = ((uint32_t)percent * 640UL) / 100UL;
  OCR1B = (duty >= 640) ? 639 : duty;
}

void setLedPWM(uint8_t percent) {
  percent = constrain(percent, 0, 100);
  OCR3A = ((uint32_t)percent * PWM_TOP) / 100UL;
}

void setRelay(bool on) {
  fanRelayOn = on;
  digitalWrite(PIN_RELAY, (on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void setPmsPower(bool on) {
  pmsPowered = on;
  digitalWrite(PIN_PMS_EN, (on == PMS_ENABLE_ACTIVE_HIGH) ? HIGH : LOW);
  if (on) {
    pmsEnabledAtMs = millis();
    pmsFrameEverReceived = false;
    pmsError = false;
    pmBufferCount = 0;
    pmBufferIndex = 0;
    pmsIndex = 0;
  } else {
    pmsFrameEverReceived = false;
    pmsError = false;
    pmBufferCount = 0;
    pmBufferIndex = 0;
    pmsIndex = 0;
  }
}

void applyFanOutput() {
  setFanPWM(fanSpeed);
  setRelay(fanSpeed > 0);
}

void applyLedOutput() { setLedPWM(ledBrightness); }

void applyOutputs() {
  applyFanOutput();
  applyLedOutput();
}

// ============================================================================
// 5. AT24C32 EEPROM CONFIGURATION STORAGE
// ============================================================================

uint16_t checksumBytes(const uint8_t *data, size_t length) {
  uint16_t sum = 0xA55A;
  for (size_t i = 0; i < length; ++i) sum = (uint16_t)((sum << 5) | (sum >> 11)) ^ data[i];
  return sum;
}

bool eepromWriteBytes(uint16_t address, const uint8_t *data, uint16_t length) {
  // AT24C32 page size is commonly 32 bytes. Split writes at page boundaries.
  while (length) {
    uint8_t pageRemaining = 32 - (address % 32);
    uint8_t chunk = (length < pageRemaining) ? length : pageRemaining;
    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    Wire.write((uint8_t)(address >> 8));
    Wire.write((uint8_t)(address & 0xFF));
    for (uint8_t i = 0; i < chunk; ++i) Wire.write(data[i]);
    if (Wire.endTransmission() != 0) return false;
    delay(5); // EEPROM internal write-cycle time; not part of the main loop.
    address += chunk; data += chunk; length -= chunk;
  }
  return true;
}

bool eepromReadBytes(uint16_t address, uint8_t *data, uint16_t length) {
  while (length) {
    uint8_t chunk = (length > 28) ? 28 : length;
    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    Wire.write((uint8_t)(address >> 8));
    Wire.write((uint8_t)(address & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    uint8_t received = Wire.requestFrom((int)EEPROM_I2C_ADDRESS, (int)chunk);
    if (received != chunk) return false;
    for (uint8_t i = 0; i < chunk; ++i) data[i] = Wire.read();
    address += chunk; data += chunk; length -= chunk;
  }
  return true;
}

void setDefaultConfig() {
  config.backlightTimeoutSeconds = DEFAULT_BACKLIGHT_TIMEOUT_SECONDS;
  config.mainScreenRotationSeconds = DEFAULT_MAIN_SCREEN_ROTATION_SECONDS;
  config.standbyLcdIntervalN = DEFAULT_STANDBY_LCD_INTERVAL_N;
  config.pmsEnabled = DEFAULT_PMS7003_ENABLED ? 1 : 0;
  config.pmAverageWindow = DEFAULT_PM_AVERAGE_WINDOW;
  config.operatingSensorIntervalSeconds = DEFAULT_OPERATING_SENSOR_INTERVAL_SECONDS;
  config.standbySensorIntervalSeconds = DEFAULT_STANDBY_SENSOR_INTERVAL_SECONDS;
  config.autoStandbyTimeoutMinutes = DEFAULT_AUTO_STANDBY_TIMEOUT_MINUTES;
  for (uint8_t i = 0; i < PRESET_COUNT; ++i) {
    config.fanPresets[i] = DEFAULT_PRESETS[i];
    config.ledPresets[i] = DEFAULT_PRESETS[i];
  }
  config.defaultFanSpeed = DEFAULT_FAN_SPEED;
  config.defaultLedBrightness = DEFAULT_LED_BRIGHTNESS;
}

bool presetsValid(const uint8_t *p) {
  if (p[0] != 0) return false;
  for (uint8_t i = 0; i < PRESET_COUNT; ++i) {
    if (p[i] > 100) return false;
    if (i && p[i] <= p[i - 1]) return false;
  }
  return true;
}

bool configValid(const Config &c) {
  if (c.backlightTimeoutSeconds < MIN_BACKLIGHT_TIMEOUT_SECONDS || c.backlightTimeoutSeconds > MAX_BACKLIGHT_TIMEOUT_SECONDS) return false;
  if (c.mainScreenRotationSeconds < MIN_MAIN_SCREEN_ROTATION_SECONDS || c.mainScreenRotationSeconds > MAX_MAIN_SCREEN_ROTATION_SECONDS) return false;
  if (c.standbyLcdIntervalN < MIN_STANDBY_LCD_INTERVAL_N || c.standbyLcdIntervalN > MAX_STANDBY_LCD_INTERVAL_N) return false;
  if (c.pmsEnabled > 1) return false;
  if (c.pmAverageWindow < MIN_PM_AVERAGE_WINDOW || c.pmAverageWindow > MAX_PM_AVERAGE_WINDOW) return false;
  if (c.operatingSensorIntervalSeconds < MIN_OPERATING_SENSOR_INTERVAL_SECONDS || c.operatingSensorIntervalSeconds > MAX_OPERATING_SENSOR_INTERVAL_SECONDS) return false;
  if (c.standbySensorIntervalSeconds < MIN_STANDBY_SENSOR_INTERVAL_SECONDS || c.standbySensorIntervalSeconds > MAX_STANDBY_SENSOR_INTERVAL_SECONDS) return false;
  if (c.autoStandbyTimeoutMinutes < MIN_AUTO_STANDBY_TIMEOUT_MINUTES || c.autoStandbyTimeoutMinutes > MAX_AUTO_STANDBY_TIMEOUT_MINUTES) return false;
  if (c.defaultFanSpeed > 100 || c.defaultLedBrightness > 100) return false;
  return presetsValid(c.fanPresets) && presetsValid(c.ledPresets);
}

bool loadConfig() {
  StoredConfig stored;
  if (!eepromReadBytes(0, (uint8_t *)&stored, sizeof(stored))) return false;
  if (stored.magic != EEPROM_MAGIC || stored.version != EEPROM_VERSION) return false;
  uint16_t expected = checksumBytes((const uint8_t *)&stored.config, sizeof(Config));
  if (stored.checksum != expected || !configValid(stored.config)) return false;
  config = stored.config;
  return true;
}

bool saveConfig() {
  StoredConfig stored;
  stored.magic = EEPROM_MAGIC;
  stored.version = EEPROM_VERSION;
  stored.config = config;
  stored.checksum = checksumBytes((const uint8_t *)&stored.config, sizeof(Config));
  return eepromWriteBytes(0, (const uint8_t *)&stored, sizeof(stored));
}

// ============================================================================
// 6. SENSOR ACQUISITION AND PM ROLLING AVERAGE
// ============================================================================

uint16_t read16(uint8_t hi, uint8_t lo) { return ((uint16_t)hi << 8) | lo; }

void clearPmAverage() {
  pmBufferCount = 0;
  pmBufferIndex = 0;
  memset(pmBuffer1, 0, sizeof(pmBuffer1));
  memset(pmBuffer25, 0, sizeof(pmBuffer25));
  memset(pmBuffer10, 0, sizeof(pmBuffer10));
}

void addPmSample(uint16_t v1, uint16_t v25, uint16_t v10) {
  uint8_t window = config.pmAverageWindow;
  pmBuffer1[pmBufferIndex] = v1;
  pmBuffer25[pmBufferIndex] = v25;
  pmBuffer10[pmBufferIndex] = v10;
  pmBufferIndex = (pmBufferIndex + 1) % window;
  if (pmBufferCount < window) pmBufferCount++;
  uint32_t s1 = 0, s25 = 0, s10 = 0;
  for (uint8_t i = 0; i < pmBufferCount; ++i) { s1 += pmBuffer1[i]; s25 += pmBuffer25[i]; s10 += pmBuffer10[i]; }
  pm1 = s1 / pmBufferCount;
  pm25 = s25 / pmBufferCount;
  pm10 = s10 / pmBufferCount;
}

void pollPMS7003() {
  if (!pmsPowered) return;
  while (Serial1.available()) {
    uint8_t b = (uint8_t)Serial1.read();
    if (pmsIndex == 0 && b != 0x42) continue;
    if (pmsIndex == 1 && b != 0x4D) { pmsIndex = (b == 0x42) ? 1 : 0; continue; }
    pmsFrame[pmsIndex++] = b;
    if (pmsIndex == sizeof(pmsFrame)) {
      pmsIndex = 0;
      if (pmsFrame[2] != 0x00 || pmsFrame[3] != 0x1C) continue;
      uint16_t sum = 0;
      for (uint8_t i = 0; i < 30; ++i) sum += pmsFrame[i];
      if (sum != read16(pmsFrame[30], pmsFrame[31])) continue;
      // Atmospheric-environment values (CF=1 values are at bytes 10-15).
      addPmSample(read16(pmsFrame[10], pmsFrame[11]), read16(pmsFrame[12], pmsFrame[13]), read16(pmsFrame[14], pmsFrame[15]));
      pmsFrameEverReceived = true;
      pmsError = false;
      lastPmsFrameMs = millis();
    }
  }
  uint32_t now = millis();
  if (elapsed(now, pmsEnabledAtMs, PMS7003_STARTUP_GRACE_MS)) {
    if (!pmsFrameEverReceived || elapsed(now, lastPmsFrameMs, PMS_STALE_TIMEOUT_MS)) pmsError = true;
  }
}

void readEnvironmentalSensors() {
  uint32_t now = millis();
  if (ahtOK) {
    float t, h;
    if (aht.read(t, h) && !isnan(t) && !isnan(h)) {
      temperatureC = t; humidityPercent = h;
      ahtReadingValid = true; lastAhtSuccessMs = now;
    } else ahtReadingValid = false;
  } else ahtReadingValid = false;

  if (bmpOK) {
    float t, p;
    if (bmp.read(t, p) && !isnan(t) && !isnan(p) && p > 0) {
      pressureHpa = p;
      // AHT20 remains the chosen temperature/humidity source on the display.
      bmpReadingValid = true; lastBmpSuccessMs = now;
    } else bmpReadingValid = false;
  } else bmpReadingValid = false;
}

bool pmsInGracePeriod() { return pmsPowered && !elapsed(millis(), pmsEnabledAtMs, PMS7003_STARTUP_GRACE_MS); }
bool pmValuesAvailable() { return pmsPowered && !pmsInGracePeriod() && !pmsError && pmBufferCount >= config.pmAverageWindow; }

// ============================================================================
// 7. BUTTONS AND ROTARY ENCODER
// ============================================================================

bool rawPressed(uint8_t pin) { return BUTTONS_ACTIVE_LOW ? digitalRead(pin) == LOW : digitalRead(pin) == HIGH; }

void initialiseButton(ButtonState &b) {
  bool p = rawPressed(b.pin);
  b.stablePressed = p; b.lastRawPressed = p; b.rawChangedAt = millis();
  b.pressedAt = 0; b.longPressFired = false;
}

// Returns true only on a debounced press/release edge.
bool updateButton(ButtonState &b, bool &pressedEdge, bool &releasedEdge) {
  pressedEdge = false; releasedEdge = false;
  bool raw = rawPressed(b.pin);
  uint32_t now = millis();
  if (raw != b.lastRawPressed) { b.lastRawPressed = raw; b.rawChangedAt = now; }
  if (raw != b.stablePressed && elapsed(now, b.rawChangedAt, BUTTON_DEBOUNCE_MS)) {
    b.stablePressed = raw;
    if (raw) { pressedEdge = true; b.pressedAt = now; b.longPressFired = false; }
    else releasedEdge = true;
    return true;
  }
  return false;
}

void pollEncoder() {
  uint8_t a = digitalRead(PIN_ENC_A);
  uint8_t b = digitalRead(PIN_ENC_B);
  uint8_t state = (a << 1) | b;
  uint8_t transition = (previousEncoderState << 2) | state;
  encoderAccumulator += (int8_t)pgm_read_byte(&encoderTable[transition]);
  previousEncoderState = state;
  if (encoderAccumulator >= 4) { encoderDetentsPending++; encoderAccumulator = 0; }
  else if (encoderAccumulator <= -4) { encoderDetentsPending--; encoderAccumulator = 0; }
}

void noteUserActivity() {
  uint32_t now = millis();
  lastBacklightActivityMs = now;
  lastUiActivityMs = now;
  lastMenuActivityMs = now;
}

void enterMainDisplay() {
  uiState = MAIN_DISPLAY;
  menuEditing = false;
  dateEditing = false;
  lastMainRotationMs = millis();
  lastUiActivityMs = millis();
}

void enterStandby(bool explicitRequest) {
  if (explicitRequest) {
    config.defaultFanSpeed = fanSpeed;
    config.defaultLedBrightness = ledBrightness;
    saveConfig();
  }
  fanSpeed = 0;
  ledBrightness = 0;
  setFanPWM(0);
  setLedPWM(0);
  setRelay(false);
  setPmsPower(false);
  setBacklight(false);
  powerState = STANDBY;
  uiState = MAIN_DISPLAY;
  menuEditing = false;
  dateEditing = false;
  outputsOffSinceMs = 0;
  lastStandbySensorMs = millis();
  lastStandbyLcdMs = millis();
  lcdLine(0, F("Air Purifier Standby"));
}

void wakeFromStandby() {
  powerState = OPERATING;
  fanSpeed = config.defaultFanSpeed;
  ledBrightness = config.defaultLedBrightness;
  applyOutputs();
  setPmsPower(config.pmsEnabled != 0);
  setBacklight(true);
  enterMainDisplay();
  clearPmAverage();
  lastOperatingSensorMs = millis();
  lastBacklightActivityMs = millis();
}

void adjustActiveOutput(int16_t delta) {
  if (!delta) return;
  if (uiState == FAN_ADJUSTMENT) fanSpeed = constrain((int)fanSpeed + delta, 0, 100);
  else if (uiState == LED_ADJUSTMENT) ledBrightness = constrain((int)ledBrightness + delta, 0, 100);
  applyOutputs();
  if (fanSpeed == 0 && ledBrightness == 0) { if (!outputsOffSinceMs) outputsOffSinceMs = millis(); }
  else outputsOffSinceMs = 0;
}

uint8_t nextPresetAbove(const uint8_t *presets, uint8_t current) {
  for (uint8_t i = 0; i < PRESET_COUNT; ++i) if (presets[i] > current) return presets[i];
  return presets[0];
}

// ============================================================================
// 8. MENU CONFIGURATION EDITING
// ============================================================================


int getSettingValue(SettingId id) {
  switch (id) {
    case SET_BACKLIGHT: return config.backlightTimeoutSeconds;
    case SET_ROTATION: return config.mainScreenRotationSeconds;
    case SET_STANDBY_LCD_N: return config.standbyLcdIntervalN;
    case SET_PMS_ENABLE: return config.pmsEnabled;
    case SET_PM_WINDOW: return config.pmAverageWindow;
    case SET_OPERATING_SENSOR: return config.operatingSensorIntervalSeconds;
    case SET_STANDBY_SENSOR: return config.standbySensorIntervalSeconds;
    case SET_AUTO_STANDBY: return config.autoStandbyTimeoutMinutes;
  }
  return 0;
}

void setSettingValue(SettingId id, int value) {
  switch (id) {
    case SET_BACKLIGHT: config.backlightTimeoutSeconds = constrain(value, MIN_BACKLIGHT_TIMEOUT_SECONDS, MAX_BACKLIGHT_TIMEOUT_SECONDS); lastBacklightActivityMs = millis(); break;
    case SET_ROTATION: config.mainScreenRotationSeconds = constrain(value, MIN_MAIN_SCREEN_ROTATION_SECONDS, MAX_MAIN_SCREEN_ROTATION_SECONDS); lastMainRotationMs = millis(); break;
    case SET_STANDBY_LCD_N: config.standbyLcdIntervalN = constrain(value, MIN_STANDBY_LCD_INTERVAL_N, MAX_STANDBY_LCD_INTERVAL_N); lastStandbyLcdMs = millis(); break;
    case SET_PMS_ENABLE: {
      config.pmsEnabled = value ? 1 : 0;
      if (powerState == OPERATING) setPmsPower(config.pmsEnabled != 0);
      clearPmAverage();
      break;
    }
    case SET_PM_WINDOW: config.pmAverageWindow = constrain(value, MIN_PM_AVERAGE_WINDOW, MAX_PM_AVERAGE_WINDOW); clearPmAverage(); break;
    case SET_OPERATING_SENSOR: config.operatingSensorIntervalSeconds = constrain(value, MIN_OPERATING_SENSOR_INTERVAL_SECONDS, MAX_OPERATING_SENSOR_INTERVAL_SECONDS); lastOperatingSensorMs = millis(); break;
    case SET_STANDBY_SENSOR: config.standbySensorIntervalSeconds = constrain(value, MIN_STANDBY_SENSOR_INTERVAL_SECONDS, MAX_STANDBY_SENSOR_INTERVAL_SECONDS); lastStandbySensorMs = millis(); lastStandbyLcdMs = millis(); break;
    case SET_AUTO_STANDBY: config.autoStandbyTimeoutMinutes = constrain(value, MIN_AUTO_STANDBY_TIMEOUT_MINUTES, MAX_AUTO_STANDBY_TIMEOUT_MINUTES); outputsOffSinceMs = (fanSpeed == 0 && ledBrightness == 0) ? millis() : 0; break;
  }
}

int settingStep(SettingId id) { return id == SET_PMS_ENABLE ? 1 : 1; }
int settingMin(SettingId id) {
  switch (id) {
    case SET_BACKLIGHT: return MIN_BACKLIGHT_TIMEOUT_SECONDS;
    case SET_ROTATION: return MIN_MAIN_SCREEN_ROTATION_SECONDS;
    case SET_STANDBY_LCD_N: return MIN_STANDBY_LCD_INTERVAL_N;
    case SET_PMS_ENABLE: return 0;
    case SET_PM_WINDOW: return MIN_PM_AVERAGE_WINDOW;
    case SET_OPERATING_SENSOR: return MIN_OPERATING_SENSOR_INTERVAL_SECONDS;
    case SET_STANDBY_SENSOR: return MIN_STANDBY_SENSOR_INTERVAL_SECONDS;
    case SET_AUTO_STANDBY: return MIN_AUTO_STANDBY_TIMEOUT_MINUTES;
  }
  return 0;
}
int settingMax(SettingId id) {
  switch (id) {
    case SET_BACKLIGHT: return MAX_BACKLIGHT_TIMEOUT_SECONDS;
    case SET_ROTATION: return MAX_MAIN_SCREEN_ROTATION_SECONDS;
    case SET_STANDBY_LCD_N: return MAX_STANDBY_LCD_INTERVAL_N;
    case SET_PMS_ENABLE: return 1;
    case SET_PM_WINDOW: return MAX_PM_AVERAGE_WINDOW;
    case SET_OPERATING_SENSOR: return MAX_OPERATING_SENSOR_INTERVAL_SECONDS;
    case SET_STANDBY_SENSOR: return MAX_STANDBY_SENSOR_INTERVAL_SECONDS;
    case SET_AUTO_STANDBY: return MAX_AUTO_STANDBY_TIMEOUT_MINUTES;
  }
  return 100;
}

const __FlashStringHelper *settingName(SettingId id) {
  switch (id) {
    case SET_BACKLIGHT: return F("Backlight timeout");
    case SET_ROTATION: return F("Screen rotation");
    case SET_STANDBY_LCD_N: return F("Standby LCD mult.");
    case SET_PMS_ENABLE: return F("PMS7003 enabled");
    case SET_PM_WINDOW: return F("PM average window");
    case SET_OPERATING_SENSOR: return F("Operating sensor");
    case SET_STANDBY_SENSOR: return F("Standby sensor");
    case SET_AUTO_STANDBY: return F("Auto standby");
  }
  return F("Setting");
}

void beginConfigEdit() { configBeforeEdit = config; menuEditing = true; }
void cancelConfigEdit() {
  config = configBeforeEdit;
  menuEditing = false;
  if (powerState == OPERATING) setPmsPower(config.pmsEnabled != 0);
  clearPmAverage();
  lastOperatingSensorMs = millis();
  lastStandbySensorMs = millis();
  lastStandbyLcdMs = millis();
}
void confirmConfigEdit() { if (configValid(config)) saveConfig(); else config = configBeforeEdit; menuEditing = false; }

void beginDateEdit() {
  if (!rtcOK) return;
  DateTime n = rtc.now();
  editYear = n.year(); editMonth = n.month(); editDay = n.day();
  editHour = n.hour(); editMinute = n.minute(); editSecond = n.second();
  dateEditField = 0; dateEditing = true;
}

void changeDateField(int delta) {
  switch (dateEditField) {
    case 0: editYear = constrain(editYear + delta, 2000, 2099); break;
    case 1: editMonth = constrain(editMonth + delta, 1, 12); break;
    case 2: editDay = constrain(editDay + delta, 1, 31); break;
    case 3: editHour = (editHour + delta + 24) % 24; break;
    case 4: editMinute = (editMinute + delta + 60) % 60; break;
    case 5: editSecond = (editSecond + delta + 60) % 60; break;
  }
}

void drawDateEditor() {
  char b[21];
  tinyFormat_P(b, sizeof(b), PSTR("Set date/time %u/6"), (unsigned int)(dateEditField + 1)); lcdLine(0, b);
  tinyFormat_P(b, sizeof(b), PSTR("%04d-%02d-%02d"), editYear, editMonth, editDay); lcdLine(1, b);
  tinyFormat_P(b, sizeof(b), PSTR("%02d:%02d:%02d"), editHour, editMinute, editSecond); lcdLine(2, b);
  lcdLine(3, F("B1 Next  B2 Cancel"));
}

void drawMenu() {
  if (dateEditing) { drawDateEditor(); return; }
  char b[21];
  lcdLine(0, F("MAIN MENU"));
  lcd.setCursor(0, 1);
  lcd.print('>');
  const __FlashStringHelper *item = F("About");
  switch (menuIndex) {
    case 0: item = F("Date & Time"); break;
    case 1: item = F("Display Settings"); break;
    case 2: item = F("Sensor Settings"); break;
    case 3: item = F("Power Settings"); break;
    case 4: item = F("Fan Presets"); break;
    case 5: item = F("LED Presets"); break;
    case 6: item = F("Status"); break;
  }
  lcd.print(item);
  uint8_t itemLength = strlen_P((PGM_P)item);
  if (itemLength > 19) itemLength = 19;
  while (++itemLength < 20) lcd.print(' ');
  tinyFormat_P(b, sizeof(b), PSTR("Item %u of 8"), (unsigned int)(menuIndex + 1)); lcdLine(2, b);
  lcdLine(3, menuEditing ? F("Rotary edit B1 save") : F("B1 Select B2 Back"));
}

void drawDisplaySettings(uint8_t idx) {
  SettingId ids[3] = {SET_BACKLIGHT, SET_ROTATION, SET_STANDBY_LCD_N};
  SettingId id = ids[idx];
  char b[21];
  lcdLine(0, F("DISPLAY SETTINGS"));
  lcdLine(1, settingName(id));
  int v = getSettingValue(id);
  tinyFormat_P(b, sizeof(b), PSTR("%d %s"), v, id == SET_STANDBY_LCD_N ? "x" : "seconds"); lcdLine(2, b);
  lcdLine(3, menuEditing ? F("Rotary +/- B1 save") : F("B1 Edit B2 Back"));
}

void drawSensorSettings(uint8_t idx) {
  SettingId ids[4] = {SET_PMS_ENABLE, SET_PM_WINDOW, SET_OPERATING_SENSOR, SET_STANDBY_SENSOR};
  SettingId id = ids[idx]; char b[21];
  lcdLine(0, F("SENSOR SETTINGS")); lcdLine(1, settingName(id));
  if (id == SET_PMS_ENABLE) tinyFormat_P(b, sizeof(b), PSTR("%s"), config.pmsEnabled ? "ON" : "OFF");
  else if (id == SET_PM_WINDOW) tinyFormat_P(b, sizeof(b), PSTR("%u readings"), (unsigned int)config.pmAverageWindow);
  else tinyFormat_P(b, sizeof(b), PSTR("%d seconds"), getSettingValue(id));
  lcdLine(2, b); lcdLine(3, menuEditing ? F("Rotary +/- B1 save") : F("B1 Edit B2 Back"));
}

void drawPowerSettings() {
  char b[21]; lcdLine(0, F("POWER SETTINGS")); lcdLine(1, F("Auto standby timeout"));
  tinyFormat_P(b, sizeof(b), PSTR("%u minutes"), (unsigned int)config.autoStandbyTimeoutMinutes); lcdLine(2, b);
  lcdLine(3, menuEditing ? F("Rotary +/- B1 save") : F("B1 Edit B2 Back"));
}

void drawPresetEditor(bool fan, uint8_t idx) {
  char b[21];
  lcdLine(0, fan ? F("FAN PRESETS") : F("LED PRESETS"));
  tinyFormat_P(b, sizeof(b), PSTR("Preset %u of 6"), (unsigned int)(idx + 1)); lcdLine(1, b);
  tinyFormat_P(b, sizeof(b), PSTR("%u%%"), (unsigned int)(fan ? config.fanPresets[idx] : config.ledPresets[idx])); lcdLine(2, b);
  lcdLine(3, menuEditing ? F("Rotary +/- B1 save") : F("B1 Edit B2 Back"));
}

void drawStatusPage() {
  char b[21]; lcdLine(0, F("SYSTEM STATUS"));
  tinyFormat_P(b, sizeof(b), PSTR("RTC:%s EEPROM:%s"), rtcOK ? "OK" : "ERR", eepromOK ? "OK" : "ERR"); lcdLine(1, b);
  tinyFormat_P(b, sizeof(b), PSTR("AHT:%s BMP:%s"), ahtReadingValid ? "OK" : "ERR", bmpReadingValid ? "OK" : "ERR"); lcdLine(2, b);
  tinyFormat_P(b, sizeof(b), PSTR("PMS:%s %s"), !config.pmsEnabled ? "OFF" : (pmsError ? "ERR" : (pmValuesAvailable() ? "OK" : "WAIT")), powerState == STANDBY ? "STBY" : "RUN"); lcdLine(3, b);
}

void drawAboutPage() {
  lcdLine(0, F("DIY Air Purifier"));
  lcdLine(1, F("Firmware version"));
  lcdLine(2, F("1.0.0"));
  lcdLine(3, F("B2 Back"));
}

// Menu subpage state: 0 main, 1 display, 2 sensors, 3 power, 4 fan presets, 5 LED presets, 6 status, 7 about, 8 date.
uint8_t menuPage = 0;
uint8_t submenuIndex = 0;

void renderMenuPage() {
  if (menuPage == 0) drawMenu();
  else if (menuPage == 1) drawDisplaySettings(submenuIndex);
  else if (menuPage == 2) drawSensorSettings(submenuIndex);
  else if (menuPage == 3) drawPowerSettings();
  else if (menuPage == 4) drawPresetEditor(true, editingPresetIndex);
  else if (menuPage == 5) drawPresetEditor(false, editingPresetIndex);
  else if (menuPage == 6) drawStatusPage();
  else if (menuPage == 7) drawAboutPage();
}

void savePresetEdit() {
  uint8_t *arr = editingFanPreset ? config.fanPresets : config.ledPresets;
  // Keep values unique and ascending by swapping the edited value into order.
  uint8_t value = arr[editingPresetIndex];
  if (editingPresetIndex > 0 && value <= arr[editingPresetIndex - 1]) value = arr[editingPresetIndex - 1] + 1;
  if (editingPresetIndex < PRESET_COUNT - 1 && value >= arr[editingPresetIndex + 1]) value = arr[editingPresetIndex + 1] - 1;
  if (value > 100) value = 100;
  arr[editingPresetIndex] = value;
  // Sort ascending; zero remains the first preset as it cannot be edited away.
  for (uint8_t i = 0; i < PRESET_COUNT; ++i) for (uint8_t j = i + 1; j < PRESET_COUNT; ++j) if (arr[j] < arr[i]) { uint8_t t = arr[i]; arr[i] = arr[j]; arr[j] = t; }
  if (!presetsValid(arr)) { if (editingFanPreset) memcpy(arr, configBeforeEdit.fanPresets, PRESET_COUNT); else memcpy(arr, configBeforeEdit.ledPresets, PRESET_COUNT); }
}

void handleMenuEncoder(int delta) {
  if (!delta) return;
  if (dateEditing) { changeDateField(delta); return; }
  if (menuPage == 0) { menuIndex = (menuIndex + delta + MENU_ITEM_COUNT_ACTUAL) % MENU_ITEM_COUNT_ACTUAL; return; }
  if (menuPage == 1 && menuEditing) {
    SettingId ids[3] = {SET_BACKLIGHT, SET_ROTATION, SET_STANDBY_LCD_N};
    SettingId id = ids[submenuIndex]; setSettingValue(id, getSettingValue(id) + delta); return;
  }
  if (menuPage == 2 && menuEditing) {
    SettingId ids[4] = {SET_PMS_ENABLE, SET_PM_WINDOW, SET_OPERATING_SENSOR, SET_STANDBY_SENSOR};
    SettingId id = ids[submenuIndex];
    if (id == SET_PMS_ENABLE) setSettingValue(id, !config.pmsEnabled);
    else setSettingValue(id, getSettingValue(id) + delta);
    return;
  }
  if (menuPage == 3 && menuEditing) { setSettingValue(SET_AUTO_STANDBY, config.autoStandbyTimeoutMinutes + delta); return; }
  if ((menuPage == 4 || menuPage == 5) && menuEditing) {
    uint8_t *arr = menuPage == 4 ? config.fanPresets : config.ledPresets;
    int lo = editingPresetIndex == 0 ? 0 : arr[editingPresetIndex - 1] + 1;
    int hi = editingPresetIndex == PRESET_COUNT - 1 ? 100 : arr[editingPresetIndex + 1] - 1;
    arr[editingPresetIndex] = constrain((int)arr[editingPresetIndex] + delta, lo, hi);
  }
}

void enterSelectedMenuItem() {
  menuEditing = false; submenuIndex = 0; editingPresetIndex = 0;
  switch (menuIndex) {
    case 0: menuPage = 8; beginDateEdit(); break;
    case 1: menuPage = 1; break;
    case 2: menuPage = 2; break;
    case 3: menuPage = 3; break;
    case 4: menuPage = 4; editingFanPreset = true; break;
    case 5: menuPage = 5; editingFanPreset = false; break;
    case 6: menuPage = 6; break;
    case 7: menuPage = 7; break;
  }
  noteUserActivity();
}

void handleMenuButton1() {
  if (dateEditing) {
    if (dateEditField < 5) dateEditField++;
    else {
      if (validDate(editYear, editMonth, editDay, editHour, editMinute, editSecond) && rtcOK) {
        rtc.adjust(DateTime(editYear, editMonth, editDay, editHour, editMinute, editSecond));
      }
      dateEditing = false; menuPage = 0;
    }
    return;
  }
  if (menuPage == 0) { enterSelectedMenuItem(); return; }
  if (menuPage == 6 || menuPage == 7) return;
  if (!menuEditing) {
    beginConfigEdit();
    if (menuPage == 4 || menuPage == 5) { editingPresetIndex = submenuIndex; }
  } else {
    if (menuPage == 4 || menuPage == 5) {
      uint8_t *arr = menuPage == 4 ? config.fanPresets : config.ledPresets;
      if (editingPresetIndex == 0) arr[0] = 0;
      // Prevent duplicates and maintain ascending order by clamping to neighbours.
      int low = editingPresetIndex == 0 ? 0 : arr[editingPresetIndex - 1] + 1;
      int high = editingPresetIndex == PRESET_COUNT - 1 ? 100 : arr[editingPresetIndex + 1] - 1;
      arr[editingPresetIndex] = constrain(arr[editingPresetIndex], low, high);
    }
    confirmConfigEdit();
    outputsOffSinceMs = (fanSpeed == 0 && ledBrightness == 0) ? millis() : 0;
  }
}

void handleMenuButton2() {
  if (dateEditing) { dateEditing = false; menuPage = 0; return; }
  if (menuEditing) { cancelConfigEdit(); return; }
  if (menuPage != 0) { menuPage = 0; return; }
  enterMainDisplay();
}

void handleMenuButton3Short() { enterMainDisplay(); }

// ============================================================================
// 9. UI BUTTON ACTIONS AND POWER MANAGEMENT
// ============================================================================

void handleButton1Short() {
  noteUserActivity();
  if (uiState == MENU) { handleMenuButton1(); return; }
  if (uiState == MAIN_DISPLAY) uiState = FAN_ADJUSTMENT;
  else if (uiState == LED_ADJUSTMENT) uiState = FAN_ADJUSTMENT;
  else if (uiState == FAN_ADJUSTMENT) { fanSpeed = nextPresetAbove(config.fanPresets, fanSpeed); applyFanOutput(); }
  lastUiActivityMs = millis();
}

void handleButton2Short() {
  noteUserActivity();
  if (uiState == MENU) { handleMenuButton2(); return; }
  if (uiState == MAIN_DISPLAY) uiState = LED_ADJUSTMENT;
  else if (uiState == FAN_ADJUSTMENT) uiState = LED_ADJUSTMENT;
  else if (uiState == LED_ADJUSTMENT) { ledBrightness = nextPresetAbove(config.ledPresets, ledBrightness); applyLedOutput(); }
  lastUiActivityMs = millis();
}

void handleButton3Short() {
  noteUserActivity();
  if (uiState == MAIN_DISPLAY) { uiState = MENU; menuPage = 0; menuIndex = 0; menuEditing = false; }
  else if (uiState == FAN_ADJUSTMENT || uiState == LED_ADJUSTMENT) enterMainDisplay();
  else if (uiState == MENU) handleMenuButton3Short();
}

void processButtons() {
  bool p1, r1, p2, r2, p3, r3;
  updateButton(button1, p1, r1);
  updateButton(button2, p2, r2);
  updateButton(button3, p3, r3);
  uint32_t now = millis();

  // First interaction after backlight timeout only wakes the backlight.
  if (powerState == OPERATING && !lcdBacklightOn) {
    if (p1 || p2 || p3) {
      setBacklight(true); noteUserActivity();
      if (p3) button3WakeOnlyUntilRelease = true;
      // Button 1/2 wake-only too; suppress their corresponding edge.
      return;
    }
  }

  if (p1) handleButton1Short();
  if (p2) handleButton2Short();

  if (p3) { button3WakeOnlyUntilRelease = false; noteUserActivity(); }
  if (button3.stablePressed && !button3.longPressFired && !button3WakeOnlyUntilRelease && elapsed(now, button3.pressedAt, BUTTON3_LONG_PRESS_MS)) {
    button3.longPressFired = true;
    if (powerState == OPERATING) enterStandby(true);
  }
  if (r3) {
    if (button3WakeOnlyUntilRelease) { button3WakeOnlyUntilRelease = false; }
    else if (!button3.longPressFired && powerState == OPERATING) handleButton3Short();
  }

  // Long press must never also emit a short-press action.
  if (uiState == MENU && !menuEditing && button3.stablePressed && !button3.longPressFired) {
    // no additional action while held
  }
}

void processEncoder() {
  if (!encoderDetentsPending) return;
  int delta = encoderDetentsPending;
  encoderDetentsPending = 0;
  if (powerState == STANDBY) return;
  if (!lcdBacklightOn) { setBacklight(true); noteUserActivity(); return; }
  noteUserActivity();
  if (uiState == FAN_ADJUSTMENT || uiState == LED_ADJUSTMENT) {
    adjustActiveOutput(delta);
  } else if (uiState == MENU) {
    if (menuPage == 8) handleMenuEncoder(delta);
    else if (menuPage == 1 || menuPage == 2 || menuPage == 3 || menuPage == 4 || menuPage == 5) {
      if (!menuEditing && (menuPage == 1 || menuPage == 2)) {
        uint8_t maxItems = menuPage == 1 ? 3 : 4;
        submenuIndex = (submenuIndex + delta + maxItems) % maxItems;
      } else if (!menuEditing && (menuPage == 4 || menuPage == 5)) {
        editingPresetIndex = (editingPresetIndex + delta + PRESET_COUNT) % PRESET_COUNT;
      } else handleMenuEncoder(delta);
    }
  }
  if (uiState == FAN_ADJUSTMENT || uiState == LED_ADJUSTMENT) lastUiActivityMs = millis();
  if (uiState == MENU) lastMenuActivityMs = millis();
}

void checkAutomaticStandby(uint32_t now) {
  if (powerState != OPERATING || uiState == MENU) { if (uiState == MENU) outputsOffSinceMs = 0; return; }
  if (fanSpeed == 0 && ledBrightness == 0) {
    if (!outputsOffSinceMs) outputsOffSinceMs = now;
    uint32_t timeout = (uint32_t)config.autoStandbyTimeoutMinutes * 60000UL;
    if (elapsed(now, outputsOffSinceMs, timeout)) enterStandby(false);
  } else outputsOffSinceMs = 0;
}

void renderStandbyDisplay();

void checkTimers(uint32_t now) {
  if (powerState == STANDBY) {
    if (elapsed(now, lastStandbySensorMs, secondsToMs(config.standbySensorIntervalSeconds))) {
      lastStandbySensorMs = now;
      readEnvironmentalSensors();
    }
    uint32_t lcdInterval = secondsToMs((uint32_t)config.standbyLcdIntervalN * config.standbySensorIntervalSeconds);
    if (elapsed(now, lastStandbyLcdMs, lcdInterval)) { lastStandbyLcdMs = now; renderStandbyDisplay(); }
    return;
  }

  if (elapsed(now, lastOperatingSensorMs, secondsToMs(config.operatingSensorIntervalSeconds))) {
    lastOperatingSensorMs = now;
    readEnvironmentalSensors();
  }
  if (uiState == MAIN_DISPLAY && elapsed(now, lastMainRotationMs, secondsToMs(config.mainScreenRotationSeconds))) {
    lastMainRotationMs = now;
    mainDisplayPage = (mainDisplayPage + 1) % 2;
    renderDisplay();
  }
  if (lcdBacklightOn && elapsed(now, lastBacklightActivityMs, secondsToMs(config.backlightTimeoutSeconds))) setBacklight(false);
  if ((uiState == FAN_ADJUSTMENT || uiState == LED_ADJUSTMENT) && elapsed(now, lastUiActivityMs, UI_INACTIVITY_TIMEOUT_MS)) enterMainDisplay();
  if (uiState == MENU && elapsed(now, lastMenuActivityMs, MENU_INACTIVITY_TIMEOUT_MS)) {
    enterMainDisplay(); setBacklight(false);
    outputsOffSinceMs = (fanSpeed == 0 && ledBrightness == 0) ? now : 0;
  }
  checkAutomaticStandby(now);
}

// ============================================================================
// 10. LCD RENDERING
// ============================================================================

void renderMainEnvironmental() {
  char b[21], v[12];
  if (rtcOK) {
    DateTime n = rtc.now();
    tinyFormat_P(b, sizeof(b), PSTR("%02u/%02u/%04u %02u:%02u"), (unsigned int)n.day(), (unsigned int)n.month(), (unsigned int)n.year(), (unsigned int)n.hour(), (unsigned int)n.minute());
  } else strcpy(b, "RTC ERR");
  lcdLine(0, b);
  formatFloat(v, sizeof(v), temperatureC, 1);
  tinyFormat_P(b, sizeof(b), PSTR("Temp: %s C"), ahtReadingValid && !elapsed(millis(), lastAhtSuccessMs, SENSOR_STALE_TIMEOUT_MS) ? v : "ERR"); lcdLine(1, b);
  formatFloat(v, sizeof(v), humidityPercent, 1);
  tinyFormat_P(b, sizeof(b), PSTR("Humidity: %s%%"), ahtReadingValid && !elapsed(millis(), lastAhtSuccessMs, SENSOR_STALE_TIMEOUT_MS) ? v : "ERR"); lcdLine(2, b);
  formatFloat(v, sizeof(v), pressureHpa, 1);
  tinyFormat_P(b, sizeof(b), PSTR("Pressure: %s hPa"), bmpReadingValid && !elapsed(millis(), lastBmpSuccessMs, SENSOR_STALE_TIMEOUT_MS) ? v : "ERR"); lcdLine(3, b);
}

void renderMainParticles() {
  char b[21];
  if (rtcOK) {
    DateTime n = rtc.now();
    tinyFormat_P(b, sizeof(b), PSTR("%02u/%02u/%04u %02u:%02u"), (unsigned int)n.day(), (unsigned int)n.month(), (unsigned int)n.year(), (unsigned int)n.hour(), (unsigned int)n.minute());
  } else strcpy(b, "RTC ERR");
  lcdLine(0, b);
  const char *state = !config.pmsEnabled ? "OFF" : (pmsInGracePeriod() ? "--" : (pmsError ? "ERR" : "--"));
  if (pmValuesAvailable()) {
    tinyFormat_P(b, sizeof(b), PSTR("PM1.0: %5u ug/m3"), pm1); lcdLine(1, b);
    tinyFormat_P(b, sizeof(b), PSTR("PM2.5: %5u ug/m3"), pm25); lcdLine(2, b);
    tinyFormat_P(b, sizeof(b), PSTR("PM10 : %5u ug/m3"), pm10); lcdLine(3, b);
  } else {
    tinyFormat_P(b, sizeof(b), PSTR("PM1.0: %s"), state); lcdLine(1, b);
    tinyFormat_P(b, sizeof(b), PSTR("PM2.5: %s"), state); lcdLine(2, b);
    tinyFormat_P(b, sizeof(b), PSTR("PM10 : %s"), state); lcdLine(3, b);
  }
}

void renderAdjustment() {
  char b[21];
  if (uiState == FAN_ADJUSTMENT) {
    lcdLine(0, F("FAN SPEED")); tinyFormat_P(b, sizeof(b), PSTR("Speed: %u%%"), (unsigned int)fanSpeed); lcdLine(1, b);
    tinyFormat_P(b, sizeof(b), PSTR("Relay: %s"), fanRelayOn ? "ON" : "OFF"); lcdLine(2, b);
    lcdLine(3, F("Rotate adjust B1 preset"));
  } else {
    lcdLine(0, F("LED BRIGHTNESS")); tinyFormat_P(b, sizeof(b), PSTR("Brightness: %u%%"), (unsigned int)ledBrightness); lcdLine(1, b);
    lcdLine(2, F("0-100 percent")); lcdLine(3, F("Rotate adjust B2 preset"));
  }
}

void renderDisplay() {
  if (!lcdOK) return;
  if (powerState == STANDBY) { renderStandbyDisplay(); return; }
  if (uiState == MAIN_DISPLAY) {
    if (mainDisplayPage == 0) renderMainEnvironmental(); else renderMainParticles();
  } else if (uiState == FAN_ADJUSTMENT || uiState == LED_ADJUSTMENT) renderAdjustment();
  else if (uiState == MENU) renderMenuPage();
  lastDisplayRenderMs = millis();
}

void renderStandbyDisplay() {
  if (!lcdOK) return;
  char b[21], v[12];
  if (rtcOK) {
    DateTime n = rtc.now();
    tinyFormat_P(b, sizeof(b), PSTR("%02u/%02u/%04u %02u:%02u"), (unsigned int)n.day(), (unsigned int)n.month(), (unsigned int)n.year(), (unsigned int)n.hour(), (unsigned int)n.minute());
  } else strcpy(b, "RTC ERR");
  lcdLine(0, b);
  formatFloat(v, sizeof(v), temperatureC, 1);
  tinyFormat_P(b, sizeof(b), PSTR("Temp: %s C"), ahtReadingValid ? v : "ERR"); lcdLine(1, b);
  formatFloat(v, sizeof(v), humidityPercent, 1);
  tinyFormat_P(b, sizeof(b), PSTR("Humidity: %s%%"), ahtReadingValid ? v : "ERR"); lcdLine(2, b);
  formatFloat(v, sizeof(v), pressureHpa, 1);
  tinyFormat_P(b, sizeof(b), PSTR("Pressure: %s hPa"), bmpReadingValid ? v : "ERR"); lcdLine(3, b);
  // Deliberately do not call lcd.backlight() here.
}

// ============================================================================
// 11. STARTUP AND MAIN LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);

  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);

  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false);
  pinMode(PIN_PMS_EN, OUTPUT);
  digitalWrite(PIN_PMS_EN, PMS_ENABLE_ACTIVE_HIGH ? LOW : HIGH);

  setupFanPWM();
  setupLedPWM();
  setFanPWM(0); setLedPWM(0);

  previousEncoderState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
  initialiseButton(button1); initialiseButton(button2); initialiseButton(button3);

  Wire.begin();
  Wire.setClock(100000);
  lcd.init(); lcdOK = true; lcd.backlight();
  lcdLine(0, F("DIY Air Purifier")); lcdLine(1, F("Initialising..."));

  ahtOK = aht.begin();
  bmpOK = bmp.begin(BMP280_I2C_ADDRESS_1);
  if (!bmpOK) bmpOK = bmp.begin(BMP280_I2C_ADDRESS_2);
  rtcOK = rtc.begin();

  setDefaultConfig();
  if (!loadConfig()) {
    // A missing/invalid EEPROM configuration uses named defaults.
    saveConfig();
  }
  // Confirm the EEPROM responds using a one-byte read.
  uint8_t probe = 0;
  eepromOK = eepromReadBytes(0, &probe, 1);

  // Deliberately do NOT set the RTC to compilation time if it has lost power.
  // User sets date/time manually in the menu.
  if (rtcOK && rtc.lostPower()) Serial.println(F("RTC lost power; set date/time in menu."));

  fanSpeed = config.defaultFanSpeed;
  ledBrightness = config.defaultLedBrightness;
  applyOutputs();
  setPmsPower(config.pmsEnabled != 0);

  readEnvironmentalSensors();
  uint32_t now = millis();
  lastOperatingSensorMs = now;
  lastMainRotationMs = now;
  lastBacklightActivityMs = now;
  lastUiActivityMs = now;
  lastMenuActivityMs = now;
  outputsOffSinceMs = (fanSpeed == 0 && ledBrightness == 0) ? now : 0;

  lcdLine(0, F("Startup complete"));
  lcdLine(1, ahtOK ? F("AHT20: OK") : F("AHT20: ERR"));
  lcdLine(2, bmpOK ? F("BMP280: OK") : F("BMP280: ERR"));
  lcdLine(3, rtcOK ? F("DS3231: OK") : F("DS3231: ERR"));
  delay(900); // Short one-time startup notice only.
  renderDisplay();
}

void loop() {
  pollEncoder();
  pollPMS7003();
  processButtons();
  processEncoder();
  checkTimers(millis());

  // Refresh the current UI after interaction or updated data; avoids clearing the LCD continuously.
  static uint32_t lastUiRefresh = 0;
  if (elapsed(millis(), lastUiRefresh, 250)) {
    lastUiRefresh = millis();
    if (powerState == OPERATING) renderDisplay();
  }
}
