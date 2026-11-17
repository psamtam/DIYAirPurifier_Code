#ifndef STOREDCONFIG_H
#define STOREDCONFIG_H

#include <Arduino.h>
#include "Config.h"

struct StoredConfig {
  uint32_t magic;
  uint16_t version;
  Config config;
  uint16_t checksum;
};

#endif
