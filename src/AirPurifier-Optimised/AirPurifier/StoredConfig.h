#ifndef STOREDCONFIG_H
#define STOREDCONFIG_H

#include <Arduino.h>

struct StoredConfig {
  uint32_t magic;
  uint16_t version;
  Config config;
  uint16_t checksum;
};

#endif