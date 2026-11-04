#pragma once
// Private implementation: included only by Firmware.cpp.
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
    DEFAULT_PRESET_VALUES, DEFAULT_PRESET_VALUES, DEFAULT_FAN_SPEED, DEFAULT_LED_BRIGHTNESS
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

#if UI_TEXT_CACHE_SLOTS == 1
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


#else
// Six entries cover a complete home page, including both clock templates.
// Asset data is immutable during operation; a failed read never becomes a hit.
uint8_t uiTextCacheCount = 0;
uint8_t uiTextCacheNext = 0;
const char *uiText(uint8_t id) {
  static char cache[UI_TEXT_CACHE_SLOTS][32];
  static uint8_t ids[UI_TEXT_CACHE_SLOTS];
  for (uint8_t i = 0; i < uiTextCacheCount; ++i) if (ids[i] == id) return cache[i];
  uint8_t slot = uiTextCacheCount < UI_TEXT_CACHE_SLOTS ? uiTextCacheCount : uiTextCacheNext;
  if (id >= UI_TEXT_COUNT || !eepromReadBytes(264 + (uint16_t)id * 32, (uint8_t *)cache[slot], 32)) {
    ids[slot] = 0xFF;
    strcpy(cache[slot], "TEXT ERR");
  } else {
    ids[slot] = id;
    if (uiTextCacheCount < UI_TEXT_CACHE_SLOTS) ++uiTextCacheCount;
    else if (++uiTextCacheNext == UI_TEXT_CACHE_SLOTS) uiTextCacheNext = 0;
    cache[slot][31] = '\0';
  }
  return cache[slot];
}

#endif
