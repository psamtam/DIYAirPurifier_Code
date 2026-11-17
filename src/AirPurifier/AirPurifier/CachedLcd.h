#pragma once
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <string.h>

// Cache what was sent. A logical cursor move costs no bus traffic until a
// different character is printed; unchanged characters break a hardware run.
class CachedLcd : public LiquidCrystal_I2C {
  char sent[80];
  uint8_t row = 0, column = 0;
  bool writing = false;
public:
  CachedLcd(uint8_t address, uint8_t columns, uint8_t rows)
    : LiquidCrystal_I2C(address, columns, rows) {}
  void init() { LiquidCrystal_I2C::init(); memset(sent, 0, sizeof(sent)); writing = false; }
  void setCursor(uint8_t c, uint8_t r) { column = c; row = r; writing = false; }
  void print(char c) {
    if (row >= 4 || column >= 20) return;
    uint8_t index = row * 20 + column;
    if (sent[index] != c) {
      if (!writing) LiquidCrystal_I2C::setCursor(column, row);
      LiquidCrystal_I2C::write((uint8_t)c);
      sent[index] = c;
      writing = true;
    } else writing = false;
    ++column;
  }
  void print(const char *text) { while (*text) print(*text++); }
};
