#include <HX711_ADC.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>

// ===== WiFi Configuration =====
#define WIFI_SSID      "YOUR_WIFI_SSID"        // TODO: set
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"    // TODO: set

// ===== Supabase Configuration =====
// Base URL of your Supabase project and the anon service key
#define SUPABASE_URL   "https://zoblfvpwqodiuudwitwt.supabase.co"
#define SUPABASE_KEY   "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InpvYmxmdnB3cW9kaXV1ZHdpdHd0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTc5MzA0OTAsImV4cCI6MjA3MzUwNjQ5MH0.AG0TqekNZ505BodHamvdhQ3A4lk0OtsLrJBGC1YlP3g"
// REST endpoint path used by the firmware when posting rows
#define SUPABASE_WEIGHTS_ENDPOINT "/rest/v1/weights"
// Name of the timestamp column used by the firmware
#define SUPABASE_CREATED_AT_FIELD "created_at"
// Prefix used to build the plant column from keypad selection (e.g., plant_1)
#define PLANT_KEY_PREFIX "plant_"

/*
  Data mapping and payload format
  - Keypad selection -> plant column name
      '0' -> plant_0
      '1' -> plant_1
      ...
      '9' -> plant_9

  - The firmware constructs the JSON field dynamically as:
      PLANT_KEY_PREFIX + <selected digit>

  - Example POST body sent to SUPABASE_URL + SUPABASE_WEIGHTS_ENDPOINT
      {
        "plant_1": 123,
        "created_at": "2025-09-24T11:22:33Z"
      }

  Notes
  - created_at is generated from the RTC at the time the stable weight is captured (UTC, ISO 8601 with 'Z').
  - When offline or send fails, the exact same JSON line is appended to OFFLINE_FILE_PATH as NDJSON.
  - On reconnection, the firmware flushes the file: POSTs each line as-is in order.
*/

// ===== Calibration =====
// Initial calibration factor for HX711_ADC
#define CAL_FACTOR 360.0f

// ===== Hardware Pin Configuration =====
// HX711 Load Cell
#define PIN_HX711_DOUT  23
#define PIN_HX711_SCK   22

// I2C (LCD + RTC)
#define PIN_I2C_SDA     18
#define PIN_I2C_SCL     19

// Keypad (3x4)
static const byte KEYPAD_ROWS = 4;
static const byte KEYPAD_COLS = 3;
static const char KEYPAD_KEYS[4][3] = {
  { '1','2','3' },
  { '4','5','6' },
  { '7','8','9' },
  { '*','0','#' }
};
static const byte KEYPAD_ROW_PINS[4] = { 13, 12, 27, 14 };
static const byte KEYPAD_COL_PINS[3] = { 26, 33, 32 };

// SD Card (SPI)
#define PIN_SD_CS   5
#define PIN_SD_SCK  16
#define PIN_SD_MISO 17
#define PIN_SD_MOSI 21

// Offline queue file on SD (NDJSON: one JSON object per line)
#define OFFLINE_FILE_PATH "/offline_queue.ndjson"

#endif // FINALV5_CONFIG_H
