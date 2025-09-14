#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define LCD_ADDR 0x27
LiquidCrystal_I2C lcd(LCD_ADDR,16,2);

void setup(){
  Wire.begin(21,22);
  lcd.init();
  lcd.backlight(); // ensure backlight ON
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Backlight ON");
  lcd.setCursor(0,1);
  lcd.print("Adjust contrast");
}
void loop(){}

/*
Wiring (typical for ESP32 modules):

LCD I2C GND → ESP32 GND

LCD I2C VCC → ESP32 3V3 (some modules also work with 5V)

LCD I2C SDA → ESP32 GPIO21

LCD I2C SCL → ESP32 GPIO22
*/