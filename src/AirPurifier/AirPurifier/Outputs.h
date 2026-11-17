#pragma once
// Private implementation: included only by Firmware.cpp.
// ============================================================================
// 4. PWM, RELAY AND SENSOR POWER
// ============================================================================

void setupFanPWM() {
  pinMode(PIN_FAN, OUTPUT);
  digitalWrite(PIN_FAN, LOW);
  TCCR1A = 0; TCCR1B = 0; TCNT1 = 0;
  ICR1 = PWM_TOP; OCR1B = 0;
  TCCR1A = _BV(COM1B1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10);
}

void setupLedPWM() {
  pinMode(PIN_LED, OUTPUT);
  TCCR3A = 0; TCCR3B = 0; TCCR3C = 0; TCNT3 = 0;
  ICR3 = PWM_TOP; OCR3A = 0;
  TCCR3A = _BV(COM3A1) | _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);
}

void setFanPWM(uint8_t percent) {
  if (percent > 100) percent = 100;
  uint16_t duty = ((uint16_t)percent * 640U) / 100U;
  OCR1B = (duty >= 640) ? 639 : duty;
}

void setLedPWM(uint8_t percent) {
  if (percent > 100) percent = 100;
  if (!percent) {
    // OCR=0 in Fast PWM still produces narrow pulses: disconnect OC3A.
    TCCR3A &= (uint8_t)~_BV(COM3A1);
    digitalWrite(PIN_LED, LOW);
  } else {
    OCR3A = ((uint16_t)percent * PWM_TOP) / 100U;
    TCCR3A |= _BV(COM3A1);
  }
}

void setRelay(bool on) {
  fanRelayOn = on;
  digitalWrite(PIN_RELAY, (on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void setPmsPower(bool on) {
  pmsPowered = on;
  digitalWrite(PIN_PMS_EN, (on == PMS_ENABLE_ACTIVE_HIGH) ? HIGH : LOW);
  if (on) {
    pmsEnabledAtMs = millis();
    pmsFrameEverReceived = false;
    pmsError = false;
    pmBufferCount = 0;
    pmBufferIndex = 0;
    pmsIndex = 0;
  } else {
    pmsFrameEverReceived = false;
    pmsError = false;
    pmBufferCount = 0;
    pmBufferIndex = 0;
    pmsIndex = 0;
  }
}

void applyFanOutput() {
  if (!fanSpeed) delayTimers[0].cancel();
  setFanPWM(fanSpeed);
  setRelay(fanSpeed > 0);
}

void applyLedOutput() { if (!ledBrightness) delayTimers[1].cancel(); setLedPWM(ledBrightness); }

void applyOutputs() {
  applyFanOutput();
  applyLedOutput();
}

