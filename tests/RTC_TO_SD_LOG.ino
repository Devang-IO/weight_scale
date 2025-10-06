// ESP32 + DS3231 -> SD card logger
// Logs real-time timestamp and temperature from DS3231 to /rtc_log.csv on SD
// Pins used to match your project:
//   I2C: SDA=18, SCL=19
//   SD (custom SPI): CS=5, SCK=17, MOSI=21, MISO=25
// Serial Monitor: 115200 baud
// Library: Adafruit RTClib (install via Library Manager)

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>

// I2C pins (match project)
static const int I2C_SDA = 18;
static const int I2C_SCL = 19;

// SD pins (match project working mapping)
static const int SD_CS   = 5;   // CS
static const int SD_SCK  = 17;  // SCK
static const int SD_MOSI = 21;  // MOSI
static const int SD_MISO = 25;  // MISO

RTC_DS3231 rtc;

const char* LOG_FILE = "/rtc_log.csv";

// Append one CSV line to the log file
static bool appendCSV(const String &line) {
  File f = SD.open(LOG_FILE, FILE_WRITE);
  if (!f) {
    Serial.println("Failed to open log file for write");
    return false;
  }
  f.seek(f.size());
  f.println(line);
  f.close();
  return true;
}

// Format a DateTime to YYYY-MM-DD,HH:MM:SS
static String formatTimestamp(const DateTime &dt) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d,%02d:%02d:%02d",
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  return String(buf);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n--- RTC to SD Logger ---");

  // Start I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Start RTC
  if (!rtc.begin()) {
    Serial.println("ERROR: DS3231 not found. Check wiring: SDA=18, SCL=19, VCC=3.3V, GND.");
    return;
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time to compile time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Prepare SPI and SD
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS, SPI)) {
    Serial.println("SD init failed! Check wiring: CS=5, SCK=17, MOSI=21, MISO=25");
    return;
  }
  Serial.println("SD init successful.");

  // Create CSV header if file doesn't exist
  if (!SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_WRITE);
    if (f) {
      f.println("date,clock,time,temperature_C"); // date and time split for easier CSV import
      f.close();
      Serial.print("Created log file: ");
      Serial.println(LOG_FILE);
    } else {
      Serial.println("Failed to create log file header.");
    }
  } else {
    Serial.print("Logging to existing file: ");
    Serial.println(LOG_FILE);
  }

  Serial.println("Logging starts. One line per second.");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();

    DateTime now = rtc.now();
    float tempC = rtc.getTemperature();

    // Construct CSV line: YYYY-MM-DD,HH:MM:SS,<temp>
    String csvLine = formatTimestamp(now) + "," + String(tempC, 2);

    bool ok = appendCSV(csvLine);

    Serial.print("Write ");
    Serial.print(ok ? "OK: " : "FAIL: ");
    Serial.println(csvLine);
  }
}
