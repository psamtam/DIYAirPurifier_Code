#pragma once
#include "Wire.h"
struct LiquidCrystal_I2C {
  std::string rows[4] = {std::string(20,' '),std::string(20,' '),std::string(20,' '),std::string(20,' ')};
  int row=0,col=0;
  bool light=false;
  bool timeoutOnWrite=false;
  LiquidCrystal_I2C(int,int,int) {}
  void init() {}
  void backlight() { light=true; }
  void noBacklight() { light=false; }
  void setCursor(int c,int r) {col=c;row=r;}
  void print(char c) {if(timeoutOnWrite) Wire.timeoutFlag=true;if(row>=0&&row<4&&col>=0&&col<20) rows[row][col]=c; ++col;}
  void print(const char *s) {while(*s) print(*s++);}
};
