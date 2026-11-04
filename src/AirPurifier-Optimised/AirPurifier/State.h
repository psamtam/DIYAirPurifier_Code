#pragma once
// Private implementation: included only by Firmware.cpp.
// ============================================================================
// 2. HARDWARE OBJECTS AND APPLICATION DATA
// ============================================================================

CachedLcd lcd(LCD_I2C_ADDRESS, 20, 4);
AHT20Lite aht;
BMP280Lite bmp;
DS3231Lite rtc;

bool lcdOK = false;
bool uiInteractionPending = false;
bool ahtOK = false;
bool bmpOK = false;
bool rtcOK = false;
bool eepromOK = false;

const uint32_t EEPROM_MAGIC = 0x41505246UL; // "APRF"
const uint16_t EEPROM_VERSION = 1;
const uint16_t EEPROM_TIME_FORMAT_ADDRESS = 128;
const uint8_t EEPROM_TIME_FORMAT_MARKER = 0xA7;
bool use24HourClock = true;
bool clockFormatBeforeEdit = true;
bool clockFormatSaveFailed = false;
Config config;
uint8_t fanSpeed = DEFAULT_FAN_SPEED;
uint8_t ledBrightness = DEFAULT_LED_BRIGHTNESS;

// Current readings and freshness/error state
int16_t temperatureC = 0; // tenths of a degree C
int16_t humidityPercent = 0; // tenths of one percent
int16_t pressureHpa = 0; // tenths of one hPa
bool ahtReadingValid = false;
bool bmpReadingValid = false;
uint32_t lastAhtSuccessMs = 0;
uint32_t lastBmpSuccessMs = 0;

uint16_t pm1 = 0, pm25 = 0, pm10 = 0;
uint16_t pmBuffer1[PM_BUFFER_CAPACITY];
uint16_t pmBuffer25[PM_BUFFER_CAPACITY];
uint16_t pmBuffer10[PM_BUFFER_CAPACITY];
uint8_t pmBufferCount = 0;
uint8_t pmBufferIndex = 0;
bool pmsFrameEverReceived = false;
bool pmsError = false;
uint32_t lastPmsFrameMs = 0;
uint32_t pmsEnabledAtMs = 0;

// State separation
 enum PowerState : uint8_t { OPERATING, STANDBY };
 enum UiState : uint8_t { MAIN_DISPLAY, FAN_ADJUSTMENT, LED_ADJUSTMENT, MENU, DELAY_EDIT };
PowerState powerState = OPERATING;
UiState uiState = MAIN_DISPLAY;
DelayOffTimer delayTimers[2]; // 0: fan, 1: LED; countdowns never persist.
uint8_t delaySelections[2] = {30, 30};
uint8_t delayEditTarget = 0;
uint8_t delayEditMinutes = 30;
bool delayOutputOffMessage = false;

bool lcdBacklightOn = true;
bool fanRelayOn = false;
bool pmsPowered = false;
uint8_t mainDisplayPage = 0;

// Timers (all millis()-based and rollover-safe)
uint32_t lastOperatingSensorMs = 0;
uint32_t lastStandbySensorMs = 0;
uint32_t lastStandbyLcdMs = 0;

// Power-down standby support. Timer0 stops in Power-down, so advance the
// Arduino millis counter by eight seconds on each watchdog wake.
extern volatile unsigned long timer0_millis;
volatile uint8_t standbyWatchdogFired = 0;
volatile uint8_t standbyButtonWakeFired = 0;
ISR(WDT_vect) { standbyWatchdogFired = 1; }
void standbyButtonWakeISR() { standbyButtonWakeFired = 1; }
uint32_t lastMainRotationMs = 0;
uint32_t lastBacklightActivityMs = 0;
uint32_t lastUiActivityMs = 0;
uint32_t lastMenuActivityMs = 0;
uint32_t outputsOffSinceMs = 0;
uint32_t lastDisplayRenderMs = 0;

// Encoder decoder
uint8_t previousEncoderState = 0;
int8_t encoderAccumulator = 0;
int16_t encoderDetentsPending = 0;
const int8_t encoderTable[16] PROGMEM = {
  0, -1, 1, 0, 1, 0, 0, -1,
  -1, 0, 0, 1, 0, 1, -1, 0
};

// Debounced button state
ButtonState button1 = {PIN_BUTTON1, false, false, 0, 0, false};
ButtonState button2 = {PIN_BUTTON2, false, false, 0, 0, false};
ButtonState button3 = {PIN_BUTTON3, false, false, 0, 0, false};

// Button 3 wake-backlight-only latch
bool button3WakeOnlyUntilRelease = false;

// Menu navigation/editing
uint8_t menuIndex = 0;
bool menuEditing = false;
uint8_t editingPresetIndex = 0;
bool editingFanPreset = true;
Config configBeforeEdit;
uint8_t dateEditField = 0;
int editYear = 2026;
int editMonth = 1;
int editDay = 1;
int editHour = 0;
int editMinute = 0;
int editSecond = 0;
uint8_t dateMenuIndex = 0;
bool dateEditing = false;

// Menu pages: Date & Time, Display, Sensors, Power, Fan Presets, LED Presets, Status, About
const uint8_t MENU_ITEM_COUNT_ACTUAL = 9;

// PMS parser
uint8_t pmsFrame[32];
uint8_t pmsIndex = 0;

