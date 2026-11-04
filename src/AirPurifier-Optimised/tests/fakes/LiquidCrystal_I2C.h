#pragma once
#include "Wire.h"
struct LiquidCrystal_I2C {
  std::string rows[4] = {std::string(20,' '),std::string(20,' '),std::string(20,' '),std::string(20,' ')};
  int row=0,col=0;
  bool light=false;
  bool timeoutOnWrite=false;
  uint32_t characterWrites=0,cursorWrites=0;
  LiquidCrystal_I2C(int,int,int) {}
  void init() { for(auto &s:rows) s.assign(20,' '); }
  void backlight() { light=true; }
  void noBacklight() { light=false; }
  void setCursor(int c,int r) {++cursorWrites;col=c;row=r;}
  void print(char c) {++characterWrites;if(timeoutOnWrite) Wire.timeoutFlag=true;if(row>=0&&row<4&&col>=0&&col<20) rows[row][col]=c; ++col;}
  void print(const char *s) {while(*s) print(*s++);}
  size_t write(uint8_t c) {print((char)c);return 1;}
};
