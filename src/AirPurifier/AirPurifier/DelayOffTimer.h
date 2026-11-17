#pragma once
#include <stdint.h>

// Duration selections and countdowns are session-only; 0 means disabled.
struct DelayOffTimer {
  uint32_t startedAtMs = 0;
  uint8_t minutes = 0;
  bool active = false;
  void set(uint32_t now, uint8_t selection) {
    startedAtMs = now; minutes = selection; active = selection != 0;
  }
  void cancel() { active = false; }
  uint32_t durationMs() const { return (uint32_t)minutes * 60000UL; }
  bool expired(uint32_t now) const {
    return active && (uint32_t)(now - startedAtMs) >= durationMs();
  }
  uint8_t remainingMinutes(uint32_t now) const {
    uint32_t passed = (uint32_t)(now - startedAtMs);
    if (!active || passed >= durationMs()) return 0;
    return (durationMs() - passed + 59999UL) / 60000UL;
  }
};

inline uint8_t stepDelayMinutes(uint8_t value, int16_t detents) {
  int32_t next = (int32_t)value + (int32_t)detents * 5;
  if (next < 0) next = 0;
  if (next > 240) next = 240;
  return (uint8_t)next;
}
