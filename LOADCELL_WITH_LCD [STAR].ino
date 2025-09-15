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
int a = 0;
float b = 0;

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
  lcd.setCursor(1, 0);
  lcd.print("Digital Scale ");
  lcd.setCursor(0, 1);
  lcd.print(" 5KG MAX LOAD ");
  delay(2000);
  lcd.clear();
}

void loop() {
  // update load cell reading
  LoadCell.update();
  float i = LoadCell.getData(); // get weight in grams (depending on cal factor)

  // handle negative sign display (original did abs and printed '-')
  if (i < 0) {
    i = -i;
    lcd.setCursor(0, 1);
    lcd.print("-");
    lcd.setCursor(8, 1);
    lcd.print("-");
  } else {
    lcd.setCursor(0, 1);
    lcd.print(" ");
    lcd.setCursor(8, 1);
    lcd.print(" ");
  }

  // display heading
  lcd.setCursor(1, 0);
  lcd.print("Digital Scale ");

  // print grams (with 1 decimal)
  lcd.setCursor(1, 1);
  // ensure we clear leftover digits if number shrinks
  char buf[17];
  snprintf(buf, sizeof(buf), "%5.1f g ", i);
  lcd.print(buf);

  // print ounces converted
  float z = i / 28.3495;
  // position at column 9 (0-indexed)
  lcd.setCursor(9, 1);
  // print with 2 decimals
  char ozbuf[8];
  snprintf(ozbuf, sizeof(ozbuf), "%4.2f", z);
  lcd.print(ozbuf);
  lcd.print("oz");

  // Overload check (same as original)
  if (i >= 5000) {
    i = 0;
    lcd.setCursor(0, 0);
    lcd.print("  Over Loaded   ");
    delay(200);
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

  // also print to Serial for debugging
  Serial.print("Weight (g): ");
  Serial.println(i, 2);

  delay(200); // faster update; increase if needed to reduce noise on display
}
