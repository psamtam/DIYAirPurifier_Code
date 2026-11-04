#ifndef BUTTONSTATE_H
#define BUTTONSTATE_H

#include <Arduino.h>

struct ButtonState {
  uint8_t pin;
  bool stablePressed;
  bool lastRawPressed;
  uint32_t rawChangedAt;
  uint32_t pressedAt;
  bool longPressFired;
};

#endif