#pragma once
// ============================================================================
// COMPACT SENSOR / RTC DRIVERS
// Register-level replacements for Adafruit_AHTX0, Adafruit_BMP280 and RTClib.
// They retain the same sensor outputs and the BMP280's Adafruit default
// operating profile: T x2, P x16, filter x16, normal mode, 500 ms standby.
// ============================================================================

static uint8_t bcdToDec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t decToBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

class DateTime {
  uint16_t y; uint8_t mo, d, h, mi, se;
public:
  DateTime() : y(2000), mo(1), d(1), h(0), mi(0), se(0) {}
  DateTime(uint16_t yy, uint8_t mm, uint8_t dd, uint8_t hh, uint8_t mn, uint8_t ss)
    : y(yy), mo(mm), d(dd), h(hh), mi(mn), se(ss) {}
  uint16_t year() const { return y; }
  uint8_t month() const { return mo; }
  uint8_t day() const { return d; }
  uint8_t hour() const { return h; }
  uint8_t minute() const { return mi; }
  uint8_t second() const { return se; }
};

class DS3231Lite {
  bool readRegs(uint8_t reg, uint8_t *dst, uint8_t n) {
    Wire.beginTransmission(0x68); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)0x68, n) != n) return false;
    while (n--) *dst++ = Wire.read();
    return true;
  }
public:
  bool begin() {
    uint8_t v;
    return readRegs(0x0F, &v, 1) || probe();
  }
  bool probe() {
    Wire.beginTransmission(0x68); Wire.write((uint8_t)0x0F);
    return Wire.endTransmission() == 0;
  }
  bool lostPower() {
    uint8_t status = 0;
    return !readRegs(0x0F, &status, 1) || (status & 0x80);
  }
  DateTime now() {
    uint8_t r[7];
    if (!readRegs(0x00, r, 7)) return DateTime();
    uint8_t sec = bcdToDec(r[0] & 0x7F);
    uint8_t min = bcdToDec(r[1] & 0x7F);
    uint8_t hour;
    if (r[2] & 0x40) hour = (uint8_t)((bcdToDec(r[2] & 0x1F) % 12) + ((r[2] & 0x20) ? 12 : 0));
    else hour = bcdToDec(r[2] & 0x3F);
    uint8_t day = bcdToDec(r[4] & 0x3F);
    uint8_t month = bcdToDec(r[5] & 0x1F);
    uint16_t year = (uint16_t)(2000 + bcdToDec(r[6]) + ((r[5] & 0x80) ? 100 : 0));
    return DateTime(year, month, day, hour, min, sec);
  }
  void adjust(const DateTime &t) {
    Wire.beginTransmission(0x68); Wire.write((uint8_t)0x00);
    Wire.write(decToBcd(t.second())); Wire.write(decToBcd(t.minute()));
    Wire.write(decToBcd(t.hour())); Wire.write((uint8_t)1);
    Wire.write(decToBcd(t.day())); Wire.write(decToBcd(t.month()));
    Wire.write(decToBcd((uint8_t)(t.year() - 2000)));
    Wire.endTransmission();
    // Clear oscillator-stop flag after setting a valid time.
    Wire.beginTransmission(0x68); Wire.write((uint8_t)0x0F);
    Wire.endTransmission(false);
    if (Wire.requestFrom((uint8_t)0x68, (uint8_t)1) == 1) {
      uint8_t status = Wire.read() & (uint8_t)~0x80;
      Wire.beginTransmission(0x68); Wire.write((uint8_t)0x0F); Wire.write(status); Wire.endTransmission();
    }
  }
};

// A common fixed-point intermediate. Truncating at bit 15 preserves the
// exact original rounded tenths after the following 16-bit rounding steps.
static __attribute__((noinline)) uint16_t ahtQuarterUnits(uint32_t raw) {
  return (raw * 125UL) >> 15;
}

class AHT20Lite {
  bool writeCommand(uint8_t a, uint8_t b, uint8_t c) {
    Wire.beginTransmission(0x38); Wire.write(a); Wire.write(b); Wire.write(c);
    return Wire.endTransmission() == 0;
  }
public:
  bool reset() {
    Wire.beginTransmission(0x38); Wire.write((uint8_t)0xBA);
    return Wire.endTransmission() == 0;
  }
  uint8_t status() {
    if (Wire.requestFrom((uint8_t)0x38, (uint8_t)1) != 1) return 0xFF;
    return Wire.read();
  }
  bool calibrate() { return writeCommand(0xBE, 0x08, 0x00); }
  bool startMeasurement() { return writeCommand(0xAC, 0x33, 0x00); }
  bool finishMeasurement(int16_t &temperature, int16_t &humidity) {
    uint8_t d[6];
    if (Wire.requestFrom((uint8_t)0x38, (uint8_t)6) != 6) return false;
    for (uint8_t i=0; i<6; ++i) d[i] = Wire.read();
    if ((d[0] & 0x88) != 0x08) return false;
    uint32_t rawH = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t rawT = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    humidity = (ahtQuarterUnits(rawH) + 2U) >> 2;
    temperature = (int16_t)((ahtQuarterUnits(rawT) + 1U) >> 1) - 500;
    return true;
  }
};

class BMP280Lite {
  uint8_t address;
  uint16_t dig_T1, dig_P1;
  int16_t dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
  int32_t tFine;
  bool readBytes(uint8_t reg, uint8_t *dst, uint8_t n) {
    Wire.beginTransmission(address); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(address, n) != n) return false;
    while (n--) *dst++ = Wire.read();
    return true;
  }
  bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address); Wire.write(reg); Wire.write(value);
    return Wire.endTransmission() == 0;
  }
public:
  bool begin(uint8_t addr) {
    address = addr;
    uint8_t id;
    if (!readBytes(0xD0, &id, 1) || (id != 0x58 && id != 0x60)) return false;
    uint8_t status; if (!readBytes(0xF3, &status, 1) || (status & 1)) return false;
    uint8_t c[24]; if (!readBytes(0x88, c, 24)) return false;
    dig_T1 = (uint16_t)((uint16_t)c[1]<<8 | c[0]); dig_T2 = (int16_t)((uint16_t)c[3]<<8 | c[2]); dig_T3 = (int16_t)((uint16_t)c[5]<<8 | c[4]);
    dig_P1 = (uint16_t)((uint16_t)c[7]<<8 | c[6]); dig_P2 = (int16_t)((uint16_t)c[9]<<8 | c[8]); dig_P3 = (int16_t)((uint16_t)c[11]<<8 | c[10]);
    dig_P4 = (int16_t)((uint16_t)c[13]<<8 | c[12]); dig_P5 = (int16_t)((uint16_t)c[15]<<8 | c[14]); dig_P6 = (int16_t)((uint16_t)c[17]<<8 | c[16]);
    dig_P7 = (int16_t)((uint16_t)c[19]<<8 | c[18]); dig_P8 = (int16_t)((uint16_t)c[21]<<8 | c[20]); dig_P9 = (int16_t)((uint16_t)c[23]<<8 | c[22]);
    if (!dig_P1) return false;
    // Adafruit_BMP280 default sampling/filter/standby profile.
    return writeReg(0xF4, 0x54) && writeReg(0xF5, 0x90) && writeReg(0xF4, 0x57) && configured();
  }
  bool configured() {
    uint8_t r[2];
    return readBytes(0xF4, r, 2) && r[0] == 0x57 && r[1] == 0x90;
  }
  bool read(int16_t &temperature, int16_t &pressureHpa) {
    if (!configured()) return false;
    uint8_t d[6]; if (!readBytes(0xF7, d, 6)) return false;
    int32_t adcP = ((int32_t)d[0]<<12) | ((int32_t)d[1]<<4) | (d[2]>>4);
    int32_t adcT = ((int32_t)d[3]<<12) | ((int32_t)d[4]<<4) | (d[5]>>4);
    if (adcP == 0x80000 || adcT == 0x80000) return false;
    int32_t v1 = ((((adcT >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
    int32_t v2 = (((((adcT >> 4) - (int32_t)dig_T1) * ((adcT >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
    tFine = v1 + v2;
    int32_t temp100 = (tFine * 5 + 128) >> 8;
    temperature = (temp100 + (temp100 >= 0 ? 5 : -5)) / 10;
    int64_t p1 = (int64_t)tFine - 128000;
    int64_t p2 = p1 * p1 * (int64_t)dig_P6;
    p2 += (p1 * (int64_t)dig_P5) * 131072LL;
    p2 += ((int64_t)dig_P4) * 34359738368LL;
    p1 = ((p1 * p1 * (int64_t)dig_P3) >> 8) + ((p1 * (int64_t)dig_P2) * 4096LL);
    p1 = (((((int64_t)1) << 47) + p1) * (int64_t)dig_P1) >> 33;
    if (!p1) return false;
    int64_t p = 1048576 - adcP;
    p = (((p << 31) - p2) * 3125) / p1;
    p1 = ((int64_t)dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    p2 = ((int64_t)dig_P8 * p) >> 19;
    p = ((p + p1 + p2) >> 8) + (((int64_t)dig_P7) * 16LL);
    pressureHpa = (int16_t)((p + 1280) / 2560);
    return true;
  }
};

