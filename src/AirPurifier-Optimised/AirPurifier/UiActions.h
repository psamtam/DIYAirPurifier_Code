#pragma once
// Private implementation: included only by Firmware.cpp.
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

