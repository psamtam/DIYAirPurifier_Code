#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

const uint8_t PRESET_COUNT = 6;

struct Config {
  uint16_t backlightTimeoutSeconds;
  uint16_t mainScreenRotationSeconds;
  uint8_t standbyLcdIntervalN;
  uint8_t pmsEnabled;
  uint8_t pmAverageWindow;
  uint16_t operatingSensorIntervalSeconds;
  uint16_t standbySensorIntervalSeconds;
  uint16_t autoStandbyTimeoutMinutes;
  uint8_t fanPresets[PRESET_COUNT];
  uint8_t ledPresets[PRESET_COUNT];
  uint8_t defaultFanSpeed;
  uint8_t defaultLedBrightness;
};

#endif