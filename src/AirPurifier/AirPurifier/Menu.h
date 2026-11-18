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
    case SET_ADJUSTMENT_TIMEOUT: lastUiActivityMs = now; break;
    case SET_MENU_TIMEOUT: lastMenuActivityMs = now; break;
    case SET_STANDBY_LCD_N: lastStandbyLcdMs = now; break;
    case SET_PMS_ENABLE:
      if (powerState == OPERATING) syncPmsPower();
      clearPmAverage(); break;
    case SET_PM_WINDOW: clearPmAverage(); break;
    case SET_OPERATING_SENSOR: lastOperatingSensorMs = now; break;
    case SET_STANDBY_SENSOR: lastStandbySensorMs = lastStandbyLcdMs = now; break;
    case SET_AUTO_STANDBY: outputsOffSinceMs = (fanSpeed == 0 && ledBrightness == 0) ? now : 0; break;
  }
}

int settingStep(SettingId id) { return id == SET_STANDBY_SENSOR ? 8 : 1; }
const char *settingName(SettingId id) {
  if (id == SET_ADJUSTMENT_TIMEOUT) return uiText(91);
  if (id == SET_MENU_TIMEOUT) return uiText(92);
  return uiText(id < 8 ? 1 + id : 9);
}

void beginConfigEdit() { configBeforeEdit = config; clockFormatBeforeEdit = use24HourClock; clockFormatSaveFailed = false; menuEditing = true; }
void cancelConfigEdit() {
  bool pmsChanged = config.pmsEnabled != configBeforeEdit.pmsEnabled;
  bool windowChanged = config.pmAverageWindow != configBeforeEdit.pmAverageWindow;
  config = configBeforeEdit;
  use24HourClock = clockFormatBeforeEdit;
  clockFormatSaveFailed = false;
  menuEditing = false;
  if (pmsChanged && powerState == OPERATING) syncPmsPower();
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
  char line[21] = "> ";
  PGM_P item;
  switch (menuIndex) {
    case 0: item = PSTR("Date & Time"); break;
    case 1: item = PSTR("Display"); break;
    case 2: item = PSTR("Sensors"); break;
    case 3: item = PSTR("Power"); break;
    case 4: item = PSTR("Fan Presets"); break;
    case 5: item = PSTR("LED Presets"); break;
    case 6: item = PSTR("Status"); break;
    case 7: item = PSTR("About"); break;
    default: item = PSTR("Factory Reset"); break;
  }
  strncpy_P(line + 2, item, 18);
  line[20] = '\0';
  lcdLine(1, line);
  tinyFormat_P(b, sizeof(b), uiText(24), (unsigned int)(menuIndex + 1), (unsigned int)MENU_ITEM_COUNT_ACTUAL);
  lcdValueLine(2, "", b, true);
  lcdLine(3, uiText(25));
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
  lcdLine(3, clockFormatSaveFailed ? "ERR" : (menuEditing ? uiText(32) : uiText(33)));
}

void drawDisplaySettings(uint8_t idx) {
  char b[21];
  lcdLine(0, uiText(26));
  if (idx < 5) {
    SettingId id = idx < 3 ? (SettingId)idx : (idx == 3 ? SET_ADJUSTMENT_TIMEOUT : SET_MENU_TIMEOUT);
    lcdLine(1, settingName(id));
    int v = getSettingValue(id);
    char format[8];
    strcpy_P(format, PSTR("%d %s"));
    tinyFormat_P(b, sizeof(b), format, v, id == SET_STANDBY_LCD_N ? "x" : "seconds");
    drawSettingValue(b);
  } else {
    lcdLine(1, uiText(27));
    lcdLine(2, menuEditing ? (use24HourClock ? uiText(28) : uiText(29)) : (use24HourClock ? uiText(30) : uiText(31)));
  }
  if (idx == 5) lcdLine(3, clockFormatSaveFailed ? "ERR" : (menuEditing ? uiText(32) : uiText(33)));
}

void drawSensorSettings(uint8_t idx) {
  SettingId id = (SettingId)(SET_PMS_ENABLE + idx); char b[21];
  lcdLine(0, uiText(34)); lcdLine(1, settingName(id));
  if (id == SET_PMS_ENABLE) {
    char mode[5];
    strcpy_P(mode, config.pmsEnabled == 0 ? PSTR("OFF") : (config.pmsEnabled == 1 ? PSTR("ON") : PSTR("AUTO")));
    drawSettingValue(mode);
    return;
  }
  if (id == SET_PM_WINDOW) tinyFormat_P(b, sizeof(b), uiText(35), (unsigned int)config.pmAverageWindow);
  else tinyFormat_P(b, sizeof(b), uiText(36), getSettingValue(id));
  drawSettingValue(b);
}

void drawPowerSettings() {
  char b[21]; lcdLine(0, uiText(37)); lcdLine(1, uiText(38));
  tinyFormat_P(b, sizeof(b), uiText(39), (unsigned int)config.autoStandbyTimeoutMinutes);
  drawSettingValue(b);
}

void drawPresetEditor(bool fan, uint8_t idx) {
  char b[21];
  lcdLine(0, fan ? uiText(40) : uiText(41));
  tinyFormat_P(b, sizeof(b), uiText(42), (unsigned int)(idx + 1), (unsigned int)PRESET_COUNT); lcdLine(1, b);
  char format[6];
  strcpy_P(format, PSTR("%u%%"));
  tinyFormat_P(b, sizeof(b), format, (unsigned int)(fan ? config.fanPresets[idx] : config.ledPresets[idx]));
  drawSettingValue(b);
}

void drawStatusPage() {
  char b[21]; lcdLine(0, uiText(43));
  tinyFormat_P(b, sizeof(b), uiText(44), rtcOK ? "OK" : "ERR", eepromOK ? "OK" : "ERR"); lcdLine(1, b);
  tinyFormat_P(b, sizeof(b), uiText(45), ahtReadingValid ? "OK" : "ERR", bmpReadingValid ? "OK" : "ERR"); lcdLine(2, b);
  tinyFormat_P(b, sizeof(b), uiText(46), !pmsPowered ? "OFF" : (pmsError ? "ERR" : (pmValuesAvailable() ? "OK" : "WAIT")), powerState == STANDBY ? "STBY" : "RUN"); lcdLine(3, b);
}

void drawAboutPage() {
  if (aboutPage == 0) {
    lcdLine(0, uiText(47));
    lcdLine(1, uiText(48));
    lcdLine(2, uiText(49));
    lcdLine(3, uiText(90));
  } else {
    uint8_t first = 93 + (aboutPage - 1) * 4;
    lcdLine(0, uiText(first));
    lcdLine(1, uiText(first + 1));
    lcdLine(2, uiText(first + 2));
    lcdLine(3, uiText(first + 3));
  }
}

void drawFactoryResetPage() {
  lcdLine(0, uiText(50));
  lcdLine(1, uiText(51));
  char blank[1] = {0};
  lcdLine(2, blank);
  lcdLine(3, uiText(52));
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
  if (menuPage == 0) { menuIndex = (menuIndex - delta % MENU_ITEM_COUNT_ACTUAL + MENU_ITEM_COUNT_ACTUAL) % MENU_ITEM_COUNT_ACTUAL; return; }
  if (menuPage == 1 && menuEditing) {
    if (submenuIndex == 5) { use24HourClock = !use24HourClock; return; }
    SettingId id = submenuIndex < 3 ? (SettingId)submenuIndex : (submenuIndex == 3 ? SET_ADJUSTMENT_TIMEOUT : SET_MENU_TIMEOUT);
    setSettingValue(id, getSettingValue(id) + delta); return;
  }
  if (menuPage == 2 && menuEditing) {
    SettingId id = (SettingId)(SET_PMS_ENABLE + submenuIndex);
    if (id == SET_PMS_ENABLE) setSettingValue(id, (config.pmsEnabled + (delta % 3) + 3) % 3);
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
    case 7: menuPage = 7; aboutPage = 0; break;
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
    syncPmsPower();
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
    if (menuPage == 1 && submenuIndex == 5) {
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

