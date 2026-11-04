#pragma once
// Private implementation: included only by Firmware.cpp.
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
  // Valid years are 2000..2099: every fourth year in this range is a leap year.
  bool leap = (y % 4 == 0);
  if (m == 2 && leap) maxDay = 29;
  return d >= 1 && d <= maxDay;
}

void setBacklight(bool on) {
  lcdBacklightOn = on;
  if (lcdOK) { if (on) lcd.backlight(); else lcd.noBacklight(); }
}

