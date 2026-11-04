/*
  DIY AIR PURIFIER - Arduino Pro Micro / Leonardo (ATmega32U4, 5V/16MHz)
  Firmware for ATmega32U4; matching external EEPROM UI assets are required.
  Compact register-level sensor/RTC drivers and flash-resident UI strings.

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

static uint16_t scaleAht(uint32_t raw, bool temperature) {
  uint32_t scaled = raw * 125UL + (temperature ? 32768UL : 65536UL);
  return temperature ? scaled >> 16 : scaled >> 17;
}

class AHT20Lite {
  bool writeCommand(uint8_t a, uint8_t b, uint8_t c) {
    Wire.beginTransmission(0x38); Wire.write(a); Wire.write(b); Wire.write(c);
    return Wire.endTransmission() == 0;
  }
public:
  bool reset() {
    Wire.beginTransmission(0x38); Wire.write((uint8_t)0xBA);
    return Wire.endTransmission() == 0;
  }
  uint8_t status() {
    if (Wire.requestFrom((uint8_t)0x38, (uint8_t)1) != 1) return 0xFF;
    return Wire.read();
  }
  bool calibrate() { return writeCommand(0xBE, 0x08, 0x00); }
  bool startMeasurement() { return writeCommand(0xAC, 0x33, 0x00); }
  bool finishMeasurement(int16_t &temperature, int16_t &humidity) {
    uint8_t d[6];
    if (Wire.requestFrom((uint8_t)0x38, (uint8_t)6) != 6) return false;
    for (uint8_t i=0; i<6; ++i) d[i] = Wire.read();
    if ((d[0] & 0x88) != 0x08) return false;
    uint32_t rawH = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t rawT = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    humidity = scaleAht(rawH, false);
    temperature = (int16_t)scaleAht(rawT, true) - 500;
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
    uint8_t status; if (!readBytes(0xF3, &status, 1) || (status & 1)) return false;
    uint8_t c[24]; if (!readBytes(0x88, c, 24)) return false;
    dig_T1 = (uint16_t)((uint16_t)c[1]<<8 | c[0]); dig_T2 = (int16_t)((uint16_t)c[3]<<8 | c[2]); dig_T3 = (int16_t)((uint16_t)c[5]<<8 | c[4]);
    dig_P1 = (uint16_t)((uint16_t)c[7]<<8 | c[6]); dig_P2 = (int16_t)((uint16_t)c[9]<<8 | c[8]); dig_P3 = (int16_t)((uint16_t)c[11]<<8 | c[10]);
    dig_P4 = (int16_t)((uint16_t)c[13]<<8 | c[12]); dig_P5 = (int16_t)((uint16_t)c[15]<<8 | c[14]); dig_P6 = (int16_t)((uint16_t)c[17]<<8 | c[16]);
    dig_P7 = (int16_t)((uint16_t)c[19]<<8 | c[18]); dig_P8 = (int16_t)((uint16_t)c[21]<<8 | c[20]); dig_P9 = (int16_t)((uint16_t)c[23]<<8 | c[22]);
    if (!dig_P1) return false;
    // Adafruit_BMP280 default sampling/filter/standby profile.
    return writeReg(0xF4, 0x54) && writeReg(0xF5, 0x90) && writeReg(0xF4, 0x57) && configured();
  }
  bool configured() {
    uint8_t r[2];
    return readBytes(0xF4, r, 2) && r[0] == 0x57 && r[1] == 0x90;
  }
  bool read(int16_t &temperature, int16_t &pressureHpa) {
    if (!configured()) return false;
    uint8_t d[6]; if (!readBytes(0xF7, d, 6)) return false;
    int32_t adcP = ((int32_t)d[0]<<12) | ((int32_t)d[1]<<4) | (d[2]>>4);
    int32_t adcT = ((int32_t)d[3]<<12) | ((int32_t)d[4]<<4) | (d[5]>>4);
    if (adcP == 0x80000 || adcT == 0x80000) return false;
    int32_t v1 = ((((adcT >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
    int32_t v2 = (((((adcT >> 4) - (int32_t)dig_T1) * ((adcT >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
    tFine = v1 + v2;
    int32_t temp100 = (tFine * 5 + 128) >> 8;
    temperature = (temp100 + (temp100 >= 0 ? 5 : -5)) / 10;
    int64_t p1 = (int64_t)tFine - 128000;
    int64_t p2 = p1 * p1 * (int64_t)dig_P6;
    p2 += (p1 * (int64_t)dig_P5) * 131072LL;
    p2 += ((int64_t)dig_P4) * 34359738368LL;
    p1 = ((p1 * p1 * (int64_t)dig_P3) >> 8) + ((p1 * (int64_t)dig_P2) * 4096LL);
    p1 = (((((int64_t)1) << 47) + p1) * (int64_t)dig_P1) >> 33;
    if (!p1) return false;
    int64_t p = 1048576 - adcP;
    p = (((p << 31) - p2) * 3125) / p1;
    p1 = ((int64_t)dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    p2 = ((int64_t)dig_P8 * p) >> 19;
    p = ((p + p1 + p2) >> 8) + (((int64_t)dig_P7) * 16LL);
    pressureHpa = (int16_t)((p + 1280) / 2560);
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
const uint8_t DEFAULT_PRESETS[PRESET_COUNT] = {0, 20, 40, 60, 80, 100};

// About-page text

// ============================================================================
// 2. HARDWARE OBJECTS AND APPLICATION DATA
// ============================================================================

CachedLcd lcd(LCD_I2C_ADDRESS, 20, 4);
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
const uint16_t EEPROM_TIME_FORMAT_ADDRESS = 128;
const uint8_t EEPROM_TIME_FORMAT_MARKER = 0xA7;
bool use24HourClock = true;
bool clockFormatBeforeEdit = true;
bool clockFormatSaveFailed = false;
Config config;
uint8_t fanSpeed = DEFAULT_FAN_SPEED;
uint8_t ledBrightness = DEFAULT_LED_BRIGHTNESS;

// Current readings and freshness/error state
int16_t temperatureC = 0; // tenths of a degree C
int16_t humidityPercent = 0; // tenths of one percent
int16_t pressureHpa = 0; // tenths of one hPa
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
 enum UiState : uint8_t { MAIN_DISPLAY, FAN_ADJUSTMENT, LED_ADJUSTMENT, MENU, DELAY_EDIT };
PowerState powerState = OPERATING;
UiState uiState = MAIN_DISPLAY;
DelayOffTimer delayTimers[2]; // 0: fan, 1: LED; countdowns never persist.
uint8_t delaySelections[2] = {30, 30};
uint8_t delayEditTarget = 0;
uint8_t delayEditMinutes = 30;
bool delayOutputOffMessage = false;

bool lcdBacklightOn = true;
bool fanRelayOn = false;
bool pmsPowered = false;
uint8_t mainDisplayPage = 0;

// Timers (all millis()-based and rollover-safe)
uint32_t lastOperatingSensorMs = 0;
uint32_t lastStandbySensorMs = 0;
uint32_t lastStandbyLcdMs = 0;

// Power-down standby support. Timer0 stops in Power-down, so advance the
// Arduino millis counter by eight seconds on each watchdog wake.
extern volatile unsigned long timer0_millis;
volatile uint8_t standbyWatchdogFired = 0;
volatile uint8_t standbyButtonWakeFired = 0;
ISR(WDT_vect) { standbyWatchdogFired = 1; }
void standbyButtonWakeISR() { standbyButtonWakeFired = 1; }
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
const uint8_t MENU_ITEM_COUNT_ACTUAL = 9;

// PMS parser
uint8_t pmsFrame[32];
uint8_t pmsIndex = 0;

// ============================================================================
// 3. GENERAL HELPERS
// ============================================================================

bool elapsed(uint32_t now, uint32_t then, uint32_t interval) {
  return (uint32_t)(now - then) >= interval;
}

// Debounce and hold intervals are < 65536 ms; these checks run at least
// every watchdog tick (8 s), including standby. Long actions latch once fired.
bool elapsedShort(uint16_t now, uint16_t then, uint16_t interval) {
  return (uint16_t)(now - then) >= interval;
}

uint32_t secondsToMs(uint32_t seconds) { return seconds * 1000UL; }

void lcdLine(uint8_t row, const char *text) {
  if (!lcdOK) return;
  Wire.clearWireTimeoutFlag();
  lcd.setCursor(0, row);
  if (Wire.getWireTimeoutFlag()) { lcdOK = false; return; }
  for (uint8_t n = 0; n < 20; ++n) {
    char c = *text;
    if (c) ++text; else c = ' ';
    lcd.print(c);
    if (Wire.getWireTimeoutFlag()) { lcdOK = false; return; }
  }
}

// Small printf-like formatter for the handful of integer/string formats used
// by this UI. It deliberately avoids pulling the much larger avr-libc printf
// implementation into the firmware. Supported: %s, %u, %d, %% and widths
// such as %02u, %04d and %5u. No floating-point formatting is performed here.
void tinyFormat_P(char *out, uint8_t capacity, const char *fmt, ...) {
  if (!capacity) return;
  uint8_t pos = 0;
  va_list ap;
  va_start(ap, fmt);
  char ch = *fmt++;
  while (ch && pos < capacity - 1) {
    if (ch != '%') { out[pos++] = ch; ch = *fmt++; continue; }
    ch = *fmt++;
    if (ch == '%') { out[pos++] = '%'; ch = *fmt++; continue; }
    char pad = ' ';
    if (ch == '0') { pad = '0'; ch = *fmt++; }
    uint8_t width = 0;
    while (ch >= '0' && ch <= '9') {
      width = (uint8_t)(width * 10 + (ch - '0'));
      ch = *fmt++;
    }
    char spec = ch;
    ch = *fmt++;
    if (spec == 's') {
      const char *v = va_arg(ap, const char *);
      if (!v) v = "";
      while (*v && pos < capacity - 1) out[pos++] = *v++;
    } else if (spec == 'u' || spec == 'd') {
      // AVR int and unsigned int are both 16 bits. Reading the promoted
      // argument as unsigned preserves all uint16_t values (including PM).
      uint16_t value = va_arg(ap, unsigned int);
      char digits[6];
      uint8_t count = 0;
      do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value && count < sizeof(digits));
      uint8_t total = count;
      while (total < width && pos < capacity - 1) { out[pos++] = pad; ++total; }
      while (count && pos < capacity - 1) out[pos++] = digits[--count];
    }
  }
  out[pos] = '\0';
  va_end(ap);
}

// Compact fixed-point formatter. Avoids pulling avr-libc's relatively large
// dtostrf floating-point formatting routine into the 32U4 firmware.
void formatReading(char *out, uint8_t len, int16_t value) {
  uint16_t magnitude = value < 0 ? (uint16_t)(-(int32_t)value) : value;
  tinyFormat_P(out, len, uiText(0), value < 0 ? "-" : "", magnitude / 10, magnitude % 10);
}

bool validDate(int y, int m, int d, int hh, int mm, int ss) {
  if (y < 2000 || y > 2099 || m < 1 || m > 12 || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return false;
  static const uint8_t days[] PROGMEM = {31,28,31,30,31,30,31,31,30,31,30,31};
  int maxDay = pgm_read_byte(&days[m - 1]);
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
  if (percent > 100) percent = 100;
  uint16_t duty = ((uint16_t)percent * 640U) / 100U;
  OCR1B = (duty >= 640) ? 639 : duty;
}

void setLedPWM(uint8_t percent) {
  if (percent > 100) percent = 100;
  if (!percent) {
    // OCR=0 in Fast PWM still produces narrow pulses: disconnect OC3A.
    TCCR3A &= (uint8_t)~_BV(COM3A1);
    digitalWrite(PIN_LED, LOW);
  } else {
    OCR3A = ((uint16_t)percent * PWM_TOP) / 100U;
    TCCR3A |= _BV(COM3A1);
  }
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
  if (!fanSpeed) delayTimers[0].cancel();
  setFanPWM(fanSpeed);
  setRelay(fanSpeed > 0);
}

void applyLedOutput() { if (!ledBrightness) delayTimers[1].cancel(); setLedPWM(ledBrightness); }

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
  // AT24C32 page size is 32 bytes, but the AVR Wire TX buffer is only
  // 32 bytes TOTAL. The two EEPROM address bytes leave at most 30 data bytes.
  // Respect both limits and never cross a physical EEPROM page boundary.
  while (length) {
    uint8_t pageRemaining = 32 - (address % 32);
    uint8_t chunk = (length < pageRemaining) ? (uint8_t)length : pageRemaining;
    if (chunk > 30) chunk = 30;

    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    if (Wire.write((uint8_t)(address >> 8)) != 1) return false;
    if (Wire.write((uint8_t)(address & 0xFF)) != 1) return false;
    for (uint8_t i = 0; i < chunk; ++i) {
      if (Wire.write(data[i]) != 1) return false;
    }
    if (Wire.endTransmission() != 0) return false;

    // Allow the AT24C32 internal page-write cycle to complete.
    delay(6);
    address += chunk;
    data += chunk;
    length -= chunk;
  }
  return true;
}

bool eepromReadBytes(uint16_t address, uint8_t *data, uint16_t length) {
  while (length) {
    uint8_t chunk = (length > 32) ? 32 : length;
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

bool loadTimeFormat() {
  uint8_t values[2];
  bool valid = eepromReadBytes(EEPROM_TIME_FORMAT_ADDRESS, values, 2) &&
               values[0] == EEPROM_TIME_FORMAT_MARKER && values[1] <= 1;
  use24HourClock = !valid || values[1] == 1;
  return valid;
}

bool saveTimeFormat() {
  uint8_t values[2] = {EEPROM_TIME_FORMAT_MARKER, (uint8_t)(use24HourClock ? 1 : 0)};
  uint8_t verify[2];
  return eepromWriteBytes(EEPROM_TIME_FORMAT_ADDRESS, values, 2) &&
         eepromReadBytes(EEPROM_TIME_FORMAT_ADDRESS, verify, 2) &&
         memcmp(values, verify, 2) == 0;
}

void setDefaultConfig() {
  static const Config defaults PROGMEM = {
    DEFAULT_BACKLIGHT_TIMEOUT_SECONDS, DEFAULT_MAIN_SCREEN_ROTATION_SECONDS,
    DEFAULT_STANDBY_LCD_INTERVAL_N, DEFAULT_PMS7003_ENABLED, DEFAULT_PM_AVERAGE_WINDOW,
    DEFAULT_OPERATING_SENSOR_INTERVAL_SECONDS, DEFAULT_STANDBY_SENSOR_INTERVAL_SECONDS,
    DEFAULT_AUTO_STANDBY_TIMEOUT_MINUTES,
    {0,20,40,60,80,100}, {0,20,40,60,80,100}, DEFAULT_FAN_SPEED, DEFAULT_LED_BRIGHTNESS
  };
  memcpy_P(&config, &defaults, sizeof(config));
}

bool presetsValid(const uint8_t *p) {
  if (p[0] != 0) return false;
  for (uint8_t i = 0; i < PRESET_COUNT; ++i) {
    if (p[i] > 100) return false;
    if (i && p[i] <= p[i - 1]) return false;
  }
  return true;
}

struct SettingLimit { uint8_t location, minimum; uint16_t maximum; };
#define SETTING_WORD(member) (0x80 | offsetof(Config, member))
const SettingLimit settingLimits[8] PROGMEM = {
  {SETTING_WORD(backlightTimeoutSeconds), MIN_BACKLIGHT_TIMEOUT_SECONDS, MAX_BACKLIGHT_TIMEOUT_SECONDS},
  {SETTING_WORD(mainScreenRotationSeconds), MIN_MAIN_SCREEN_ROTATION_SECONDS, MAX_MAIN_SCREEN_ROTATION_SECONDS},
  {offsetof(Config, standbyLcdIntervalN), MIN_STANDBY_LCD_INTERVAL_N, MAX_STANDBY_LCD_INTERVAL_N},
  {offsetof(Config, pmsEnabled), 0, 1},
  {offsetof(Config, pmAverageWindow), MIN_PM_AVERAGE_WINDOW, MAX_PM_AVERAGE_WINDOW},
  {SETTING_WORD(operatingSensorIntervalSeconds), MIN_OPERATING_SENSOR_INTERVAL_SECONDS, MAX_OPERATING_SENSOR_INTERVAL_SECONDS},
  {SETTING_WORD(standbySensorIntervalSeconds), MIN_STANDBY_SENSOR_INTERVAL_SECONDS, MAX_STANDBY_SENSOR_INTERVAL_SECONDS},
  {SETTING_WORD(autoStandbyTimeoutMinutes), MIN_AUTO_STANDBY_TIMEOUT_MINUTES, MAX_AUTO_STANDBY_TIMEOUT_MINUTES}
};
#undef SETTING_WORD

int readSetting(const Config &c, SettingId id) {
  uint8_t location = pgm_read_byte(&settingLimits[id].location);
  const uint8_t *p = (const uint8_t *)&c + (location & 0x7F);
  return location & 0x80 ? *(const uint16_t *)p : *p;
}

int settingMin(SettingId id) { return pgm_read_byte(&settingLimits[id].minimum); }
int settingMax(SettingId id) { return pgm_read_word(&settingLimits[id].maximum); }

bool configValid(const Config &c) {
  for (uint8_t id = 0; id < 8; ++id) {
    int value = readSetting(c, (SettingId)id);
    if (value < settingMin((SettingId)id) || value > settingMax((SettingId)id)) return false;
  }
  if ((c.standbySensorIntervalSeconds % 8) != 0) return false;
  if (c.defaultFanSpeed > 100 || c.defaultLedBrightness > 100) return false;
  return presetsValid(c.fanPresets) && presetsValid(c.ledPresets);
}

bool loadConfig() {
  StoredConfig stored;
  if (!eepromReadBytes(0, (uint8_t *)&stored, sizeof(stored))) return false;
  if (stored.magic != EEPROM_MAGIC || stored.version != EEPROM_VERSION) return false;
  uint16_t expected = checksumBytes((const uint8_t *)&stored.config, sizeof(Config));
  if (stored.checksum != expected) return false;
  // Migrate older standby intervals to the nearest supported 8-second step.
  if (stored.config.standbySensorIntervalSeconds < MIN_STANDBY_SENSOR_INTERVAL_SECONDS)
    stored.config.standbySensorIntervalSeconds = MIN_STANDBY_SENSOR_INTERVAL_SECONDS;
  else if (stored.config.standbySensorIntervalSeconds > MAX_STANDBY_SENSOR_INTERVAL_SECONDS)
    stored.config.standbySensorIntervalSeconds = MAX_STANDBY_SENSOR_INTERVAL_SECONDS;
  else
    stored.config.standbySensorIntervalSeconds = ((stored.config.standbySensorIntervalSeconds + 4) / 8) * 8;
  if (!configValid(stored.config)) return false;
  config = stored.config;
  return true;
}

bool saveConfig() {
  StoredConfig stored;
  stored.magic = EEPROM_MAGIC;
  stored.version = EEPROM_VERSION;
  stored.config = config;
  stored.checksum = checksumBytes((const uint8_t *)&stored.config, sizeof(Config));

  if (!eepromWriteBytes(0, (const uint8_t *)&stored, sizeof(stored))) return false;

  // Read the record back so a failed/truncated I2C write cannot be mistaken
  // for a successful save and later silently replaced with defaults.
  StoredConfig verify;
  if (!eepromReadBytes(0, (uint8_t *)&verify, sizeof(verify))) return false;
  if (verify.magic != stored.magic || verify.version != stored.version) return false;
  if (verify.checksum != stored.checksum) return false;
  if (memcmp(&verify.config, &stored.config, sizeof(Config)) != 0) return false;
  return true;
}

bool uiAssetsOK = false;

bool validateUiAssets() {
  uint32_t magic; uint16_t count, expected;
  if (!eepromReadBytes(256, (uint8_t *)&magic, 4) || magic != 0x58545041UL ||
      !eepromReadBytes(260, (uint8_t *)&count, 2) || count != UI_TEXT_COUNT ||
      !eepromReadBytes(262, (uint8_t *)&expected, 2) || expected != UI_TEXT_CHECKSUM) return false;
  uint16_t checksum = 0xA55A;
  uint8_t block[32];
  for (uint16_t i = 0; i < UI_TEXT_COUNT; ++i) {
    if (!eepromReadBytes(264 + i * 32, block, 32)) return false;
    for (uint8_t j = 0; j < 32; ++j) checksum = (uint16_t)((checksum << 5) | (checksum >> 11)) ^ block[j];
  }
  return checksum == UI_TEXT_CHECKSUM;
}

uint8_t uiTextCachedId = 0xFF;
const char *uiText(uint8_t id) {
  static char text[32];
  if (id != uiTextCachedId) {
    uiTextCachedId = 0xFF;
    if (id >= UI_TEXT_COUNT || !eepromReadBytes(264 + (uint16_t)id * 32, (uint8_t *)text, 32)) strcpy(text, "TEXT ERR");
    else uiTextCachedId = id;
    text[31] = '\0';
  }
  return text;
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

uint32_t ahtMeasurementStartedMs = 0;
bool ahtMeasurementPending = false;
const uint32_t SENSOR_RETRY_MS = 10000UL;
uint32_t sensorAttemptMs[2] = {0, 0};
uint8_t ahtInitPhase = 0; // 0: retry interval, 1: reset wait, 2: calibration wait
bool bmpSettling = false;
uint8_t nextSensorRecovery = 0;
bool environmentalReadDue = false;

void sensorFailed(uint8_t target, uint32_t now) {
  sensorAttemptMs[target] = now;
  if (!target) {
    ahtOK = ahtReadingValid = ahtMeasurementPending = false;
    ahtInitPhase = 0;
  } else { bmpOK = bmpReadingValid = bmpSettling = false; }
}

void sensorRecovered(uint8_t target) {
  if (!target) { ahtOK = true; ahtInitPhase = 0; }
  else { bmpOK = true; bmpSettling = false; }
  // The normal scheduler obtains a fresh reading in the current power state.
  environmentalReadDue = true;
}

void pollSensorRecovery(uint32_t now) {
  uint8_t target = nextSensorRecovery;
  nextSensorRecovery ^= 1;
  if (!target) {
    if (ahtOK) return;
    if (!ahtInitPhase) {
      if (!elapsed(now, sensorAttemptMs[0], SENSOR_RETRY_MS)) return;
      sensorAttemptMs[0] = now;
      if (aht.reset()) ahtInitPhase = 1;
    } else {
      if (!elapsed(now, sensorAttemptMs[0], ahtInitPhase == 1 ? 20 : 10)) return;
      uint8_t status = aht.status();
      if (status & 0x80) { sensorFailed(0, now); return; }
      if (status & 0x08) sensorRecovered(0);
      else if (ahtInitPhase == 1 && aht.calibrate()) {
        ahtInitPhase = 2; sensorAttemptMs[0] = now;
      } else sensorFailed(0, now);
    }
  } else {
    if (bmpOK) return;
    if (bmpSettling) {
      if (elapsed(now, sensorAttemptMs[1], 100)) sensorRecovered(1);
    } else if (elapsed(now, sensorAttemptMs[1], SENSOR_RETRY_MS)) {
      sensorAttemptMs[1] = now;
      bmpSettling = bmp.begin(BMP280_I2C_ADDRESS_1) || bmp.begin(BMP280_I2C_ADDRESS_2);
    }
  }
}

void readEnvironmentalSensors() {
  uint32_t now = millis();
  if (ahtOK) {
    if (powerState == STANDBY) {
      // In Power-down standby the main loop sleeps between watchdog ticks.
      // Complete the AHT20 transaction while awake instead of leaving a
      // conversion pending across sleep cycles. This avoids stale/ERR readings
      // if the watchdog timing or millis bookkeeping differs from expectation.
      if (ahtMeasurementPending) {
        ahtMeasurementPending = false;
        int16_t pendingT, pendingH;
        if (aht.finishMeasurement(pendingT, pendingH)) {
          temperatureC = pendingT; humidityPercent = pendingH;
          ahtReadingValid = true; lastAhtSuccessMs = now;
        }
      }
      if (aht.startMeasurement()) {
        delay(80); // Only once per configured standby sensor refresh interval.
        int16_t t, h;
        if (aht.finishMeasurement(t, h)) {
          temperatureC = t; humidityPercent = h;
          ahtReadingValid = true; lastAhtSuccessMs = millis();
        } else sensorFailed(0, millis());
      } else sensorFailed(0, millis());
    } else if (!ahtMeasurementPending) {
      // Operating mode remains non-blocking so encoder polling stays responsive.
      if (aht.startMeasurement()) {
        ahtMeasurementStartedMs = now;
        ahtMeasurementPending = true;
      } else sensorFailed(0, now);
    }
  } else ahtReadingValid = false;

  if (bmpOK) {
    int16_t t, p;
    if (bmp.read(t, p) && p > 0) {
      pressureHpa = p;
      bmpReadingValid = true; lastBmpSuccessMs = now;
    } else sensorFailed(1, now);
  } else bmpReadingValid = false;
}

void pollAhtMeasurement(uint32_t now) {
  if (!ahtMeasurementPending || !elapsed(now, ahtMeasurementStartedMs, 80)) return;
  ahtMeasurementPending = false;
  int16_t t, h;
  if (aht.finishMeasurement(t, h)) {
    temperatureC = t; humidityPercent = h;
    ahtReadingValid = true; lastAhtSuccessMs = now;
  } else sensorFailed(0, now);
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
  if (raw != b.stablePressed && elapsedShort((uint16_t)now, b.rawChangedAt, BUTTON_DEBOUNCE_MS)) {
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
  if (menuEditing) cancelConfigEdit();
  uiState = MAIN_DISPLAY;
  menuEditing = false;
  dateEditing = false;
  lastMainRotationMs = millis();
  lastUiActivityMs = millis();
}

void renderStandbyDisplay();

void enterStandby(bool explicitRequest) {
  if (menuEditing) cancelConfigEdit();
  delayTimers[0].cancel(); delayTimers[1].cancel();
  if (explicitRequest) {
    config.defaultFanSpeed = fanSpeed;
    config.defaultLedBrightness = ledBrightness;
    bool defaultsSaved = saveConfig();
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
  // Draw the complete standby layout immediately. Do not write only row 0:
  // that leaves the previous operating-screen contents visible on rows 1-3.
  renderStandbyDisplay();
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

void beginDelayEdit(uint8_t target) {
  delayEditTarget = target;
  delayEditMinutes = delaySelections[target];
  delayOutputOffMessage = false;
  uiState = DELAY_EDIT;
  noteUserActivity();
}

void handleDelayButton(uint8_t target) {
  if (target != delayEditTarget) { enterMainDisplay(); return; }
  if (delayEditMinutes && !(target == 0 ? fanSpeed : ledBrightness)) {
    delayOutputOffMessage = true;
    return;
  }
  delaySelections[target] = delayEditMinutes;
  delayTimers[target].set(millis(), delayEditMinutes);
  enterMainDisplay();
}

void checkDelayTimers(uint32_t now) {
  if (powerState != OPERATING) return;
  for (uint8_t target = 0; target < 2; ++target) {
    if (!delayTimers[target].expired(now)) continue;
    delayTimers[target].cancel();
    if (target == 0) { fanSpeed = 0; applyFanOutput(); }
    else { ledBrightness = 0; applyLedOutput(); }
  }
}

uint8_t nextPresetAbove(const uint8_t *presets, uint8_t current) {
  for (uint8_t i = 0; i < PRESET_COUNT; ++i) if (presets[i] > current) return presets[i];
  return presets[0];
}

// ============================================================================
// 8. MENU CONFIGURATION EDITING
// ============================================================================

int getSettingValue(SettingId id) { return readSetting(config, id); }

void setSettingValue(SettingId id, int value) {
  value = constrain(value, settingMin(id), settingMax(id));
  if (id == SET_STANDBY_SENSOR) value = (value / 8) * 8;
  uint8_t location = pgm_read_byte(&settingLimits[id].location);
  uint8_t *p = (uint8_t *)&config + (location & 0x7F);
  if (location & 0x80) *(uint16_t *)p = value; else *p = value;
  uint32_t now = millis();
  switch (id) {
    case SET_BACKLIGHT: lastBacklightActivityMs = now; break;
    case SET_ROTATION: lastMainRotationMs = now; break;
    case SET_STANDBY_LCD_N: lastStandbyLcdMs = now; break;
    case SET_PMS_ENABLE:
      if (powerState == OPERATING) setPmsPower(config.pmsEnabled != 0);
      clearPmAverage(); break;
    case SET_PM_WINDOW: clearPmAverage(); break;
    case SET_OPERATING_SENSOR: lastOperatingSensorMs = now; break;
    case SET_STANDBY_SENSOR: lastStandbySensorMs = lastStandbyLcdMs = now; break;
    case SET_AUTO_STANDBY: outputsOffSinceMs = (fanSpeed == 0 && ledBrightness == 0) ? now : 0; break;
  }
}

int settingStep(SettingId id) { return id == SET_STANDBY_SENSOR ? 8 : 1; }
const char *settingName(SettingId id) { return uiText(id < 8 ? 1 + id : 9); }

void beginConfigEdit() { configBeforeEdit = config; clockFormatBeforeEdit = use24HourClock; clockFormatSaveFailed = false; menuEditing = true; }
void cancelConfigEdit() {
  config = configBeforeEdit;
  use24HourClock = clockFormatBeforeEdit;
  clockFormatSaveFailed = false;
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
  if (!rtcOK || !dateEditing) {
    lcdLine(0, uiText(10));
    lcdLine(1, uiText(11));
    lcdLine(2, uiText(12));
    lcdLine(3, uiText(13));
    return;
  }
  char b[21];
  lcdLine(0, uiText(10));
  lcdLine(1, uiText(14 + dateEditField));
  tinyFormat_P(b, sizeof(b), uiText(20),
               editYear, editMonth, editDay, editHour, editMinute, editSecond);
  lcdLine(2, b);
  if (dateEditField == 5) lcdLine(3, uiText(21));
  else lcdLine(3, uiText(22));
}

void drawMenu() {
  if (dateEditing) { drawDateEditor(); return; }
  char b[21];
  lcdLine(0, uiText(23));
  lcd.setCursor(0, 1);
  lcd.print("> ");
  const char *item = uiText(menuIndex < 8 ? 25 + menuIndex : 24);
  lcd.print(item);
  uint8_t itemLength = strlen(item);
  if (itemLength > 19) itemLength = 19;
  while (itemLength + 2 < 20) { lcd.print(' '); ++itemLength; }
  tinyFormat_P(b, sizeof(b), uiText(33), (unsigned int)(menuIndex + 1), (unsigned int)MENU_ITEM_COUNT_ACTUAL);
  lcdLine(2, uiText(34));
  lcd.setCursor(20 - strlen(b), 2); lcd.print(b);
  lcdLine(3, uiText(35));
}

void drawSettingValue(const char *value) {
  lcdLine(2, menuEditing ? ">>> " : "  > ");
  lcd.setCursor(4, 2); lcd.print(value);
  lcdLine(3, clockFormatSaveFailed ? "ERR" : (menuEditing ? uiText(43) : uiText(44)));
}

void drawDisplaySettings(uint8_t idx) {
  char b[21];
  lcdLine(0, uiText(36));
  if (idx < 3) {
    SettingId id = (SettingId)idx;
    lcdLine(1, settingName(id));
    int v = getSettingValue(id);
    tinyFormat_P(b, sizeof(b), uiText(37), v, id == SET_STANDBY_LCD_N ? "x" : "seconds");
    drawSettingValue(b);
  } else {
    lcdLine(1, uiText(38));
    lcdLine(2, menuEditing ? (use24HourClock ? uiText(39) : uiText(40)) : (use24HourClock ? uiText(41) : uiText(42)));
  }
  if (idx == 3) lcdLine(3, clockFormatSaveFailed ? "ERR" : (menuEditing ? uiText(43) : uiText(44)));
}

void drawSensorSettings(uint8_t idx) {
  SettingId id = (SettingId)(SET_PMS_ENABLE + idx); char b[21];
  lcdLine(0, uiText(45)); lcdLine(1, settingName(id));
  if (id == SET_PMS_ENABLE) tinyFormat_P(b, sizeof(b), uiText(46), config.pmsEnabled ? "ON" : "OFF");
  else if (id == SET_PM_WINDOW) tinyFormat_P(b, sizeof(b), uiText(47), (unsigned int)config.pmAverageWindow);
  else tinyFormat_P(b, sizeof(b), uiText(48), getSettingValue(id));
  drawSettingValue(b);
}

void drawPowerSettings() {
  char b[21]; lcdLine(0, uiText(49)); lcdLine(1, uiText(50));
  tinyFormat_P(b, sizeof(b), uiText(51), (unsigned int)config.autoStandbyTimeoutMinutes);
  drawSettingValue(b);
}

void drawPresetEditor(bool fan, uint8_t idx) {
  char b[21];
  lcdLine(0, fan ? uiText(52) : uiText(53));
  tinyFormat_P(b, sizeof(b), uiText(54), (unsigned int)(idx + 1), (unsigned int)PRESET_COUNT); lcdLine(1, b);
  tinyFormat_P(b, sizeof(b), uiText(55), (unsigned int)(fan ? config.fanPresets[idx] : config.ledPresets[idx]));
  drawSettingValue(b);
}

void drawStatusPage() {
  char b[21]; lcdLine(0, uiText(56));
  tinyFormat_P(b, sizeof(b), uiText(57), rtcOK ? "OK" : "ERR", eepromOK ? "OK" : "ERR"); lcdLine(1, b);
  tinyFormat_P(b, sizeof(b), uiText(58), ahtReadingValid ? "OK" : "ERR", bmpReadingValid ? "OK" : "ERR"); lcdLine(2, b);
  tinyFormat_P(b, sizeof(b), uiText(59), !config.pmsEnabled ? "OFF" : (pmsError ? "ERR" : (pmValuesAvailable() ? "OK" : "WAIT")), powerState == STANDBY ? "STBY" : "RUN"); lcdLine(3, b);
}

void drawAboutPage() {
  lcdLine(0, uiText(60));
  lcdLine(1, uiText(61));
  lcdLine(2, uiText(62));
  lcdLine(3, uiText(13));
}

void drawFactoryResetPage() {
  lcdLine(0, uiText(63));
  lcdLine(1, uiText(64));
  lcdLine(2, uiText(34));
  lcdLine(3, uiText(65));
}

// Menu subpage state: 0 main, 1 display, 2 sensors, 3 power, 4 fan presets, 5 LED presets, 6 status, 7 about, 8 date.
uint8_t menuPage = 0;
uint8_t submenuIndex = 0;

void renderMenuPage() {
  if (menuPage == 0) drawMenu();
  else if (menuPage == 8) drawDateEditor();
  else if (menuPage == 9) drawFactoryResetPage();
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
  if (menuPage == 1 && menuEditing && submenuIndex == 3) { use24HourClock = !use24HourClock; return; }
  if (menuPage == 1 && menuEditing) {
    SettingId id = (SettingId)submenuIndex; setSettingValue(id, getSettingValue(id) + delta); return;
  }
  if (menuPage == 2 && menuEditing) {
    SettingId id = (SettingId)(SET_PMS_ENABLE + submenuIndex);
    if (id == SET_PMS_ENABLE) setSettingValue(id, !config.pmsEnabled);
    else setSettingValue(id, getSettingValue(id) + delta * settingStep(id));
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
    case 8: menuPage = 9; break;
  }
  noteUserActivity();
}

void handleMenuButton1() {
  if (menuPage == 8 && !dateEditing) return;
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
  if (menuPage == 9) {
    setDefaultConfig();
    use24HourClock = true;
    saveConfig();
    saveTimeFormat();
    fanSpeed = config.defaultFanSpeed;
    ledBrightness = config.defaultLedBrightness;
    applyOutputs();
    setPmsPower(config.pmsEnabled != 0);
    clearPmAverage();
    menuPage = 0; menuIndex = 0; menuEditing = false;
    return;
  }
  if (!menuEditing) {
    // Keep editingPresetIndex: the rotary encoder selects which preset is edited.
    beginConfigEdit();
  } else {
    if (menuPage == 4 || menuPage == 5) {
      uint8_t *arr = menuPage == 4 ? config.fanPresets : config.ledPresets;
      if (editingPresetIndex == 0) arr[0] = 0;
      // Prevent duplicates and maintain ascending order by clamping to neighbours.
      int low = editingPresetIndex == 0 ? 0 : arr[editingPresetIndex - 1] + 1;
      int high = editingPresetIndex == PRESET_COUNT - 1 ? 100 : arr[editingPresetIndex + 1] - 1;
      arr[editingPresetIndex] = constrain(arr[editingPresetIndex], low, high);
    }
    if (menuPage == 1 && submenuIndex == 3) {
      if (!saveTimeFormat()) { clockFormatSaveFailed = true; return; }
      clockFormatSaveFailed = false;
      menuEditing = false;
    } else confirmConfigEdit();
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

void handleOutputButtonShort(uint8_t target) {
  noteUserActivity();
  if (uiState == DELAY_EDIT) { handleDelayButton(target); return; }
  if (uiState == MENU) {
    if (!target) handleMenuButton1(); else handleMenuButton2();
    return;
  }
  UiState adjustment = target ? LED_ADJUSTMENT : FAN_ADJUSTMENT;
  if (uiState == adjustment) {
    uint8_t &value = target ? ledBrightness : fanSpeed;
    value = nextPresetAbove(target ? config.ledPresets : config.fanPresets, value);
    applyOutputs();
  } else uiState = adjustment;
}

void handleButton1Short() { handleOutputButtonShort(0); }
void handleButton2Short() { handleOutputButtonShort(1); }

void handleButton3Short() {
  noteUserActivity();
  if (uiState == MAIN_DISPLAY) { uiState = MENU; menuPage = 0; menuIndex = 0; menuEditing = false; }
  else if (uiState == FAN_ADJUSTMENT || uiState == LED_ADJUSTMENT || uiState == DELAY_EDIT) enterMainDisplay();
  else if (uiState == MENU) handleMenuButton3Short();
}

void processButtons() {
  bool p1, r1, p2, r2, p3, r3;
  updateButton(button1, p1, r1);
  updateButton(button2, p2, r2);
  updateButton(button3, p3, r3);
  uint32_t now = millis();

  // Standby accepts only Button 3. Button 1, Button 2 and the encoder
  // must not change outputs while the purifier is in standby.
  if (powerState == STANDBY) {
    if (p3) {
      wakeFromStandby();
      button1.longPressFired = true; button2.longPressFired = true;
      button3WakeOnlyUntilRelease = true;
    }
    return;
  }

  // First interaction after backlight timeout only wakes the backlight.
  if (powerState == OPERATING && !lcdBacklightOn) {
    if (p1 || p2 || p3) {
      setBacklight(true); noteUserActivity();
      if (p3) button3WakeOnlyUntilRelease = true;
      if (p1) button1.longPressFired = true;
      if (p2) button2.longPressFired = true;
      // Button 1/2 wake-only too; suppress their corresponding edge.
      return;
    }
  }

  // Short actions occur on release, so a hold cannot also cycle a preset.
  if (p1 || p2) noteUserActivity();
  if (uiState != MENU) {
    for (uint8_t target = 0; target < 2; ++target) {
      ButtonState &button = target ? button2 : button1;
      if (button.stablePressed && !button.longPressFired && elapsedShort((uint16_t)now, button.pressedAt, BUTTON3_LONG_PRESS_MS)) {
        button.longPressFired = true;
        beginDelayEdit(target);
      }
    }
  }
  if (r1 && !button1.longPressFired) handleButton1Short();
  if (r2 && !button2.longPressFired) handleButton2Short();

  if (p3) { button3WakeOnlyUntilRelease = false; noteUserActivity(); }
  if (button3.stablePressed && !button3.longPressFired && !button3WakeOnlyUntilRelease && elapsedShort((uint16_t)now, button3.pressedAt, BUTTON3_LONG_PRESS_MS)) {
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
  if (uiState == DELAY_EDIT) {
    delayEditMinutes = stepDelayMinutes(delayEditMinutes, delta);
    delayOutputOffMessage = false;
  } else if (uiState == FAN_ADJUSTMENT || uiState == LED_ADJUSTMENT) {
    adjustActiveOutput(delta);
  } else if (uiState == MENU) {
    if (menuPage == 0 || menuPage == 8) handleMenuEncoder(delta);
    else if (menuPage == 1 || menuPage == 2 || menuPage == 3 || menuPage == 4 || menuPage == 5) {
      if (!menuEditing && (menuPage == 1 || menuPage == 2)) {
        uint8_t maxItems = menuPage == 1 ? 4 : 4;
        submenuIndex = (submenuIndex + delta + maxItems) % maxItems;
      } else if (!menuEditing && (menuPage == 4 || menuPage == 5)) {
        editingPresetIndex = (editingPresetIndex + delta + PRESET_COUNT) % PRESET_COUNT;
      } else handleMenuEncoder(delta);
    }
  }
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
    if (environmentalReadDue || elapsed(now, lastStandbySensorMs, secondsToMs(config.standbySensorIntervalSeconds))) {
      environmentalReadDue = false;
      lastStandbySensorMs = now;
      readEnvironmentalSensors();
    }
    uint32_t lcdInterval = secondsToMs((uint32_t)config.standbyLcdIntervalN * config.standbySensorIntervalSeconds);
    if (elapsed(now, lastStandbyLcdMs, lcdInterval)) { lastStandbyLcdMs = now; renderStandbyDisplay(); }
    return;
  }

  if (environmentalReadDue || elapsed(now, lastOperatingSensorMs, secondsToMs(config.operatingSensorIntervalSeconds))) {
    environmentalReadDue = false;
    lastOperatingSensorMs = now;
    readEnvironmentalSensors();
  }
  if (uiState == MAIN_DISPLAY && elapsed(now, lastMainRotationMs, secondsToMs(config.mainScreenRotationSeconds))) {
    lastMainRotationMs = now;
    mainDisplayPage = (mainDisplayPage + 1) % 2;
    renderDisplay();
  }
  if (lcdBacklightOn && elapsed(now, lastBacklightActivityMs, secondsToMs(config.backlightTimeoutSeconds))) setBacklight(false);
  if ((uiState == FAN_ADJUSTMENT || uiState == LED_ADJUSTMENT || uiState == DELAY_EDIT) && elapsed(now, lastUiActivityMs, UI_INACTIVITY_TIMEOUT_MS)) enterMainDisplay();
  if (uiState == MENU && elapsed(now, lastMenuActivityMs, MENU_INACTIVITY_TIMEOUT_MS)) {
    enterMainDisplay(); setBacklight(false);
    outputsOffSinceMs = (fanSpeed == 0 && ledBrightness == 0) ? now : 0;
  }
  checkDelayTimers(now);
  checkAutomaticStandby(now);
}

// ============================================================================
// 10. LCD RENDERING
// ============================================================================

void formatClockLine(char *out, size_t outSize, uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
  char timePart[10];
  if (use24HourClock) {
    tinyFormat_P(timePart, sizeof(timePart), uiText(66), (unsigned int)hour, (unsigned int)minute);
  } else {
    uint8_t h = hour % 12;
    if (!h) h = 12;
    tinyFormat_P(timePart, sizeof(timePart), uiText(67), (unsigned int)h, (unsigned int)minute, hour >= 12 ? "PM" : "AM");
  }
  tinyFormat_P(out, outSize, uiText(68), (unsigned int)year, (unsigned int)month, (unsigned int)day);
  // DS3231 years are 2000..2199, so the date is exactly ten characters.
  // Every production caller supplies a 21-byte LCD row buffer.
  if (outSize < 21) return;
  uint8_t timeLen = strlen(timePart), position = 20 - timeLen;
  memset(out + 10, ' ', position - 10);
  memcpy(out + position, timePart, timeLen + 1);
}

void renderClock() {
  char b[21];
  if (rtcOK) { DateTime n = rtc.now(); formatClockLine(b, sizeof(b), n.year(), n.month(), n.day(), n.hour(), n.minute()); }
  else strcpy(b, "RTC ERR");
  lcdLine(0, b);
}

void renderMainEnvironmental() {
  char b[21], v[12], t[8], h[8];
  renderClock();
  formatReading(v, sizeof(v), temperatureC);
  strcpy(t, ahtReadingValid && !elapsed(millis(), lastAhtSuccessMs, SENSOR_STALE_TIMEOUT_MS) ? v : "ERR");
  formatReading(v, sizeof(v), humidityPercent);
  strcpy(h, ahtReadingValid && !elapsed(millis(), lastAhtSuccessMs, SENSOR_STALE_TIMEOUT_MS) ? v : "ERR");
  lcdLine(1, uiText(69));
  tinyFormat_P(b, sizeof(b), uiText(70), t, h); lcdLine(2, b);
  formatReading(v, sizeof(v), pressureHpa);
  tinyFormat_P(b, sizeof(b), uiText(71), bmpReadingValid && !elapsed(millis(), lastBmpSuccessMs, SENSOR_STALE_TIMEOUT_MS) ? v : "ERR"); lcdLine(3, b);
}

void renderMainParticles() {
  char b[21];
  renderClock();
  const char *state = !config.pmsEnabled ? "OFF" : (pmsInGracePeriod() ? "--" : (pmsError ? "ERR" : "--"));
  bool available = pmValuesAvailable();
  uint16_t values[3] = {pm1, pm25, pm10};
  for (uint8_t i = 0; i < 3; ++i) {
    if (available) tinyFormat_P(b, sizeof(b), uiText(72 + i), values[i]);
    else tinyFormat_P(b, sizeof(b), uiText(75 + i), state);
    lcdLine(i + 1, b);
  }
}

void renderAdjustment() {
  bool fan = uiState == FAN_ADJUSTMENT;
  uint8_t target = fan ? 0 : 1;
  char b[21];
  lcdLine(0, fan ? uiText(78) : uiText(79));
  lcdLine(1, fan ? uiText(80) : uiText(81));
  tinyFormat_P(b, sizeof(b), uiText(55), (unsigned int)(fan ? fanSpeed : ledBrightness));
  lcd.setCursor(20 - strlen(b), 1); lcd.print(b);
  if (delayTimers[target].active) {
    tinyFormat_P(b, sizeof(b), uiText(82), (unsigned int)delayTimers[target].remainingMinutes(millis()));
    lcdLine(2, b);
  } else lcdLine(2, uiText(83));
  lcdLine(3, fan ? uiText(84) : uiText(85));
}

void renderDelayEditor() {
  char b[21];
  lcdLine(0, delayEditTarget == 0 ? uiText(86) : uiText(87));
  if (delayEditMinutes) tinyFormat_P(b, sizeof(b), uiText(88), (unsigned int)delayEditMinutes);
  else strcpy(b, uiText(89));
  lcdLine(1, b);
  lcdLine(2, delayOutputOffMessage ? uiText(90) : uiText(91));
  lcdLine(3, delayEditTarget == 0 ? uiText(92) : uiText(93));
}

void renderDisplay() {
  if (!lcdOK) return;
  if (powerState == STANDBY) { renderStandbyDisplay(); return; }
  if (uiState == MAIN_DISPLAY) {
    if (mainDisplayPage == 0) renderMainEnvironmental(); else renderMainParticles();
  } else if (uiState == FAN_ADJUSTMENT || uiState == LED_ADJUSTMENT) renderAdjustment();
  else if (uiState == DELAY_EDIT) renderDelayEditor();
  else if (uiState == MENU) renderMenuPage();
  lastDisplayRenderMs = millis();
}

void renderStandbyDisplay() {
  if (!lcdOK) return;
  // Reuse the environmental renderer so standby and home layouts stay identical.
  // Replace only the pressure row with the standby message.
  renderMainEnvironmental();
  lcdLine(3, uiText(94));
  // Deliberately do not call lcd.backlight() here.
}

// ============================================================================
// 11. STARTUP AND MAIN LOOP
// ============================================================================

void setup() {
  wdt_disable();
  Serial1.begin(9600);

  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);

  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? HIGH : LOW);
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
  Wire.setWireTimeout(25000UL, true);
  lcd.init(); lcdOK = true; lcd.backlight();
  uiAssetsOK = validateUiAssets();
  if (!uiAssetsOK) {
    lcdLine(0, "UI EEPROM error");
    lcdLine(1, "Load UI programmer");
    return; // Outputs were set OFF before the bus was initialised.
  }
  lcdLine(0, uiText(60)); lcdLine(1, uiText(95));

  sensorAttemptMs[0] = sensorAttemptMs[1] = millis() - SENSOR_RETRY_MS;
  pollSensorRecovery(millis()); pollSensorRecovery(millis());
  rtcOK = rtc.begin();

  setDefaultConfig();
  if (!loadConfig()) {
    // A missing/invalid EEPROM configuration uses named defaults.
    saveConfig();
  }
  if (!loadTimeFormat()) saveTimeFormat();
  // Confirm the EEPROM responds using a one-byte read.
  uint8_t probe = 0;
  eepromOK = eepromReadBytes(0, &probe, 1);

  // Deliberately do NOT set the RTC to compilation time if it has lost power.
  // User sets date/time manually in the menu.

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

  lcdLine(0, uiText(96));
  lcdLine(1, ahtOK ? uiText(97) : uiText(98));
  lcdLine(2, bmpOK ? uiText(99) : uiText(100));
  lcdLine(3, rtcOK ? uiText(101) : uiText(102));
  delay(900); // Short one-time startup notice only.
  renderDisplay();
}

// Sleep for one 8-second watchdog interval while in standby. The watchdog
// wakes the MCU periodically so configured sensor/LCD intervals continue.
// Button 3 (D7 / INT6 on the target ATmega32U4 board) can wake it immediately.
void standbyPowerDownOnce() {
  if (powerState != STANDBY) return;
  // Do not re-enter sleep while the wake button is still held; this gives the
  // normal debounce logic time to recognise the press and restore operation.
  if (digitalRead(PIN_BUTTON3) == LOW) return;

  uint8_t adcWasEnabled = ADCSRA & _BV(ADEN);
  ADCSRA &= (uint8_t)~_BV(ADEN);
  standbyWatchdogFired = 0;
  standbyButtonWakeFired = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON3), standbyButtonWakeISR, LOW);

  cli();
  wdt_reset();
  MCUSR &= (uint8_t)~_BV(WDRF);
  WDTCSR = _BV(WDCE) | _BV(WDE);
  // ATmega32U4 watchdog interrupt, nominal 8-second period.
  WDTCSR = _BV(WDIE) | _BV(WDP3) | _BV(WDP0);

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  sleep_enable();
#if defined(BODS) && defined(BODSE)
  sleep_bod_disable();
#endif
  sei();
  sleep_cpu();

  // Execution resumes here after either watchdog or Button 3 interrupt.
  sleep_disable();
  cli();
  wdt_reset();
  MCUSR &= (uint8_t)~_BV(WDRF);
  WDTCSR = _BV(WDCE) | _BV(WDE);
  WDTCSR = 0; // Disable watchdog until the next standby sleep interval.
  if (standbyWatchdogFired) timer0_millis += 8000UL;
  sei();

  detachInterrupt(digitalPinToInterrupt(PIN_BUTTON3));
  if (adcWasEnabled) ADCSRA |= _BV(ADEN);

  // Handle an external-button wake here rather than relying on the next
  // polling pass. This avoids losing the wake press during the sleep/loop
  // hand-off, especially when the INT6 low-level interrupt fires as the
  // button is released. A watchdog-only wake leaves standby unchanged.
  if (standbyButtonWakeFired || digitalRead(PIN_BUTTON3) == LOW) {
    delay(BUTTON_DEBOUNCE_MS);
    wakeFromStandby();
    initialiseButton(button3); // Treat the wake press as already consumed.
    button3WakeOnlyUntilRelease = true;
    renderDisplay();
  }
}

void loop() {
  if (!uiAssetsOK) return;
  pollEncoder();
  pollSensorRecovery(millis());
  pollAhtMeasurement(millis());
  pollPMS7003();
  processButtons();
  processEncoder();
  checkTimers(millis());
  pollEncoder();
  processEncoder();

  // Refresh the current UI after interaction or updated data; avoids clearing the LCD continuously.
  static uint16_t lastUiRefresh = 0;
  if (elapsedShort((uint16_t)millis(), lastUiRefresh, 250)) {
    lastUiRefresh = millis();
    if (powerState == OPERATING) renderDisplay();
  }

  // Only standby enters Power-down. Watchdog wake-ups keep configured standby
  // sensor and LCD refresh intervals progressing; Button 3 wakes immediately.
  standbyPowerDownOnce();
}
