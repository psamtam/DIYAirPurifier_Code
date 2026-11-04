#pragma once
// Private implementation: included only by Firmware.cpp.
// ============================================================================
// 6. SENSOR ACQUISITION AND PM ROLLING AVERAGE
// ============================================================================

uint16_t read16(uint8_t hi, uint8_t lo) { return ((uint16_t)hi << 8) | lo; }

void clearPmAverage() {
  pmBufferCount = 0;
  pmBufferIndex = 0;
  memset(pmBuffer1, 0, sizeof(pmBuffer1));
  memset(pmBuffer25, 0, sizeof(pmBuffer25));
  memset(pmBuffer10, 0, sizeof(pmBuffer10));
}

void addPmSample(uint16_t v1, uint16_t v25, uint16_t v10) {
  uint8_t window = config.pmAverageWindow;
  pmBuffer1[pmBufferIndex] = v1;
  pmBuffer25[pmBufferIndex] = v25;
  pmBuffer10[pmBufferIndex] = v10;
  pmBufferIndex = (pmBufferIndex + 1) % window;
  if (pmBufferCount < window) pmBufferCount++;
  uint32_t s1 = 0, s25 = 0, s10 = 0;
  for (uint8_t i = 0; i < pmBufferCount; ++i) { s1 += pmBuffer1[i]; s25 += pmBuffer25[i]; s10 += pmBuffer10[i]; }
  pm1 = s1 / pmBufferCount;
  pm25 = s25 / pmBufferCount;
  pm10 = s10 / pmBufferCount;
}

void pollPMS7003() {
  if (!pmsPowered) return;
  while (Serial1.available()) {
    uint8_t b = (uint8_t)Serial1.read();
    if (pmsIndex == 0 && b != 0x42) continue;
    if (pmsIndex == 1 && b != 0x4D) { pmsIndex = (b == 0x42) ? 1 : 0; continue; }
    pmsFrame[pmsIndex++] = b;
    if (pmsIndex == sizeof(pmsFrame)) {
      pmsIndex = 0;
      if (pmsFrame[2] != 0x00 || pmsFrame[3] != 0x1C) continue;
      uint16_t sum = 0;
      for (uint8_t i = 0; i < 30; ++i) sum += pmsFrame[i];
      if (sum != read16(pmsFrame[30], pmsFrame[31])) continue;
      // Atmospheric-environment values (CF=1 values are at bytes 10-15).
      addPmSample(read16(pmsFrame[10], pmsFrame[11]), read16(pmsFrame[12], pmsFrame[13]), read16(pmsFrame[14], pmsFrame[15]));
      pmsFrameEverReceived = true;
      pmsError = false;
      lastPmsFrameMs = millis();
    }
  }
  uint32_t now = millis();
  if (elapsed(now, pmsEnabledAtMs, PMS7003_STARTUP_GRACE_MS)) {
    if (!pmsFrameEverReceived || elapsed(now, lastPmsFrameMs, PMS_STALE_TIMEOUT_MS)) pmsError = true;
  }
}

uint32_t ahtMeasurementStartedMs = 0;
bool ahtMeasurementPending = false;
const uint32_t SENSOR_RETRY_MS = 10000UL;
// Start waits after command completion, with a millisecond quantisation margin.
// Measurement polling has additional margin over the datasheet's 80 ms wait.
uint32_t sensorAttemptMs[2] = {0, 0};
uint8_t ahtInitPhase = 0; // 0: retry interval, 1: reset wait, 2: calibration wait
bool bmpSettling = false;
uint8_t nextSensorRecovery = 0;
bool environmentalReadDue = false;

void sensorFailed(uint8_t target, uint32_t now) {
  sensorAttemptMs[target] = now;
  if (!target) {
    ahtOK = ahtReadingValid = ahtMeasurementPending = false;
    ahtInitPhase = 0;
  } else { bmpOK = bmpReadingValid = bmpSettling = false; }
}

void sensorRecovered(uint8_t target) {
  if (!target) { ahtOK = true; ahtInitPhase = 0; }
  else { bmpOK = true; bmpSettling = false; }
  // The normal scheduler obtains a fresh reading in the current power state.
  environmentalReadDue = true;
}

void pollSensorRecovery(uint32_t now) {
  uint8_t target = nextSensorRecovery;
  nextSensorRecovery ^= 1;
  if (!target) {
    if (ahtOK) return;
    if (!ahtInitPhase) {
      if (!elapsed(now, sensorAttemptMs[0], SENSOR_RETRY_MS)) return;
      if (aht.reset()) ahtInitPhase = 1;
      sensorAttemptMs[0] = millis();
    } else {
      if (!elapsed(now, sensorAttemptMs[0], ahtInitPhase == 1 ? 21 : 11)) return;
      uint8_t status = aht.status();
      if (status & 0x80) { sensorFailed(0, now); return; }
      if (status & 0x08) sensorRecovered(0);
      else if (ahtInitPhase == 1 && aht.calibrate()) {
        ahtInitPhase = 2; sensorAttemptMs[0] = millis();
      } else sensorFailed(0, now);
    }
  } else {
    if (bmpOK) return;
    if (bmpSettling) {
      if (elapsed(now, sensorAttemptMs[1], 100)) sensorRecovered(1);
    } else if (elapsed(now, sensorAttemptMs[1], SENSOR_RETRY_MS)) {
      sensorAttemptMs[1] = now;
      bmpSettling = bmp.begin(BMP280_I2C_ADDRESS_1) || bmp.begin(BMP280_I2C_ADDRESS_2);
    }
  }
}

void readEnvironmentalSensors() {
  uint32_t now = millis();
  if (ahtOK) {
    if (powerState == STANDBY) {
      // In Power-down standby the main loop sleeps between watchdog ticks.
      // Complete the AHT20 transaction while awake instead of leaving a
      // conversion pending across sleep cycles. This avoids stale/ERR readings
      // if the watchdog timing or millis bookkeeping differs from expectation.
      if (ahtMeasurementPending) {
        ahtMeasurementPending = false;
        int16_t pendingT, pendingH;
        if (aht.finishMeasurement(pendingT, pendingH)) {
          temperatureC = pendingT; humidityPercent = pendingH;
          ahtReadingValid = true; lastAhtSuccessMs = now;
        }
      }
      if (aht.startMeasurement()) {
        delay(80); // Only once per configured standby sensor refresh interval.
        int16_t t, h;
        if (aht.finishMeasurement(t, h)) {
          temperatureC = t; humidityPercent = h;
          ahtReadingValid = true; lastAhtSuccessMs = millis();
        } else sensorFailed(0, millis());
      } else sensorFailed(0, millis());
    } else if (!ahtMeasurementPending) {
      // Operating mode remains non-blocking so encoder polling stays responsive.
      if (aht.startMeasurement()) {
        ahtMeasurementStartedMs = millis();
        ahtMeasurementPending = true;
      } else sensorFailed(0, now);
    }
  } else ahtReadingValid = false;

  if (bmpOK) {
    int16_t t, p;
    if (bmp.read(t, p) && p > 0) {
      pressureHpa = p;
      bmpReadingValid = true; lastBmpSuccessMs = now;
    } else sensorFailed(1, now);
  } else bmpReadingValid = false;
}

void pollAhtMeasurement(uint32_t now) {
  if (!ahtMeasurementPending || !elapsed(now, ahtMeasurementStartedMs, 90)) return;
  ahtMeasurementPending = false;
  int16_t t, h;
  if (aht.finishMeasurement(t, h)) {
    temperatureC = t; humidityPercent = h;
    ahtReadingValid = true; lastAhtSuccessMs = now;
  } else sensorFailed(0, now);
}

bool pmsInGracePeriod() { return pmsPowered && !elapsed(millis(), pmsEnabledAtMs, PMS7003_STARTUP_GRACE_MS); }
bool pmValuesAvailable() { return pmsPowered && !pmsInGracePeriod() && !pmsError && pmBufferCount >= config.pmAverageWindow; }

