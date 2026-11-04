#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "FirmwareUnderTest.cpp"
volatile unsigned long timer0_millis=0;
static int checks=0;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)

void resetFirmware() {
  fakeNow=1000; Wire.fail=false;Wire.timeoutFlag=false;Wire.writes.clear();
  for(int &p:fakePins) p=HIGH;
  setDefaultConfig();powerState=OPERATING;uiState=MAIN_DISPLAY;
  fanSpeed=50;ledBrightness=60;delayTimers[0]=DelayOffTimer();delayTimers[1]=DelayOffTimer();
  delaySelections[0]=delaySelections[1]=30;delayEditTarget=0;delayEditMinutes=30;
  delayOutputOffMessage=false;lcdOK=true;lcdBacklightOn=true;rtcOK=ahtOK=bmpOK=false;
  lcd.timeoutOnWrite=false;
  ahtMeasurementPending=false;menuEditing=dateEditing=false;menuPage=0;
  button3WakeOnlyUntilRelease=false;
  initialiseButton(button1);initialiseButton(button2);initialiseButton(button3);
  lastOperatingSensorMs=lastMainRotationMs=lastBacklightActivityMs=lastUiActivityMs=lastMenuActivityMs=fakeNow;
  outputsOffSinceMs=0;encoderDetentsPending=0;applyOutputs();
}
void edge(uint8_t pin,bool down) {
  fakePins[pin]=down?LOW:HIGH;processButtons();fakeNow+=BUTTON_DEBOUNCE_MS;processButtons();
}
void tap(uint8_t pin) {edge(pin,true);edge(pin,false);}
void hold(uint8_t pin) {edge(pin,true);fakeNow+=2000;processButtons();edge(pin,false);}

int main(int argc,char **argv) {
  CHECK(argc==2);
  FILE *f=fopen(argv[1],"rb");CHECK(f);
  auto n=fread(Wire.memory.data()+256,1,3840,f);fclose(f);CHECK(n>8);
  resetFirmware();
  CHECK(validateUiAssets());
  uint8_t saved=Wire.memory[264];Wire.memory[264]^=1;CHECK(!validateUiAssets());Wire.memory[264]=saved;
  Wire.memory[260]^=1;CHECK(!validateUiAssets());Wire.memory[260]^=1;
  Wire.memory[262]^=1;CHECK(!validateUiAssets());Wire.memory[262]^=1;
  Wire.memory[256]^=1;CHECK(!validateUiAssets());Wire.memory[256]^=1;
  Wire.fail=true;CHECK(!validateUiAssets());CHECK(strcmp(uiText(0),"TEXT ERR")==0);Wire.fail=false;
  // Every catalogue entry is read and terminated; the date-name table must not alias.
  for(uint8_t i=0;i<UI_TEXT_COUNT;++i) CHECK(strcmp(uiText(i),(char*)Wire.memory.data()+264+(int)i*32)==0);
  rtcOK=true;dateEditing=true;dateEditField=0;drawDateEditor();CHECK(lcd.rows[1].find("Set Year")==0);
  dateEditField=5;drawDateEditor();CHECK(lcd.rows[1].find("Set Second")==0);
  resetFirmware();
  // No short action on press; one on release. Holding in adjustment does not cycle.
  edge(PIN_BUTTON1,true);CHECK(uiState==MAIN_DISPLAY);edge(PIN_BUTTON1,false);CHECK(uiState==FAN_ADJUSTMENT);
  hold(PIN_BUTTON1);CHECK(uiState==DELAY_EDIT);CHECK(fanSpeed==50);CHECK(!delayTimers[0].active);
  CHECK(delayEditMinutes==30);tap(PIN_BUTTON1);CHECK(delayTimers[0].active);CHECK(uiState==MAIN_DISPLAY);
  CHECK(delayTimers[0].minutes==30);CHECK(Wire.writes.empty());
  hold(PIN_BUTTON2);CHECK(delayEditTarget==1);delayEditMinutes=5;tap(PIN_BUTTON2);
  CHECK(delayTimers[0].active&&delayTimers[1].active);CHECK(delayTimers[1].minutes==5);
  uint32_t fanStart=delayTimers[0].startedAtMs;
  uiState=FAN_ADJUSTMENT;adjustActiveOutput(10);CHECK(fanSpeed==60);CHECK(delayTimers[0].startedAtMs==fanStart);
  // One timer expires while the other remains armed; LED PWM is disconnected.
  fakeNow=delayTimers[1].startedAtMs+300000;checkDelayTimers(fakeNow);
  CHECK(ledBrightness==0&&!delayTimers[1].active);CHECK(fanSpeed==60&&delayTimers[0].active);
  CHECK(!(TCCR3A&_BV(COM3A1)));CHECK(fakePins[PIN_LED]==LOW);
  fakeNow=fanStart+1800000;checkDelayTimers(fakeNow);CHECK(fanSpeed==0&&!fanRelayOn);
  CHECK(!delayTimers[0].active);
  resetFirmware();
  delayTimers[0].set(fakeNow,30);fanStart=fakeNow;fakeNow+=60000;
  beginDelayEdit(0);CHECK(delayEditMinutes==30);delayEditMinutes=5;handleDelayButton(1);
  CHECK(delayTimers[0].startedAtMs==fanStart&&delayTimers[0].minutes==30);
  beginDelayEdit(0);delayEditMinutes=5;handleButton3Short();CHECK(delayTimers[0].startedAtMs==fanStart);
  beginDelayEdit(0);fakeNow+=10000;checkTimers(fakeNow);CHECK(uiState==MAIN_DISPLAY);CHECK(delayTimers[0].startedAtMs==fanStart);
  beginDelayEdit(0);delayEditMinutes=10;handleDelayButton(0);CHECK(delayTimers[0].startedAtMs==fakeNow&&delayTimers[0].minutes==10);
  beginDelayEdit(0);delayEditMinutes=0;handleDelayButton(0);CHECK(!delayTimers[0].active&&fanSpeed==50);CHECK(delaySelections[0]==0);
  beginDelayEdit(0);CHECK(delayEditMinutes==0);delayEditMinutes=5;fanSpeed=0;applyFanOutput();handleDelayButton(0);
  CHECK(!delayTimers[0].active&&delayOutputOffMessage&&uiState==DELAY_EDIT);renderDelayEditor();CHECK(lcd.rows[2].find("Output is off")==0);
  delayEditMinutes=0;renderDelayEditor();CHECK(lcd.rows[1].find("Delay: Off")==0);
  resetFirmware();delayTimers[0].set(fakeNow,5);delayTimers[1].set(fakeNow,5);
  uiState=FAN_ADJUSTMENT;fanSpeed=1;adjustActiveOutput(-1);CHECK(!delayTimers[0].active&&delayTimers[1].active);
  uiState=LED_ADJUSTMENT;ledBrightness=1;adjustActiveOutput(-1);CHECK(!delayTimers[1].active);
  resetFirmware();delayTimers[0].set(fakeNow,5);delayTimers[1].set(fakeNow,5);
  fakeNow+=300000;checkTimers(fakeNow);CHECK(fanSpeed==0&&ledBrightness==0&&powerState==OPERATING);
  fakeNow+=300000;checkTimers(fakeNow);CHECK(powerState==STANDBY);
  resetFirmware();delayTimers[0].set(fakeNow,5);delayTimers[1].set(fakeNow,5);enterStandby(false);
  CHECK(!delayTimers[0].active&&!delayTimers[1].active);wakeFromStandby();CHECK(fanSpeed==50&&ledBrightness==50);
  resetFirmware();delayTimers[0].set(fakeNow,5);beginDelayEdit(1);hold(PIN_BUTTON3);
  CHECK(powerState==STANDBY&&!delayTimers[0].active&&uiState==MAIN_DISPLAY);
  resetFirmware();setBacklight(false);hold(PIN_BUTTON1);CHECK(uiState==MAIN_DISPLAY&&lcdBacklightOn);tap(PIN_BUTTON1);CHECK(uiState==FAN_ADJUSTMENT);
  resetFirmware();setBacklight(false);hold(PIN_BUTTON2);CHECK(uiState==MAIN_DISPLAY&&lcdBacklightOn);
  resetFirmware();setBacklight(false);hold(PIN_BUTTON3);CHECK(uiState==MAIN_DISPLAY&&powerState==OPERATING);
  resetFirmware();tap(PIN_BUTTON3);CHECK(uiState==MENU);hold(PIN_BUTTON1);CHECK(menuPage==8&&dateEditing==false);CHECK(uiState==MENU);
  tap(PIN_BUTTON2);CHECK(menuPage==0);tap(PIN_BUTTON2);CHECK(uiState==MAIN_DISPLAY);
  resetFirmware();beginDelayEdit(0);encoderDetentsPending=-32768;processEncoder();CHECK(delayEditMinutes==0);
  encoderDetentsPending=32767;processEncoder();CHECK(delayEditMinutes==240);
  encoderDetentsPending=-1;processEncoder();CHECK(delayEditMinutes==235);
  for(int v=0;v<=240;v+=5) {CHECK(stepDelayMinutes(v,1)==(v==240?240:v+5));CHECK(stepDelayMinutes(v,-1)==(v==0?0:v-5));}
  DelayOffTimer timer;uint32_t start=0xffff0000UL;timer.set(start,240);
  CHECK(timer.remainingMinutes(start)==240);CHECK(timer.remainingMinutes(start+1)==240);
  CHECK(timer.remainingMinutes(start+60000)==239);CHECK(!timer.expired(start+14399999UL));
  CHECK(timer.expired(start+14400000UL));CHECK(timer.remainingMinutes(start+14400000UL)==0);
  // Integer AHT conversion compared with the physical transfer function.
  uint32_t raws[]={0,1,31,262144,524287,524288,786432,1048574,1048575};
  for(auto rh:raws) for(auto rt:raws) {
    Wire.ahtData={0,(uint8_t)(rh>>12),(uint8_t)(rh>>4),(uint8_t)((rh<<4)|(rt>>16)),(uint8_t)(rt>>8),(uint8_t)rt};
    int16_t t,h;CHECK(aht.finishMeasurement(t,h));
    CHECK(fabs(h/10.0-rh*100.0/1048576.0)<=0.050001);
    CHECK(fabs(t/10.0-(rt*200.0/1048576.0-50))<=0.050001);
  }
  char formatted[16];formatReading(formatted,sizeof(formatted),-1);CHECK(strcmp(formatted,"-0.1")==0);
  formatReading(formatted,sizeof(formatted),-500);CHECK(strcmp(formatted,"-50.0")==0);
  formatReading(formatted,sizeof(formatted),1000);CHECK(strcmp(formatted,"100.0")==0);
  // Bosch's published calibration/raw sample: 25.08 C and 100653.25 Pa.
  Wire.bmpPresent=true;Wire.bmpRegisters[0xD0]=0x58;
  int coefficients[]={27504,26435,-1000,36477,-10685,3024,2855,140,-7,15500,-14600,6000};
  for(int i=0;i<12;++i) {Wire.bmpRegisters[0x88+i*2]=(uint16_t)coefficients[i]&255;Wire.bmpRegisters[0x89+i*2]=(uint16_t)coefficients[i]>>8;}
  CHECK(bmp.begin(0x76));
  auto putRaw=[](int reg,int raw){Wire.bmpRegisters[reg]=raw>>12;Wire.bmpRegisters[reg+1]=raw>>4;Wire.bmpRegisters[reg+2]=raw<<4;};
  putRaw(0xF7,415148);putRaw(0xFA,519888);int16_t bt,bp;CHECK(bmp.read(bt,bp));CHECK(bt==251);CHECK(bp==10065);
  Wire.bmpPresent=false;
  // Existing config remains in the reserved area; asset bytes survive saving/reset.
  auto assets=std::vector<uint8_t>(Wire.memory.begin()+256,Wire.memory.end());
  resetFirmware();CHECK(saveConfig());CHECK(loadConfig());CHECK(saveTimeFormat());
  for(auto address:Wire.writes) CHECK(address<256);
  CHECK(std::equal(assets.begin(),assets.end(),Wire.memory.begin()+256));
  resetFirmware();menuPage=9;handleMenuButton1();CHECK(std::equal(assets.begin(),assets.end(),Wire.memory.begin()+256));
  // Runtime text failure does not stop a due output-off action.
  resetFirmware();delayTimers[0].set(fakeNow,5);Wire.fail=true;CHECK(strcmp(uiText(0),"TEXT ERR")==0);
  fakeNow+=300000;checkDelayTimers(fakeNow);CHECK(fanSpeed==0&&!fanRelayOn);Wire.fail=false;
  resetFirmware();lcd.timeoutOnWrite=true;lcdLine(0,"test");CHECK(!lcdOK);
  delayTimers[1].set(fakeNow,5);fakeNow+=300000;checkTimers(fakeNow);CHECK(ledBrightness==0);
  // No valid assets: setup never enables outputs; normal loop stays inert.
  resetFirmware();Wire.memory[264]^=1;setup();CHECK(!uiAssetsOK&&fanRelayOn==false&&fakePins[PIN_LED]==LOW);
  loop();CHECK(!fanRelayOn);Wire.memory[264]^=1;CHECK(Wire.timeout==25000);
  resetFirmware();CHECK(delaySelections[0]==30&&delaySelections[1]==30&&!delayTimers[0].active);
  printf("PASS: %d firmware assertions\n",checks);
}
