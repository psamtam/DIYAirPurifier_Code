#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pin definitions provided
const uint8_t PIN_ENC_A    = A0;
const uint8_t PIN_ENC_B    = A1;

// LCD Setup: Address 0x27, 20 columns, 4 rows
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Encoder tracking variables
long encoderCount = 0;
int lastStateA;

void setup() {
  // Initialize Serial Monitor for extra debugging
  Serial.begin(9600);

  // Configure encoder pins as inputs with internal pull-up resistors
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);

  // Read initial state of encoder pin A
  lastStateA = digitalRead(PIN_ENC_A);

  // Initialize the LCD
  lcd.init();                      
  lcd.backlight(); // Turn on backlight
  
  // Display initial test layout across all 4 rows
  lcd.setCursor(0, 0);
  lcd.print(" LCD 2004 & Encoder ");
  lcd.setCursor(0, 1);
  lcd.print("Status: Running     ");
  lcd.setCursor(0, 2);
  lcd.print("Count: 0            ");
  lcd.setCursor(0, 3);
  lcd.print("Rotate encoder now! ");
}

void loop() {
  int currentStateA = digitalRead(PIN_ENC_A);

  // Check if encoder pin A has changed state
  if (currentStateA != lastStateA) {
    // Determine rotation direction based on pin B
    if (digitalRead(PIN_ENC_B) != currentStateA) {
      encoderCount++;
    } else {
      encoderCount--;
    }

    // Output to Serial Monitor
    Serial.print("Encoder Count: ");
    Serial.println(encoderCount);

    // Update the LCD count on row 3 (index 2)
    lcd.setCursor(0, 2);
    lcd.print("Count:              "); // Clear space
    lcd.setCursor(0, 2);
    lcd.print("Count: ");
    lcd.print(encoderCount);
  }

  // Save the current state for the next check
  lastStateA = currentStateA;
}