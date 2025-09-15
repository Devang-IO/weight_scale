// Converted for ESP32
// Original: Viral Science - Arduino Digital Weight Scale HX711 Load Cell Module
// Libraries required:
//  - HX711_ADC  (https://github.com/olkal/HX711_ADC)
//  - LiquidCrystal_I2C

#include <HX711_ADC.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ----------------- USER CHANGEABLE PINS -----------------
// HX711 wiring (change if you used different pins)
const int HX711_DOUT = 23; // HX711 DT -> ESP32 GPIO 23
const int HX711_SCK  = 22; // HX711 SCK -> ESP32 GPIO 22

// I2C pins for LCD (ESP32 allows any free pins for Wire.begin)
const int I2C_SDA = 18;    // LCD SDA -> ESP32 GPIO 18
const int I2C_SCL = 19;    // LCD SCL -> ESP32 GPIO 19

// Tare button pin (use a safe free pin)
const int TARE_BTN = 25;   // push button to ground (INPUT_PULLUP)


// ----------------- OBJECTS -----------------
HX711_ADC LoadCell(HX711_DOUT, HX711_SCK); // HX711_ADC constructor (dout, sck)
LiquidCrystal_I2C lcd(0x27, 16, 2);       // LCD I2C address (0x27 common). Run I2C scanner if unsure.

// ----------------- CALIBRATION -----------------
// Keep your existing cal factor or update after calibration
float calFactor = 375.0; // adjust per your calibration (comment from original: for 100 g)
const float MIN_WEIGHT_THRESHOLD = 5.0; // Only show weight if above 5 grams
String lastDisplay = ""; // Track last displayed text to prevent flickering

void setup() {
  // Serial debug
  Serial.begin(115200);
  delay(20);

  // init tare button
  pinMode(TARE_BTN, INPUT_PULLUP); // button to GND

  // Initialize I2C on custom pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // LCD init
  lcd.init();        // initialize the lcd (may also work with lcd.begin())
  lcd.backlight();

  // HX711 init
  LoadCell.begin();
  // give the HX711 some time to settle
  LoadCell.start(1000);
  // set calibration factor (use your previously found value or change after real calibration)
  LoadCell.setCalFactor(calFactor);

  // show startup
  lcd.setCursor(4, 0);
  lcd.print("READY");
  delay(2000);
  lcd.clear();
}

void loop() {
  // update load cell reading
  LoadCell.update();
  float weight = LoadCell.getData(); // get weight in grams
  
  String currentDisplay = "";
  
  // Only show weight if item is placed (above threshold)
  if (abs(weight) >= MIN_WEIGHT_THRESHOLD) {
    // Convert to whole grams (no decimals)
    int wholeGrams = (int)round(abs(weight));
    
    // Handle negative values
    bool isNegative = weight < 0;
    
    // Create display string
    char weightStr[10];
    snprintf(weightStr, sizeof(weightStr), "%s%d g", isNegative ? "-" : "", wholeGrams);
    currentDisplay = String(weightStr);
    
    // Overload check
    if (wholeGrams >= 5000) {
      currentDisplay = "OVERLOADED";
    }
  }
  
  // Only update display if it changed
  if (currentDisplay != lastDisplay) {
    lcd.clear();
    
    if (currentDisplay.length() > 0) {
      if (currentDisplay == "OVERLOADED") {
        lcd.setCursor(2, 0);
        lcd.print("OVERLOADED");
        lcd.setCursor(3, 1);
        lcd.print("5KG MAX!");
      } else {
        // Center the weight on display
        int len = currentDisplay.length();
        int startPos = (16 - len) / 2;
        if (startPos < 0) startPos = 0;
        
        // Display weight centered on first row
        lcd.setCursor(startPos, 0);
        lcd.print(currentDisplay);
      }
    }
    
    lastDisplay = currentDisplay;
  }

  // Tare button pressed? (button wired to GND)
  if (digitalRead(TARE_BTN) == LOW) {
    lcd.setCursor(0, 1);
    lcd.print("   Taring...    ");
    // re-start/stabilize HX711 (same approach as original)
    LoadCell.start(1000);
    // blank the data row after taring
    lcd.setCursor(0, 1);
    lcd.print("                ");
    // small debounce
    delay(300);
  }

  // Serial debug
  Serial.print("Weight (g): ");
  Serial.println(weight, 1);

  delay(200); // Update every 200ms
}
