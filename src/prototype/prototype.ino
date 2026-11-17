
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <RTClib.h>
#include <math.h>

//https://github.com/marcoschwartz/LiquidCrystal_I2C =====================================================
// ARDUINO PRO MICRO - ATmega32U4, 5V / 16 MHz
// =====================================================

// Pin assignments: Arduino digital pin numbers
const uint8_t PIN_PMS_EN   = 4;
const uint8_t PIN_LED      = 5;
const uint8_t PIN_BUTTON3   = 7;
const uint8_t PIN_RELAY    = 8;
const uint8_t PIN_FAN      = 10;
const uint8_t PIN_ENC_A    = A0;
const uint8_t PIN_ENC_B    = A1;
const uint8_t PIN_BUTTON1  = 14;
const uint8_t PIN_BUTTON2  = 15;

// PMS7003 hardware UART:
// Pro Micro RX (D0) <- PMS TX through level shifter
// Pro Micro TX (D1) -> PMS RX through level shifter

LiquidCrystal_I2C lcd(0x27, 20, 4);
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
RTC_DS3231 rtc;

// Set true for ONE upload to force the RTC to the
// sketch compilation date/time.
// Then set it back to false and upload again.
const bool FORCE_RTC_SET = false;

// Set true only if your relay module is active LOW.
const bool RELAY_ACTIVE_LOW = true;

// Set true if your PMS EN input is active HIGH.
const bool PMS_EN_ACTIVE_HIGH = true;

// Encoder setting, 0-100%.
int controlPercent = 50;

// Sensor availability
bool lcdOK = false;
bool ahtOK = false;
bool bmpOK = false;
bool rtcOK = false;

// Latest readings
float ahtTemp = NAN;
float ahtHumidity = NAN;
float bmpTemp = NAN;
float bmpPressure = NAN;

uint16_t pm1 = 0;
uint16_t pm25 = 0;
uint16_t pm10 = 0;
bool pmsDataOK = false;
unsigned long lastPmsMillis = 0;

// Timing
unsigned long lastSensorRead = 0;
unsigned long lastDisplay = 0;
unsigned long lastSerialReport = 0;
uint8_t displayPage = 0;

// Encoder decoder
uint8_t previousEncoderState = 0;
int8_t encoderAccumulator = 0;

const int8_t encoderTable[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

// =====================================================
// FAN PWM: TIMER1, OC1B ON D10, 25 kHz
// 16 MHz / (1 * (639 + 1)) = 25,000 Hz
// =====================================================

void setupFanPWM() {
  pinMode(PIN_FAN, OUTPUT);
  digitalWrite(PIN_FAN, LOW);

  // Disconnect Timer1 outputs and stop Timer1.
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  // Fast PWM mode 14, TOP = ICR1, prescaler = 1.
  ICR1 = 639;
  OCR1B = 0;

  // Non-inverting PWM on OC1B (D10).
  TCCR1A = _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
}

// LED PWM on D5 (OC3A), using Timer3
// Approx. 25 kHz at 16 MHz CPU clock
// Assumes a 5V / 16MHz Pro Micro.

void setupLedPwm() {
  pinMode(5, OUTPUT);

  // Stop Timer3 while configuring it
  TCCR3A = 0;
  TCCR3B = 0;
  TCCR3C = 0;

  // Fast PWM, mode 14: TOP = ICR3
  ICR3 = 639;

  // Non-inverting PWM on OC3A (D5)
  TCCR3A = (1 << COM3A1) | (1 << WGM31);
  TCCR3B = (1 << WGM33) | (1 << WGM32) | (1 << CS30);

  OCR3A = 0;  // Start with LED off
}

void setFanPercent(uint8_t percent) {
  percent = constrain(percent, 0, 100);

  // 0-100% maps to 0-640 timer counts.
  // Avoid OCR1B=TOP at 100%, which is a special case
  // for Fast PWM hardware.
  uint16_t duty = ((uint32_t)percent * 640UL) / 100UL;

  if (duty >= 640) {
    OCR1B = 639;
  } else {
    OCR1B = duty;
  }
}

void setLedPercent(uint8_t percent) {
  percent = constrain(percent, 0, 100);

  // Timer TOP = 639
  OCR3A = (uint32_t)percent * 639 / 100;
}

// =====================================================
// OUTPUT CONTROL
// =====================================================

void applyOutputs() {
  setLedPercent(controlPercent);
  setFanPercent(controlPercent);
}

void setRelay(bool on) {
  digitalWrite(PIN_RELAY,
    (on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

// =====================================================
// PMS7003: 32-BYTE ACTIVE-MODE FRAME PARSER
// =====================================================

uint8_t pmsFrame[32];
uint8_t pmsIndex = 0;

uint16_t read16(uint8_t hi, uint8_t lo) {
  return ((uint16_t)hi << 8) | lo;
}

void pollPMS7003() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();

    if (pmsIndex == 0 && b != 0x42) {
      continue;
    }

    if (pmsIndex == 1 && b != 0x4D) {
      pmsIndex = (b == 0x42) ? 1 : 0;
      continue;
    }

    pmsFrame[pmsIndex++] = b;

    if (pmsIndex >= sizeof(pmsFrame)) {
      pmsIndex = 0;

      if (pmsFrame[0] != 0x42 ||
          pmsFrame[1] != 0x4D ||
          pmsFrame[2] != 0x00 ||
          pmsFrame[3] != 0x1C) {
        continue;
      }

      uint16_t checksum = 0;
      for (uint8_t i = 0; i < 30; i++) {
        checksum += pmsFrame[i];
      }

      uint16_t receivedChecksum =
        read16(pmsFrame[30], pmsFrame[31]);

      if (checksum != receivedChecksum) {
        continue;
      }

      // Atmospheric environment PM values:
      // PM1.0 at bytes 10-11
      // PM2.5 at bytes 12-13
      // PM10  at bytes 14-15
      pm1  = read16(pmsFrame[10], pmsFrame[11]);
      pm25 = read16(pmsFrame[12], pmsFrame[13]);
      pm10 = read16(pmsFrame[14], pmsFrame[15]);

      pmsDataOK = true;
      lastPmsMillis = millis();
    }
  }
}

// =====================================================
// ROTARY ENCODER
// =====================================================

void pollEncoder() {
  uint8_t a = digitalRead(PIN_ENC_A);
  uint8_t b = digitalRead(PIN_ENC_B);

  uint8_t currentState = (a << 1) | b;
  uint8_t transition =
    (previousEncoderState << 2) | currentState;

  encoderAccumulator += encoderTable[transition];
  previousEncoderState = currentState;

  // Most mechanical encoders produce four transitions
  // per detent. Adjust if your encoder differs.
  if (encoderAccumulator >= 4) {
    controlPercent++;
    encoderAccumulator = 0;
  } else if (encoderAccumulator <= -4) {
    controlPercent--;
    encoderAccumulator = 0;
  }

  controlPercent = constrain(controlPercent, 0, 100);
}

void pollButtons() {
  static bool lastButton1 = HIGH;
  static bool lastButton2 = HIGH;
  static bool lastButton3 = HIGH;

  static unsigned long lastPressTime = 0;
  const unsigned long debounceMs = 50;

  bool button1 = digitalRead(PIN_BUTTON3);
  bool button2 = digitalRead(PIN_BUTTON1);
  bool button3 = digitalRead(PIN_BUTTON2);

  bool pressed =
      (lastButton1 == HIGH && button1 == LOW) ||
      (lastButton2 == HIGH && button2 == LOW) ||
      (lastButton3 == HIGH && button3 == LOW);

  lastButton1 = button1;
  lastButton2 = button2;
  lastButton3 = button3;

  if (pressed && millis() - lastPressTime >= debounceMs) {
    static bool relayOn = false;

    relayOn = !relayOn;
    setRelay(relayOn);

    lastPressTime = millis();

    Serial.print("Relay: ");
    Serial.println(relayOn ? "ON" : "OFF");
  }
}

// =====================================================
// SENSOR READINGS
// =====================================================

void readSensors() {
  if (ahtOK) {
    sensors_event_t humidity, temperature;

    if (aht.getEvent(&humidity, &temperature)) {
      ahtTemp = temperature.temperature;
      ahtHumidity = humidity.relative_humidity;
    }
  }

  if (bmpOK) {
    bmpTemp = bmp.readTemperature();
    bmpPressure = bmp.readPressure() / 100.0F;
  }
}

// =====================================================
// LCD HELPERS
// =====================================================

void printPadded(uint8_t col, uint8_t row, const char *text) {
  lcd.setCursor(col, row);

  uint8_t count = 0;
  while (*text && count < 20) {
    lcd.print(*text++);
    count++;
  }

  while (count++ < 20) {
    lcd.print(' ');
  }
}


void showScreen() {
  char line[21];

  if (displayPage == 0) {
    printPadded(0, 0, "AHT20 - Temperature");

    char line[21];

    if (ahtOK && !isnan(ahtTemp)) {
      dtostrf(ahtTemp, 5, 1, line);
      char tempLine[21];
      snprintf(tempLine, sizeof(tempLine), "Temp: %s C", line);
      printPadded(0, 1, tempLine);
    } else {
      printPadded(0, 1, "Temp: SENSOR ERROR");
    }

    if (ahtOK && !isnan(ahtHumidity)) {
      dtostrf(ahtHumidity, 5, 1, line);
      char humidityLine[21];
      snprintf(humidityLine, sizeof(humidityLine), "Humidity: %s%%", line);
      printPadded(0, 2, humidityLine);
    } else {
      printPadded(0, 2, "Humidity: ERROR");
    }

    printPadded(0, 3, "Page 1/4");
  }

  else if (displayPage == 1) {
    printPadded(0, 0, "BMP280 - Barometer");

    char line[21];

    if (bmpOK && !isnan(bmpTemp)) {
      dtostrf(bmpTemp, 5, 1, line);
      char tempLine[21];
      snprintf(tempLine, sizeof(tempLine), "Temp: %s C", line);
      printPadded(0, 1, tempLine);
    } else {
      printPadded(0, 1, "Temp: SENSOR ERROR");
    }

    if (bmpOK && !isnan(bmpPressure)) {
      dtostrf(bmpPressure, 7, 1, line);
      char pressureLine[21];
      snprintf(pressureLine, sizeof(pressureLine), "Pressure:%s hPa", line);
      printPadded(0, 2, pressureLine);
    } else {
      printPadded(0, 2, "Pressure: ERROR");
    }

    printPadded(0, 3, "Page 2/4");
  }


  else if (displayPage == 2) {
    printPadded(0, 0, "DS3231 Real Time");

    if (rtcOK) {
      DateTime now = rtc.now();

      snprintf(line, sizeof(line), "%04u-%02u-%02u",
        now.year(), now.month(), now.day());
      printPadded(0, 1, line);

      snprintf(line, sizeof(line), "%02u:%02u:%02u",
        now.hour(), now.minute(), now.second());
      printPadded(0, 2, line);
    } else {
      printPadded(0, 1, "RTC NOT DETECTED");
      printPadded(0, 2, "Check I2C wiring");
    }

    printPadded(0, 3, "Page 3/4");
  }

  else {
    printPadded(0, 0, "PMS7003 Particles");

    if (pmsDataOK &&
        millis() - lastPmsMillis < 10000UL) {
      snprintf(line, sizeof(line), "PM1.0: %5u ug/m3", pm1);
      printPadded(0, 1, line);

      snprintf(line, sizeof(line), "PM2.5: %5u ug/m3", pm25);
      printPadded(0, 2, line);

      snprintf(line, sizeof(line), "PM10 : %5u ug/m3", pm10);
      printPadded(0, 3, line);
    } else {
      printPadded(0, 1, "Waiting for valid data");
      printPadded(0, 2, "Check UART / sensor");
      snprintf(line, sizeof(line), "Output: %3d%%",
        controlPercent);
      printPadded(0, 3, line);
    }
  }
}

// =====================================================
// USB SERIAL DIAGNOSTICS
// =====================================================

void reportStatus() {
  Serial.println(F("\n--- Communication status ---"));

  Serial.print(F("AHT20: "));
  Serial.println(ahtOK ? F("OK") : F("NOT FOUND"));

  Serial.print(F("BMP280: "));
  Serial.println(bmpOK ? F("OK") : F("NOT FOUND"));

  Serial.print(F("DS3231: "));
  Serial.println(rtcOK ? F("OK") : F("NOT FOUND"));

  Serial.print(F("PMS7003 valid frame: "));
  Serial.println(pmsDataOK ? F("RECEIVED") : F("WAITING"));

  Serial.print(F("Encoder setting: "));
  Serial.print(controlPercent);
  Serial.println(F("%"));

  Serial.print(F("Fan PWM: 25 kHz, duty command "));
  Serial.print(controlPercent);
  Serial.println(F("%"));

  Serial.print(F("LED brightness command: "));
  Serial.print(controlPercent);
  Serial.println(F("%"));

  if (pmsDataOK) {
    Serial.print(F("PM1.0="));
    Serial.print(pm1);
    Serial.print(F(" PM2.5="));
    Serial.print(pm25);
    Serial.print(F(" PM10="));
    Serial.println(pm10);
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  // USB serial for diagnostics.
  Serial.begin(115200);

  // PMS7003 UART: default 9600 baud.
  Serial1.begin(9600);

  pinMode(PIN_LED, OUTPUT);

  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);

  // Relay is deliberately kept OFF during this test.
  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false);

  pinMode(PIN_PMS_EN, OUTPUT);
  digitalWrite(PIN_PMS_EN,
    PMS_EN_ACTIVE_HIGH ? HIGH : LOW);

  setupFanPWM();
  setupLedPwm();

  previousEncoderState =
    (digitalRead(PIN_ENC_A) << 1) |
     digitalRead(PIN_ENC_B);

  applyOutputs();

  Wire.begin();
  Wire.setClock(100000);

  lcd.init();
  lcd.backlight();
  lcdOK = true;

  printPadded(0, 0, "Pro Micro Test");
  printPadded(0, 1, "Starting devices...");
  printPadded(0, 2, "Please wait");
  printPadded(0, 3, "USB: 115200 baud");

  delay(500);

  ahtOK = aht.begin();

  // Most BMP280 modules use address 0x76 or 0x77.
  bmpOK = bmp.begin(0x76);
  if (!bmpOK) {
    bmpOK = bmp.begin(0x77);
  }

  rtcOK = rtc.begin();

  if (rtcOK) {
    // Set compile-time date/time if forced, or if the
    // RTC reports that it lost power.
    if (FORCE_RTC_SET || rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  printPadded(0, 0, "Device startup check");
  printPadded(0, 1, ahtOK ? "AHT20: OK" : "AHT20: FAIL");
  printPadded(0, 2, bmpOK ? "BMP280: OK" : "BMP280: FAIL");
  printPadded(0, 3, rtcOK ? "DS3231: OK" : "DS3231: FAIL");

  delay(1800);

  Serial.println(F("\nPro Micro verification sketch"));
  Serial.println(F("LCD address: 0x27"));
  Serial.println(F("PMS7003 UART: Serial1 at 9600 baud"));
  Serial.println(F("Fan PWM target: 25 kHz on D10"));
  Serial.println(F("Relay held OFF for testing"));
  Serial.println(F("RTC uses compile time only when forced or power lost"));

  readSensors();
  showScreen();
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  pollEncoder();
  pollPMS7003();
  pollButtons();

  static int previousPercent = -1;

  if (controlPercent != previousPercent) {
    applyOutputs();
    previousPercent = controlPercent;
  }

  unsigned long now = millis();

  // Read I2C sensors every 2 seconds.
  if (now - lastSensorRead >= 2000UL) {
    lastSensorRead = now;
    readSensors();
  }

  // Rotate LCD screen every 3 seconds.
  if (now - lastDisplay >= 3000UL) {
    lastDisplay = now;
    displayPage = (displayPage + 1) % 4;
    showScreen();
  }

  // Serial diagnostics every 5 seconds.
  if (now - lastSerialReport >= 5000UL) {
    lastSerialReport = now;
    reportStatus();
  }

  delay(1);
}
