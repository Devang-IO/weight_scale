// ESP32 + DS3231 RTC basic test
// I2C pins used: SDA=18, SCL=19 (match your existing project wiring)
// Serial Monitor: 115200 baud
// Library: Adafruit RTClib (install via Library Manager)

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

// I2C pins
static const int I2C_SDA = 18;
static const int I2C_SCL = 19;

RTC_DS3231 rtc;

// Optional: quick I2C scan to help diagnose wiring
void i2cScan() {
  Serial.println("\nI2C scan start...");
  byte count = 0;
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("Found I2C device at 0x");
      if (address < 16) Serial.print('0');
      Serial.print(address, HEX);
      Serial.println();
      count++;
    }
  }
  Serial.print("I2C scan done. Devices found: ");
  Serial.println(count);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n--- DS3231 RTC Test ---");
  Wire.begin(I2C_SDA, I2C_SCL);

  // Optional: run a quick scan
  i2cScan();

  if (!rtc.begin()) {
    Serial.println("ERROR: Couldn't find DS3231 RTC. Check wiring: SDA=18, SCL=19, VCC=3.3V, GND.");
    return;
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting the time to compile time.");
    // Set RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Small sanity read
  DateTime now = rtc.now();
  Serial.print("Current time: ");
  Serial.print(now.year()); Serial.print('/');
  Serial.print(now.month()); Serial.print('/');
  Serial.print(now.day()); Serial.print(' ');
  Serial.print(now.hour()); Serial.print(':');
  Serial.print(now.minute()); Serial.print(':');
  Serial.println(now.second());

  // Show temperature from DS3231 sensor
  float tempC = rtc.getTemperature();
  Serial.print("DS3231 temperature: ");
  Serial.print(tempC, 2); Serial.println(" C");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    DateTime now = rtc.now();

    char buf[32];
    // Format: YYYY-MM-DD HH:MM:SS
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

    float tempC = rtc.getTemperature();

    Serial.print("Time: ");
    Serial.print(buf);
    Serial.print(" | Temp: ");
    Serial.print(tempC, 2);
    Serial.println(" C");
  }
}
