#pragma once
#include "Arduino.h"
struct FakeWire {
  std::vector<uint8_t> memory = std::vector<uint8_t>(4096,0xff);
  std::vector<uint8_t> tx,rx,ahtData;
  std::vector<uint8_t> bmpRegisters=std::vector<uint8_t>(256,0);
  std::vector<uint16_t> writes;
  uint8_t address=0;
  uint16_t pointer=0;
  size_t readIndex=0;
  bool fail=false,timeoutFlag=false,corruptWrites=false;
  bool bmpPresent=false;
  int writesUntilFailure=-1;
  uint32_t timeout=0;
  void begin() {}
  void setClock(uint32_t) {}
  void setWireTimeout(uint32_t n,bool) { timeout=n; }
  bool getWireTimeoutFlag() { return timeoutFlag; }
  void clearWireTimeoutFlag() { timeoutFlag=false; }
  void beginTransmission(uint8_t a) { address=a;tx.clear(); }
  size_t write(uint8_t b) { tx.push_back(b);return 1; }
  uint8_t endTransmission(bool=true) {
    if(fail) { timeoutFlag=true;return 5; }
    if(address==0x38) return 0;
    if(address==0x76 && bmpPresent) {
      if(!tx.empty()) pointer=tx[0];
      for(size_t i=1;i<tx.size();++i) bmpRegisters[pointer++]=tx[i];
      return 0;
    }
    if(address!=0x57) return 2;
    if(tx.size()<2) return 0;
    pointer=((uint16_t)tx[0]<<8)|tx[1];
    if(tx.size()>2) {
      if(writesUntilFailure==0) return 4;
      if(writesUntilFailure>0) --writesUntilFailure;
      if(tx.size()>32 || (pointer%32)+(tx.size()-2)>32 || pointer+tx.size()-2>4096) return 3;
      for(size_t i=2;i<tx.size();++i) { writes.push_back(pointer);memory[pointer++]=tx[i]^(corruptWrites?1:0); }
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t a,uint8_t n) {
    rx.clear();readIndex=0;
    if(fail) {timeoutFlag=true;return 0;}
    if(a==0x38) { rx=ahtData; if(rx.size()>n) rx.resize(n);return (uint8_t)rx.size(); }
    if(a==0x76 && bmpPresent) {
      rx.insert(rx.end(),bmpRegisters.begin()+pointer,bmpRegisters.begin()+pointer+n);pointer+=n;return n;
    }
    if(a!=0x57 || pointer+n>4096) return 0;
    rx.insert(rx.end(),memory.begin()+pointer,memory.begin()+pointer+n);pointer+=n;return n;
  }
  uint8_t requestFrom(int a,int n) { return requestFrom((uint8_t)a,(uint8_t)n); }
  int available() { return (int)(rx.size()-readIndex); }
  int read() { return readIndex<rx.size()?rx[readIndex++]:-1; }
};
inline FakeWire Wire;
