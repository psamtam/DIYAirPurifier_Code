#pragma once
// Private implementation: included only by Firmware.cpp.
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
  uiInteractionPending = true;
  uint32_t now = millis();
  lastBacklightActivityMs = now;
  lastUiActivityMs = now;
  lastMenuActivityMs = now;
}

void enterMainDisplay() {
  uiInteractionPending = true;
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

