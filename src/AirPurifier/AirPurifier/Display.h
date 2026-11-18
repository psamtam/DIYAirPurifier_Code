#pragma once
// Private implementation: included only by Firmware.cpp.
// ============================================================================
// 10. LCD RENDERING
// ============================================================================

void formatClockLine(char *out, size_t outSize, uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
  if (outSize < 21) { if (outSize) out[0] = '\0'; return; }
  uint8_t h = hour;
  if (!use24HourClock) { h = hour % 12; if (!h) h = 12; }
  uint8_t position = use24HourClock ? 15 : (h < 10 ? 13 : 12);
  tinyFormat_P(out, outSize, uiText(55), (unsigned int)year, (unsigned int)month, (unsigned int)day);
  memset(out + 10, ' ', position - 10);
  tinyFormat_P(out + position, outSize - position, uiText(use24HourClock ? 53 : 54),
               (unsigned int)h, (unsigned int)minute, hour >= 12 ? "PM" : "AM");
}

void renderClock() {
  char b[21];
  if (rtcOK) { DateTime n = rtc.now(); formatClockLine(b, sizeof(b), n.year(), n.month(), n.day(), n.hour(), n.minute()); }
  else strcpy(b, "RTC ERR");
  lcdLine(0, b);
}

void renderMainEnvironmental() {
  char b[21], v[12], t[12], h[12];
  renderClock();
  formatReading(v, sizeof(v), temperatureC);
  strcpy(t, ahtReadingValid && !elapsed(millis(), lastAhtSuccessMs, SENSOR_STALE_TIMEOUT_MS) ? v : "ERR");
  formatReading(v, sizeof(v), humidityPercent);
  strcpy(h, ahtReadingValid && !elapsed(millis(), lastAhtSuccessMs, SENSOR_STALE_TIMEOUT_MS) ? v : "ERR");
  lcdLine(1, uiText(56));
  tinyFormat_P(b, sizeof(b), uiText(57), t, h); lcdLine(2, b);
  formatPressure(v, sizeof(v), pressureHpa100000);
  tinyFormat_P(b, sizeof(b), uiText(58), bmpReadingValid && !elapsed(millis(), lastBmpSuccessMs, SENSOR_STALE_TIMEOUT_MS) ? v : "ERR"); lcdLine(3, b);
}

void renderMainParticles() {
  char b[21];
  renderClock();
  const char *state = !pmsPowered ? "OFF" : (pmsInGracePeriod() ? "--" : (pmsError ? "ERR" : "--"));
  bool available = pmValuesAvailable();
  uint16_t values[3] = {pm1, pm25, pm10};
  for (uint8_t i = 0; i < 3; ++i) {
    if (available) tinyFormat_P(b, sizeof(b), uiText(59 + i), values[i]);
    else tinyFormat_P(b, sizeof(b), uiText(62 + i), state);
    lcdLine(i + 1, b);
  }
}

void renderAdjustment() {
  bool fan = uiState == FAN_ADJUSTMENT;
  uint8_t target = fan ? 0 : 1;
  char b[21];
  lcdLine(0, fan ? uiText(65) : uiText(66));
  char format[6];
  strcpy_P(format, PSTR("%u%%"));
  tinyFormat_P(b, sizeof(b), format, (unsigned int)(fan ? fanSpeed : ledBrightness));
  lcdValueLine(1, fan ? uiText(67) : uiText(68), b, true);
  if (delayTimers[target].active) {
    tinyFormat_P(b, sizeof(b), uiText(69), (unsigned int)delayTimers[target].remainingMinutes(millis()));
    lcdLine(2, b);
  } else lcdLine(2, uiText(70));
  lcdLine(3, fan ? uiText(71) : uiText(72));
}

void renderDelayEditor() {
  char b[21];
  lcdLine(0, delayEditTarget == 0 ? uiText(73) : uiText(74));
  if (delayEditMinutes) tinyFormat_P(b, sizeof(b), uiText(75), (unsigned int)delayEditMinutes);
  else strcpy(b, uiText(76));
  lcdLine(1, b);
  lcdLine(2, delayOutputOffMessage ? uiText(77) : uiText(78));
  lcdLine(3, delayEditTarget == 0 ? uiText(79) : uiText(80));
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
  lcdLine(3, uiText(81));
  // Deliberately do not call lcd.backlight() here.
}

