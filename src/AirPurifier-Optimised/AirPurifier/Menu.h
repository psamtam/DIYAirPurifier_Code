#pragma once
// Private implementation: included only by Firmware.cpp.
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
  bool pmsChanged = config.pmsEnabled != configBeforeEdit.pmsEnabled;
  bool windowChanged = config.pmAverageWindow != configBeforeEdit.pmAverageWindow;
  config = configBeforeEdit;
  use24HourClock = clockFormatBeforeEdit;
  clockFormatSaveFailed = false;
  menuEditing = false;
  if (pmsChanged && powerState == OPERATING) setPmsPower(config.pmsEnabled != 0);
  if (pmsChanged || windowChanged) clearPmAverage();
  // Restored intervals use the latest scheduled reading. Unrelated edits
  // must not postpone healthy environmental sampling.
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
  const char *item = uiText(menuIndex < 8 ? 25 + menuIndex : 24);
  lcdValueLine(1, "> ", item, false);
  tinyFormat_P(b, sizeof(b), uiText(33), (unsigned int)(menuIndex + 1), (unsigned int)MENU_ITEM_COUNT_ACTUAL);
  lcdValueLine(2, "", b, true);
  lcdLine(3, uiText(35));
}

void lcdValueLine(uint8_t row, const char *prefix, const char *value, bool rightAligned) {
  char line[21];
  uint8_t n = 0;
  while (*prefix && n < 20) line[n++] = *prefix++;
  uint8_t position = rightAligned ? 20 - strlen(value) : n;
  while (n < position) line[n++] = ' ';
  n = position;
  while (*value && n < 20) line[n++] = *value++;
  line[n] = '\0';
  lcdLine(row, line);
}

void drawSettingValue(const char *value) {
  lcdValueLine(2, menuEditing ? ">>> " : "  > ", value, false);
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

