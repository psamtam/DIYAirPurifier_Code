#pragma once
// Private implementation: included only by Firmware.cpp.
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
  if (uiInteractionPending || elapsedShort((uint16_t)millis(), lastUiRefresh, 250)) {
    uiInteractionPending = false;
    lastUiRefresh = millis();
    if (powerState == OPERATING) renderDisplay();
  }

  // Only standby enters Power-down. Watchdog wake-ups keep configured standby
  // sensor and LCD refresh intervals progressing; Button 3 wakes immediately.
  standbyPowerDownOnce();
}
