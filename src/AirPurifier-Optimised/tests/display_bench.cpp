#include <stdio.h>
#include <stdlib.h>
#include "FirmwareUnderTest.cpp"
volatile unsigned long timer0_millis=0;
int main(int argc,char **argv) {
  if(argc!=2) return 2;
  FILE *f=fopen(argv[1],"rb");if(!f) return 3;
  fread(Wire.memory.data()+256,1,3840,f);fclose(f);
  fakeNow=1000;for(int &p:fakePins) p=HIGH;
  initialiseButton(button1);initialiseButton(button2);initialiseButton(button3);
  previousEncoderState=3;setDefaultConfig();
  config.operatingSensorIntervalSeconds=60;config.mainScreenRotationSeconds=60;config.backlightTimeoutSeconds=300;
  ahtOK=bmpOK=ahtReadingValid=bmpReadingValid=true;rtcOK=false;
  temperatureC=230;humidityPercent=500;pressureHpa=10065;
  lastAhtSuccessMs=lastBmpSuccessMs=fakeNow;
  lcdOK=uiAssetsOK=true;lcd.init();powerState=OPERATING;uiState=MAIN_DISPLAY;mainDisplayPage=0;
  lastOperatingSensorMs=lastMainRotationMs=lastBacklightActivityMs=fakeNow;
  renderDisplay();uint32_t initialChars=lcd.characterWrites,initialCursors=lcd.cursorWrites;
  uint32_t chars=lcd.characterWrites,cursors=lcd.cursorWrites,reads=Wire.rxCount[0x57];
  for(int i=0;i<16;++i) {fakeNow+=250;loop();}
  uint32_t idleChars=lcd.characterWrites-chars,idleCursors=lcd.cursorWrites-cursors,idleReads=Wire.rxCount[0x57]-reads;
  chars=lcd.characterWrites;cursors=lcd.cursorWrites;reads=Wire.rxCount[0x57];
  temperatureC=220;humidityPercent=500;pressureHpa=10065;ahtReadingValid=bmpReadingValid=true;
  lastAhtSuccessMs=lastBmpSuccessMs=fakeNow;renderDisplay();
  printf("{\"initial_chars\":%u,\"initial_cursors\":%u,\"idle_4s_chars\":%u,\"idle_4s_cursors\":%u,\"idle_4s_eeprom_reads\":%u,\"changed_chars\":%u,\"changed_cursors\":%u,\"changed_eeprom_reads\":%u}\n",
    initialChars,initialCursors,idleChars,idleCursors,idleReads,lcd.characterWrites-chars,lcd.cursorWrites-cursors,Wire.rxCount[0x57]-reads);
}
