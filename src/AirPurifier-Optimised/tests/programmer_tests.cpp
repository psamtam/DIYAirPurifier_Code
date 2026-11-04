#include <stdio.h>
#include <stdlib.h>
#include "ProgrammerUnderTest.cpp"
static int checks=0;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main() {
  for(int i=0;i<256;++i) Wire.memory[i]=i;
  CHECK(programUiAssets());
  for(int i=0;i<256;++i) CHECK(Wire.memory[i]==i);
  for(size_t i=0;i<sizeof(UI_IMAGE);++i) CHECK(Wire.memory[256+i]==UI_IMAGE[i]);
  for(auto address:Wire.writes) CHECK(address>=256&&address<256+sizeof(UI_IMAGE));
  uint8_t bytes[28]={0};
  CHECK(!writeBlock(255,bytes,1));CHECK(!writeBlock(4095,bytes,2));CHECK(!readBlock(4095,bytes,2));
  CHECK(!readBlock(256,bytes,29));
  Wire.writesUntilFailure=4;CHECK(!programUiAssets());CHECK(Wire.memory[256]==0&&Wire.memory[257]==0);
  for(int i=0;i<256;++i) CHECK(Wire.memory[i]==i);
  Wire.writesUntilFailure=-1;Wire.corruptWrites=true;CHECK(!programUiAssets());
  Wire.corruptWrites=false;CHECK(programUiAssets());CHECK(verifyPart(0,sizeof(UI_IMAGE)));
  Wire.fail=true;CHECK(!programUiAssets());Wire.fail=false;
  setup();CHECK(programmingSucceeded);CHECK(fakePins[8]==HIGH);CHECK(fakePins[5]==LOW);
  CHECK(fakePins[4]==LOW&&fakePins[10]==LOW);CHECK(Wire.timeout==25000);
  CHECK(lcd.rows[1].find("UI load verified")==0);
  printf("PASS: %d EEPROM programmer assertions\n",checks);
}
