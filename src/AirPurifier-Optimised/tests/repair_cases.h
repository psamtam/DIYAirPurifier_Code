// Uses the actual production implementation included by firmware_tests.cpp.
void prepareBmp(uint8_t address=0x76) {
  Wire.bmpPresent=true;Wire.bmpAddress=address;
  Wire.bmpRegisters.assign(256,0);Wire.bmpRegisters[0xD0]=0x58;
  int c[]={27504,26435,-1000,36477,-10685,3024,2855,140,-7,15500,-14600,6000};
  for(int i=0;i<12;++i) {Wire.bmpRegisters[0x88+i*2]=(uint16_t)c[i]&255;Wire.bmpRegisters[0x89+i*2]=(uint16_t)c[i]>>8;}
  auto raw=[](int reg,int value){Wire.bmpRegisters[reg]=value>>12;Wire.bmpRegisters[reg+1]=value>>4;Wire.bmpRegisters[reg+2]=value<<4;};
  raw(0xF7,415148);raw(0xFA,519888);
}
void recoveryPass() {pollSensorRecovery(fakeNow);pollSensorRecovery(fakeNow);}
void clockEdit() {Wire.rtcPresent=true;rtcOK=true;uiState=MENU;menuPage=1;submenuIndex=3;beginConfigEdit();handleMenuEncoder(1);}

void testRepairs() {
  // All cancellation routes restore both clock format and configuration drafts.
  resetFirmware();CHECK(saveTimeFormat());Wire.writes.clear();clockEdit();CHECK(!use24HourClock);
  handleMenuButton2();CHECK(use24HourClock&&!menuEditing&&Wire.writes.empty());CHECK(Wire.rtcTimeWrites==0);
  resetFirmware();clockEdit();handleMenuButton3Short();CHECK(use24HourClock&&!menuEditing&&uiState==MAIN_DISPLAY);
  resetFirmware();clockEdit();fakeNow+=60000;checkTimers(fakeNow);CHECK(use24HourClock&&!menuEditing);
  resetFirmware();clockEdit();enterStandby(true);CHECK(use24HourClock&&!menuEditing&&powerState==STANDBY);CHECK(loadTimeFormat()&&use24HourClock);
  resetFirmware();clockEdit();handleMenuButton1();CHECK(!menuEditing&&!use24HourClock);use24HourClock=true;CHECK(loadTimeFormat()&&!use24HourClock);CHECK(Wire.rtcTimeWrites==0);
  resetFirmware();clockEdit();Wire.writesUntilFailure=0;handleMenuButton1();CHECK(menuEditing&&clockFormatSaveFailed&&!use24HourClock);
  renderDisplay();CHECK(lcd.rows[3].find("ERR")==0);
  Wire.writesUntilFailure=-1;handleMenuButton1();CHECK(!menuEditing&&!clockFormatSaveFailed);CHECK(loadTimeFormat()&&!use24HourClock);
  resetFirmware();clockEdit();Wire.corruptWrites=true;handleMenuButton1();CHECK(menuEditing&&clockFormatSaveFailed);
  Wire.corruptWrites=false;handleMenuButton2();CHECK(use24HourClock&&!menuEditing);
  resetFirmware();use24HourClock=false;CHECK(saveTimeFormat());clockEdit();CHECK(use24HourClock);
  handleMenuButton2();CHECK(!use24HourClock);CHECK(loadTimeFormat()&&!use24HourClock);
  clockEdit();handleMenuButton1();CHECK(use24HourClock&&!menuEditing);use24HourClock=false;CHECK(loadTimeFormat()&&use24HourClock);
  resetFirmware();uiState=MENU;menuPage=1;beginConfigEdit();setSettingValue(SET_BACKLIGHT,90);handleMenuButton3Short();CHECK(config.backlightTimeoutSeconds==30);
  CHECK(saveConfig());config.backlightTimeoutSeconds=100;CHECK(loadConfig()&&config.backlightTimeoutSeconds==30);
  resetFirmware();setPmsPower(true);for(int i=0;i<5;++i) addPmSample(10,20,30);
  uint32_t enabledAt=pmsEnabledAtMs;clockEdit();handleMenuButton2();CHECK(pmBufferCount==5&&pmsEnabledAtMs==enabledAt&&pmsPowered);
  lastOperatingSensorMs=500;clockEdit();handleMenuButton2();CHECK(lastOperatingSensorMs==500);
  uiState=MENU;menuPage=2;submenuIndex=0;beginConfigEdit();setSettingValue(SET_PMS_ENABLE,0);handleMenuButton2();CHECK(config.pmsEnabled&&pmsPowered);
  // A deliberate Date & Time confirmation still writes RTC time.
  resetFirmware();Wire.rtcPresent=true;rtcOK=true;uiState=MENU;menuPage=8;dateEditing=true;dateEditField=5;
  editYear=2026;editMonth=11;editDay=4;editHour=13;editMinute=45;editSecond=12;
  handleMenuButton1();CHECK(Wire.rtcTimeWrites==1);CHECK(rtc.now().hour()==13&&rtc.now().minute()==45);
  // Table-driven validation preserves every setting's bounds and encoding.
  resetFirmware();for(int id=0;id<8;++id) {
    SettingId setting=(SettingId)id;
    setSettingValue(setting,-1);CHECK(getSettingValue(setting)==settingMin(setting));
    setSettingValue(setting,2000);CHECK(getSettingValue(setting)==settingMax(setting));
    CHECK(configValid(config));
  }
  Config bad=config;bad.pmAverageWindow=0;CHECK(!configValid(bad));bad=config;bad.standbySensorIntervalSeconds=9;CHECK(!configValid(bad));
  // Missing devices are rate-limited; no new delays are introduced by recovery.
  resetFirmware();Wire.ahtPresent=false;Wire.bmpPresent=false;
  sensorAttemptMs[0]=sensorAttemptMs[1]=fakeNow-SENSOR_RETRY_MS;
  uint32_t ahtTx=Wire.txCount[0x38],bmpTx=Wire.txCount[0x76]+Wire.txCount[0x77],at=fakeNow;
  recoveryPass();CHECK(fakeNow==at&&!ahtOK&&!bmpOK&&ahtInitPhase==0&&!bmpSettling);
  CHECK(Wire.txCount[0x38]==ahtTx+1);CHECK(Wire.txCount[0x76]+Wire.txCount[0x77]==bmpTx+2);
  for(int i=0;i<100;++i) recoveryPass();CHECK(Wire.txCount[0x38]==ahtTx+1);
  fakeNow+=9999;recoveryPass();CHECK(Wire.txCount[0x38]==ahtTx+1);
  ++fakeNow;recoveryPass();CHECK(Wire.txCount[0x38]==ahtTx+2);
  // Calibrated AHT reconnect: reset, timed wait, then fresh conversion.
  Wire.ahtPresent=true;fakeNow+=10000;recoveryPass();CHECK(ahtInitPhase==1&&!ahtOK);
  uint32_t rx=Wire.rxCount[0x38];fakeNow+=20;recoveryPass();CHECK(Wire.rxCount[0x38]==rx);
  ++fakeNow;recoveryPass();CHECK(ahtOK&&environmentalReadDue&&!ahtReadingValid);
  Wire.ahtData={8,0x80,0,0x08,0,0};checkTimers(fakeNow);CHECK(ahtMeasurementPending);
  fakeNow+=90;pollAhtMeasurement(fakeNow);CHECK(ahtReadingValid&&temperatureC==500&&humidityPercent==500);
  // Uncalibrated sensor has a second non-blocking calibration wait.
  resetFirmware();Wire.ahtStatus=0;sensorAttemptMs[0]=fakeNow-SENSOR_RETRY_MS;at=fakeNow;
  recoveryPass();CHECK(ahtInitPhase==1&&fakeNow==at);fakeNow+=21;recoveryPass();CHECK(ahtInitPhase==2&&!ahtOK);
  fakeNow+=10;recoveryPass();CHECK(!ahtOK);++fakeNow;recoveryPass();CHECK(ahtOK&&ahtInitPhase==0);
  resetFirmware();Wire.ahtStatus=0;sensorAttemptMs[0]=fakeNow-SENSOR_RETRY_MS;recoveryPass();fakeNow+=21;recoveryPass();Wire.ahtStatus=0;fakeNow+=11;recoveryPass();CHECK(!ahtOK&&ahtInitPhase==0);
  resetFirmware();sensorAttemptMs[0]=fakeNow-SENSOR_RETRY_MS;recoveryPass();Wire.ahtStatus=0;Wire.failAhtCommands=true;
  fakeNow+=21;recoveryPass();CHECK(!ahtOK&&ahtInitPhase==0&&sensorAttemptMs[0]==fakeNow);
  resetFirmware();sensorAttemptMs[0]=fakeNow-SENSOR_RETRY_MS;recoveryPass();Wire.ahtStatus=0x80;fakeNow+=21;recoveryPass();CHECK(!ahtOK&&ahtInitPhase==0);
  // Communication time is not subtracted from the required post-command wait.
  resetFirmware();Wire.ahtCommandDelayMs=3;sensorAttemptMs[0]=fakeNow-SENSOR_RETRY_MS;at=fakeNow;
  recoveryPass();CHECK(fakeNow==at+3&&sensorAttemptMs[0]==fakeNow);
  fakeNow+=20;recoveryPass();CHECK(ahtInitPhase==1&&!ahtOK);++fakeNow;recoveryPass();CHECK(ahtOK);
  resetFirmware();Wire.ahtCommandDelayMs=3;Wire.ahtStatus=0;sensorAttemptMs[0]=fakeNow-SENSOR_RETRY_MS;
  recoveryPass();fakeNow+=21;recoveryPass();CHECK(ahtInitPhase==2&&sensorAttemptMs[0]==fakeNow);
  fakeNow+=10;recoveryPass();CHECK(!ahtOK);++fakeNow;recoveryPass();CHECK(ahtOK);
  resetFirmware();Wire.ahtCommandDelayMs=3;ahtOK=true;readEnvironmentalSensors();CHECK(ahtMeasurementStartedMs==fakeNow);
  Wire.ahtData={8,0,0,0,0,0};fakeNow+=89;pollAhtMeasurement(fakeNow);CHECK(ahtMeasurementPending&&!ahtReadingValid);
  ++fakeNow;pollAhtMeasurement(fakeNow);CHECK(ahtReadingValid&&!ahtMeasurementPending);
  // Measurement failure discards pending work and starts a new retry interval.
  resetFirmware();ahtOK=ahtReadingValid=ahtMeasurementPending=true;Wire.ahtPresent=false;
  ahtMeasurementStartedMs=fakeNow-90;pollAhtMeasurement(fakeNow);CHECK(!ahtOK&&!ahtReadingValid&&!ahtMeasurementPending&&sensorAttemptMs[0]==fakeNow);
  // BMP reconnect at either address; reject NVM busy and failed config writes.
  for(uint8_t address: {uint8_t(0x76),uint8_t(0x77)}) {
    resetFirmware();prepareBmp(address);sensorAttemptMs[1]=fakeNow-SENSOR_RETRY_MS;at=fakeNow;
    recoveryPass();CHECK(bmpSettling&&!bmpOK&&fakeNow==at);
    fakeNow+=99;recoveryPass();CHECK(!bmpOK);++fakeNow;recoveryPass();CHECK(bmpOK&&environmentalReadDue&&!bmpReadingValid);
    checkTimers(fakeNow);CHECK(bmpReadingValid&&pressureHpa==10065);
  }
  resetFirmware();prepareBmp();Wire.failBmpWrites=true;sensorAttemptMs[1]=fakeNow-SENSOR_RETRY_MS;recoveryPass();CHECK(!bmpOK&&!bmpSettling);
  resetFirmware();prepareBmp();Wire.ignoreBmpWrites=true;sensorAttemptMs[1]=fakeNow-SENSOR_RETRY_MS;recoveryPass();CHECK(!bmpOK&&!bmpSettling);
  resetFirmware();prepareBmp();Wire.bmpRegisters[0xF3]=1;sensorAttemptMs[1]=fakeNow-SENSOR_RETRY_MS;recoveryPass();CHECK(!bmpOK&&!bmpSettling);
  resetFirmware();prepareBmp();CHECK(bmp.begin(0x76));bmpOK=bmpReadingValid=true;Wire.bmpPresent=false;readEnvironmentalSensors();CHECK(!bmpOK&&!bmpReadingValid);
  resetFirmware();prepareBmp();CHECK(bmp.begin(0x76));bmpOK=bmpReadingValid=true;Wire.bmpRegisters[0xF4]=0;readEnvironmentalSensors();CHECK(!bmpOK&&!bmpReadingValid);
  resetFirmware();ahtOK=bmpOK=true;ahtTx=Wire.txCount[0x38];bmpTx=Wire.txCount[0x76]+Wire.txCount[0x77];
  for(int i=0;i<50;++i) {fakeNow+=1000;recoveryPass();}
  CHECK(Wire.txCount[0x38]==ahtTx&&Wire.txCount[0x76]+Wire.txCount[0x77]==bmpTx);
  resetFirmware();ahtOK=true;prepareBmp();sensorAttemptMs[1]=fakeNow-SENSOR_RETRY_MS;ahtTx=Wire.txCount[0x38];recoveryPass();CHECK(Wire.txCount[0x38]==ahtTx);
  // Full-width retry rollover and standby watchdog-sized progression.
  resetFirmware();fakeNow=0xfffffff0UL;Wire.ahtPresent=false;sensorAttemptMs[0]=fakeNow;fakeNow+=10000;recoveryPass();CHECK(sensorAttemptMs[0]==fakeNow&&!ahtOK);
  resetFirmware();powerState=STANDBY;sensorAttemptMs[0]=fakeNow-SENSOR_RETRY_MS;at=fakeNow;recoveryPass();CHECK(fakeNow==at&&ahtInitPhase==1);
  fakeNow+=8000;at=fakeNow;recoveryPass();CHECK(ahtOK&&environmentalReadDue&&fakeNow==at&&powerState==STANDBY);
  // Both controls continue running while a sensor is waiting to initialise.
  resetFirmware();sensorAttemptMs[0]=fakeNow-SENSOR_RETRY_MS;recoveryPass();hold(PIN_BUTTON1);CHECK(uiState==DELAY_EDIT);
  delayTimers[1].set(fakeNow-300000,5);checkTimers(fakeNow);CHECK(ledBrightness==0);
  // Short timing remains correct across 16-bit and full millis rollovers.
  for(uint32_t origin: {uint32_t(65520),uint32_t(0xfffffff0UL)}) {
    resetFirmware();fakeNow=origin;hold(PIN_BUTTON1);CHECK(uiState==DELAY_EDIT&&fanSpeed==50);
    fakeNow+=70000;processButtons();CHECK(uiState==DELAY_EDIT);
  }
  resetFirmware();edge(PIN_BUTTON1,true);fakeNow+=2000;processButtons();CHECK(uiState==DELAY_EDIT);
  fakeNow+=70000;processButtons();CHECK(uiState==DELAY_EDIT&&!delayTimers[0].active);edge(PIN_BUTTON1,false);CHECK(!delayTimers[0].active);
  // PWM mapping is bit-for-bit identical for all selectable levels.
  for(unsigned p=0;p<=100;++p) {
    setFanPWM(p);CHECK(OCR1B==(p==100?639:p*640/100));
    setLedPWM(p);CHECK(!p ? !(TCCR3A&_BV(COM3A1)) : OCR3A==p*639/100);
  }
  // The smaller AHT scaling is exactly equivalent across its complete raw domain.
  bool equivalent=true;
  for(uint32_t raw=0;raw<1048576;++raw) {
    if(((ahtQuarterUnits(raw)+2U)>>2)!=((raw*1000UL+524288UL)>>20) ||
       ((ahtQuarterUnits(raw)+1U)>>1)!=((raw*2000UL+524288UL)>>20)) {equivalent=false;break;}
  }
  CHECK(equivalent);
  // Compare the simplified leap rule with Gregorian rules over every accepted
  // year/month and all day-boundary candidates, including invalid day zero.
  bool calendarExact=true;
  const int days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
  for(int y=2000;y<=2099;++y) for(int m=1;m<=12;++m) for(int d=0;d<=32;++d) {
    bool leap=(y%4==0&&(y%100!=0||y%400==0));
    int maximum=days[m-1]+(m==2&&leap?1:0);
    if(validDate(y,m,d,0,0,0)!=(d>=1&&d<=maximum)) calendarExact=false;
  }
  CHECK(calendarExact);
  CHECK(!validDate(1999,1,1,0,0,0));CHECK(!validDate(2100,1,1,0,0,0));
  CHECK(!validDate(2000,0,1,0,0,0));CHECK(!validDate(2000,13,1,0,0,0));
  CHECK(!validDate(2000,1,1,24,0,0));CHECK(!validDate(2000,1,1,0,60,0));
  CHECK(!validDate(2000,1,1,0,0,60));CHECK(!validDate(2000,1,1,-1,0,0));
  // Both clock layouts remain identical at every hour/minute, including noon
  // and midnight. Formatting fits a 20-character LCD row plus terminator.
  for(bool format24: {false,true}) for(unsigned hour=0;hour<24;++hour) for(unsigned minute=0;minute<60;++minute) {
    use24HourClock=format24;char actual[21],time[10];
    formatClockLine(actual,sizeof(actual),2026,11,4,hour,minute);
    unsigned h=hour%12;if(!h)h=12;
    if(format24) snprintf(time,sizeof(time),"%02u:%02u",hour,minute);
    else snprintf(time,sizeof(time),"%u:%02u %s",h,minute,hour>=12?"PM":"AM");
    std::string expected="2026-11-04";expected.append(20-expected.size()-strlen(time),' ');expected+=time;
    CHECK(actual==expected);
  }
  // Character cache preserves full rows, trailing spaces and cursor jumps.
  resetFirmware();lcdLine(0,"longer label");lcdLine(0,"short");CHECK(lcd.rows[0]=="short               ");
  lcdLine(1,"abcdef");lcd.setCursor(1,1);lcd.print("XcY");CHECK(lcd.rows[1].substr(0,6)=="aXcYef");
  lcdLine(0,"one");lcdLine(1,"two");lcdLine(2,"three");lcdLine(3,"four");
  CHECK(lcd.rows[0].find("one")==0&&lcd.rows[1].find("two")==0&&lcd.rows[2].find("three")==0&&lcd.rows[3].find("four")==0);
  // Idle home-screen redraws send no LCD characters or cursor commands.
  resetFirmware();uiState=MAIN_DISPLAY;mainDisplayPage=0;renderDisplay();
  uint32_t chars=lcd.characterWrites,cursors=lcd.cursorWrites,reads=Wire.rxCount[0x57];
  for(int i=0;i<16;++i) renderDisplay();
  CHECK(lcd.characterWrites==chars&&lcd.cursorWrites==cursors);
  uint32_t idleReads=Wire.rxCount[0x57]-reads;
  printf("METRIC cache idle16 LCD_chars=%u cursors=%u EEPROM_reads=%u\n",lcd.characterWrites-chars,lcd.cursorWrites-cursors,idleReads);
  temperatureC=220;humidityPercent=500;pressureHpa=10065;ahtReadingValid=bmpReadingValid=true;lastAhtSuccessMs=lastBmpSuccessMs=fakeNow;
  renderDisplay();CHECK(lcd.characterWrites>chars&&lcd.rows[2].find("22.0")!=std::string::npos);
  printf("METRIC cache changed_frame LCD_chars=%u cursors=%u\n",lcd.characterWrites-chars,lcd.cursorWrites-cursors);
  // Composed value rows avoid clearing and rewriting the same value each frame.
  for(UiState state: {FAN_ADJUSTMENT,LED_ADJUSTMENT,MENU}) {
    uiState=state;menuPage=state==MENU?0:1;renderDisplay();chars=lcd.characterWrites;cursors=lcd.cursorWrites;
    for(int i=0;i<4;++i) renderDisplay();CHECK(lcd.characterWrites==chars&&lcd.cursorWrites==cursors);
  }
  // Text caching skips repeat reads; a failed read is not cached.
  clearTextCache();reads=Wire.rxCount[0x57];uiText(0);uiText(0);CHECK(Wire.rxCount[0x57]==reads+1);
  clearTextCache();Wire.fail=true;CHECK(strcmp(uiText(0),"TEXT ERR")==0);Wire.fail=false;CHECK(strcmp(uiText(0),(char*)Wire.memory.data()+264)==0);
  clearTextCache();ahtReadingValid=bmpReadingValid=true;lastAhtSuccessMs=lastBmpSuccessMs=fakeNow;
  Wire.fail=true;renderMainEnvironmental();CHECK(lcd.rows[2].find("TEXT ERR")!=std::string::npos);Wire.fail=false;
  // A failed miss must invalidate an evicted slot, not poison its old ID.
  clearTextCache();for(uint8_t id=0;id<UI_TEXT_CACHE_SLOTS;++id) uiText(id);
  Wire.fail=true;CHECK(strcmp(uiText(20),"TEXT ERR")==0);Wire.fail=false;
  CHECK(strcmp(uiText(0),(char*)Wire.memory.data()+264)==0);
  // Recognised input bypasses the periodic refresh throttle rather than
  // waiting up to another 250 ms for a visible screen change.
  resetFirmware();uiAssetsOK=true;mainDisplayPage=0;loop();at=fakeNow;tap(PIN_BUTTON1);CHECK(fakeNow-at<250);
  CHECK(uiState==FAN_ADJUSTMENT&&uiInteractionPending);loop();CHECK(!uiInteractionPending&&lastDisplayRenderMs==fakeNow);
}
