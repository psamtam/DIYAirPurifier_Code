#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include <deque>
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2
#define OUTPUT 1
#define A0 18
#define A1 19
#define _BV(n) (1U << (n))
#define constrain(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define F(s) s
using __FlashStringHelper = char;
inline uint32_t fakeNow = 0;
inline int fakePins[64] = {};
inline int fakeModes[64] = {};
inline uint32_t millis() { return fakeNow; }
inline void delay(uint32_t ms) { fakeNow += ms; }
inline void delayMicroseconds(uint32_t) {}
inline void pinMode(uint8_t pin, int mode) { fakeModes[pin]=mode; if(mode==INPUT_PULLUP) fakePins[pin]=HIGH; }
inline int digitalRead(uint8_t pin) { return fakePins[pin]; }
inline void digitalWrite(uint8_t pin, int level) { fakePins[pin]=level; }
inline int digitalPinToInterrupt(uint8_t pin) { return pin; }
inline void attachInterrupt(int,void(*)(),int) {}
inline void detachInterrupt(int) {}
struct FakeSerial {
  std::deque<uint8_t> input;
  std::string output;
  void begin(int) {}
  int available() { return (int)input.size(); }
  int read() { if(input.empty()) return -1; auto b=input.front();input.pop_front();return b; }
  void print(const char *s) { output+=s; }
  void println(const char *s) { print(s);output+='\n'; }
  explicit operator bool() const { return true; }
};
inline FakeSerial Serial1,Serial;
inline volatile uint8_t TCCR1A=0,TCCR1B=0,TCCR3A=0,TCCR3B=0,TCCR3C=0;
inline volatile uint8_t ADCSRA=0,MCUSR=0,WDTCSR=0;
inline volatile uint16_t TCNT1=0,TCNT3=0,ICR1=0,ICR3=0,OCR1B=0,OCR3A=0;
enum { COM1B1=5,WGM11=1,WGM12=3,WGM13=4,CS10=0,COM3A1=7,WGM31=1,WGM32=3,WGM33=4,CS30=0,ADEN=7,WDRF=3,WDCE=4,WDE=3,WDIE=6,WDP3=5,WDP0=0 };
