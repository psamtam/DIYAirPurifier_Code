#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <avr/sleep.h>
#include <avr/power.h>

// Define I2C address for the 20x4 LCD (commonly 0x27 or 0x3F)
#define LCD_I2C_ADDRESS 0x27

// Pin definitions as specified
const uint8_t PIN_BUTTON3 = 7;   // External interrupt-capable pin (INT6)
const uint8_t PIN_ENC_A   = A0;  // Encoder Pin A
const uint8_t PIN_ENC_B   = A1;  // Encoder Pin B

// Initialize the LCD (20 columns, 4 rows)
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 20, 4);

// Volatile variables used across interrupts and main loop
volatile int encoderCounter = 0;
bool lastEncAState = HIGH;

// Variables for button debouncing in active mode
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void setup() {
  // 5-second safety delay to allow reflashing/reprogramming 
  delay(5000);

  // Initialize I2C and LCD
  lcd.init();                      
  
  // Initial display setup
  lcd.setCursor(0, 0);
  lcd.print("Power-Down Test");
  lcd.setCursor(0, 1);
  lcd.print("Count: 0");

  // Configure pins with internal pull-ups
  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
}

void loop() {
  // 1. Rotary Encoder Reader
  bool currentEncAState = digitalRead(PIN_ENC_A);
  if (currentEncAState != lastEncAState) {
    if (currentEncAState == LOW) {
      if (digitalRead(PIN_ENC_B) == HIGH) {
        encoderCounter++;
      } else {
        encoderCounter--;
      }
      if (encoderCounter < 0) encoderCounter = 0;
      updateDisplay();
    }
    lastEncAState = currentEncAState;
  }

  // 2. Button Reader: Increases counter when awake (interrupt is detached here)
  bool currentButtonState = digitalRead(PIN_BUTTON3);
  if (currentButtonState != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (currentButtonState == LOW && lastButtonState == HIGH) {
      encoderCounter++;
      updateDisplay();
    }
  }
  lastButtonState = currentButtonState;

  // 3. Check if counter reaches 10 to enter deep low-power sleep mode
  if (encoderCounter >= 10) {
    lcd.setCursor(0, 2);
    lcd.print("Entering Sleep...   ");
    delay(200); // Allow text to render
    
    lcd.noBacklight();        // Turn off LCD backlight to save power
    ADCSRA &= ~(1 << ADEN);   // Disable ADC (Analog-to-Digital Converter) to stop current leakage

    // Attach interrupt for waking up (LOW level trigger works reliably in Power-Down mode on INT6)
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON3), wakeUpISR, LOW);

    // Set deepest sleep mode: Power-Down
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    
    cli();          // Disable global interrupts temporarily
    sleep_enable(); // Enable sleep mode bit
    sei();          // Re-enable global interrupts

    // Enter sleep state. Execution completely freezes here until PIN_BUTTON3 is pressed to GND.
    sleep_cpu(); 

    // --- Execution resumes here immediately after wakeup ---

    sleep_disable();                       // Clear sleep enable bit
    detachInterrupt(digitalPinToInterrupt(PIN_BUTTON3)); // Detach wakeup interrupt so active polling works again
    ADCSRA |= (1 << ADEN);                 // Re-enable ADC

    encoderCounter = 0; // Reset counter to 0 as requested
    lcd.setCursor(0, 2);
    lcd.print("Woken & Reset!      ");
    delay(1000);
    lcd.setCursor(0, 2);
    lcd.print("                    "); // Clear status line
    updateDisplay();
  }

  delay(2);
}

void updateDisplay() {
  lcd.setCursor(0, 1);
  lcd.print("Count: ");
  lcd.print(encoderCounter);
  lcd.print("   "); // Clear trailing characters
}

// Interrupt Service Routine executed upon wakeup
void wakeUpISR() {
  // Empty ISR: its sole purpose is to break the CPU out of sleep_cpu()
}