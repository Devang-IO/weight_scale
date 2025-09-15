/*
 * ESP32 Keypad + LCD Test
 * Shows keypad presses on LCD display
 */

#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- LCD Configuration ---
const int I2C_SDA = 18;
const int I2C_SCL = 19;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Keypad Configuration ---
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

byte rowPins[ROWS] = {13, 12, 27, 14};
byte colPins[COLS] = {26, 33, 32};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- Variables ---
String inputBuffer = "";
int cursorPos = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize I2C LCD
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  
  // Show startup message
  lcd.setCursor(0, 0);
  lcd.print("Keypad LCD Test");
  lcd.setCursor(0, 1);
  lcd.print("Press any key...");
  
  Serial.println("Keypad LCD Test Ready!");
  delay(2000);
  
  // Clear and show input area
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Input:");
  lcd.setCursor(0, 1);
  lcd.print("*=Enter #=Clear");
}

void loop() {
  char key = keypad.getKey();
  
  if (key) {
    Serial.print("Key pressed: ");
    Serial.println(key);
    
    if (key == '*') {
      // Enter/Submit
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Entered:");
      lcd.setCursor(0, 1);
      if (inputBuffer.length() > 0) {
        lcd.print(inputBuffer);
      } else {
        lcd.print("(empty)");
      }
      
      Serial.print("Final input: ");
      Serial.println(inputBuffer);
      
      delay(2000);
      clearInput();
      
    } else if (key == '#') {
      // Clear
      clearInput();
      Serial.println("Input cleared");
      
    } else {
      // Regular key (0-9)
      if (inputBuffer.length() < 10) { // Limit input length
        inputBuffer += key;
        updateDisplay();
      }
    }
  }
}

void clearInput() {
  inputBuffer = "";
  cursorPos = 0;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Input:");
  lcd.setCursor(0, 1);
  lcd.print("*=Enter #=Clear");
}

void updateDisplay() {
  // Clear the input line
  lcd.setCursor(0, 1);
  lcd.print("                "); // Clear line
  
  // Show current input
  lcd.setCursor(0, 1);
  lcd.print(inputBuffer);
  
  // Show blinking cursor
  lcd.setCursor(inputBuffer.length(), 1);
  lcd.print("_");
}