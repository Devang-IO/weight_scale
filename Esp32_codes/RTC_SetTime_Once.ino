/*
 * RTC_SetTime_Once.ino
 * Simple utility to set DS3231 RTC time once via Serial.
 *
 * How to use:
 * 1) Wire DS3231 to ESP32 I2C SDA=18, SCL=19 (change pins below if needed)
 * 2) Upload this sketch. Open Serial Monitor at 115200 baud.
 * 3) Enter date/time in one of the formats and press Enter:
 *    - ISO: YYYY-MM-DDTHH:MM:SS    (example: 2025-09-24T14:06:00)
 *    - SPACES: YYYY-MM-DD HH:MM:SS (example: 2025-09-24 14:06:00)
 *    - EPOCH: unix seconds         (example: 1758703560)
 * 4) The sketch will set the RTC and then print the current RTC time every second.
 * 5) Power off, then load your main sketch (Finalv5.ino).
 */

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

// I2C pins (adjust if needed)
const int I2C_SDA = 18;
const int I2C_SCL = 19;

RTC_DS3231 rtc;

bool parseISOorSpace(const String& s, tm& out) {
  // Accept "YYYY-MM-DDTHH:MM:SS" or "YYYY-MM-DD HH:MM:SS"
  if (s.length() < 19) return false;
  String str = s.substring(0, 19);
  char buf[20];
  str.toCharArray(buf, sizeof(buf));
  int y, M, d, h, m, sec;
  if (sscanf(buf, "%d-%d-%dT%d:%d:%d", &y, &M, &d, &h, &m, &sec) == 6 ||
      sscanf(buf, "%d-%d-%d %d:%d:%d", &y, &M, &d, &h, &m, &sec) == 6) {
    memset(&out, 0, sizeof(out));
    out.tm_year = y - 1900;
    out.tm_mon  = M - 1;
    out.tm_mday = d;
    out.tm_hour = h;
    out.tm_min  = m;
    out.tm_sec  = sec;
    return true;
  }
  return false;
}

bool parseEpoch(const String& s, time_t& out) {
  for (size_t i = 0; i < s.length(); i++) {
    if (!isDigit(s[i])) return false;
  }
  out = (time_t) s.toInt();
  return out > 1000000000UL; // simple sanity check
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("RTC Set Time (DS3231)"));
  Serial.println(F("Enter one of the following and press Enter:"));
  Serial.println(F("  - 2025-09-24T14:06:00"));
  Serial.println(F("  - 2025-09-24 14:06:00"));
  Serial.println(F("  - 1758703560 (epoch seconds, UTC)"));

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!rtc.begin()) {
    Serial.println(F("RTC not found! Check wiring."));
  } else {
    if (rtc.lostPower()) {
      Serial.println(F("RTC lost power; time not set yet."));
    }
  }
}

unsigned long lastPrint = 0;

void loop() {
  // Handle user input
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      tm t = {};
      time_t ep = 0;
      bool ok = false;
      if (parseISOorSpace(line, t)) {
        // Interpret input as UTC
        time_t utc = timegm(&t); // convert tm (UTC) to epoch
        rtc.adjust(DateTime((uint32_t)utc));
        ok = true;
      } else if (parseEpoch(line, ep)) {
        rtc.adjust(DateTime((uint32_t)ep));
        ok = true;
      } else {
        Serial.println(F("Unrecognized format. Try again."));
      }
      if (ok) {
        DateTime now = rtc.now();
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
        Serial.print(F("RTC set. Current RTC: "));
        Serial.println(buf);
      }
    }
  }

  // Periodic display
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    DateTime now = rtc.now();
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
    Serial.print(F("RTC now: ")); Serial.println(buf);
  }
}
