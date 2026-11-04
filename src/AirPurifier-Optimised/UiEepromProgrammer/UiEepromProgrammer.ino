/* Load external display assets before uploading AirPurifier.
   Only AT24C32 addresses 256..4095 may be written. Settings are preserved. */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <avr/pgmspace.h>
#include "UiImage.h"

const uint8_t EEPROM_ADDRESS = 0x57;
LiquidCrystal_I2C lcd(0x27, 20, 4);
bool programmingSucceeded = false;

bool writeBlock(uint16_t address, const uint8_t *data, uint16_t length) {
  if (address < 256 || address > 4096 || length > 4096 - address) return false;
  while (length) {
    uint8_t chunk = 32 - address % 32;
    if (chunk > 30) chunk = 30;
    if (chunk > length) chunk = length;
    Wire.beginTransmission(EEPROM_ADDRESS);
    Wire.write((uint8_t)(address >> 8)); Wire.write((uint8_t)address);
    for (uint8_t i = 0; i < chunk; ++i) if (Wire.write(data[i]) != 1) return false;
    if (Wire.endTransmission() != 0) return false;
    delay(6);
    address += chunk; data += chunk; length -= chunk;
  }
  return true;
}

bool readBlock(uint16_t address, uint8_t *data, uint8_t length) {
  if (address < 256 || address > 4096 || length > 4096-address || length > 28) return false;
  Wire.beginTransmission(EEPROM_ADDRESS);
  Wire.write((uint8_t)(address >> 8)); Wire.write((uint8_t)address);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(EEPROM_ADDRESS, length) != length) return false;
  for (uint8_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

bool verifyPart(uint16_t offset, uint16_t length) {
  uint8_t block[28];
  while (length) {
    uint8_t count = length > sizeof(block) ? sizeof(block) : length;
    if (!readBlock(256 + offset, block, count)) return false;
    for (uint8_t i = 0; i < count; ++i)
      if (block[i] != pgm_read_byte(UI_IMAGE + offset + i)) return false;
    offset += count; length -= count;
  }
  return true;
}

bool programUiAssets() {
  // Invalidate first. Interrupted programming cannot appear to be complete.
  uint8_t block[28] = {0};
  if (!writeBlock(256, block, 8)) return false;
  for (uint16_t offset = 8; offset < sizeof(UI_IMAGE); ) {
    uint8_t count = sizeof(UI_IMAGE)-offset > sizeof(block) ? sizeof(block) : sizeof(UI_IMAGE)-offset;
    for (uint8_t i = 0; i < count; ++i) block[i] = pgm_read_byte(UI_IMAGE + offset + i);
    if (!writeBlock(256 + offset, block, count)) return false;
    offset += count;
  }
  if (!verifyPart(8, sizeof(UI_IMAGE)-8)) return false;
  // Commit the valid header only after the complete payload was verified.
  for (uint8_t i = 0; i < 8; ++i) block[i] = pgm_read_byte(UI_IMAGE + i);
  return writeBlock(256, block, 8) && verifyPart(0, sizeof(UI_IMAGE));
}

void setup() {
  // Same active-low relay and active-high sensor/LED wiring as the main sketch.
  digitalWrite(8, HIGH); pinMode(8, OUTPUT);
  digitalWrite(4, LOW); pinMode(4, OUTPUT);
  digitalWrite(5, LOW); pinMode(5, OUTPUT);
  digitalWrite(10, LOW); pinMode(10, OUTPUT);
  Serial.begin(9600);
  Wire.begin(); Wire.setClock(100000); Wire.setWireTimeout(25000UL, true);
  lcd.init(); lcd.backlight(); lcd.setCursor(0,0); lcd.print(F("Loading UI EEPROM"));
  programmingSucceeded = programUiAssets();
  const char *status = programmingSucceeded ? "UI load verified" : "UI load FAILED";
  lcd.setCursor(0,1); lcd.print(status); Serial.println(status);
  lcd.setCursor(0,2); lcd.print(F("Upload main firmware"));
}

void loop() {}
