// Keypad -> SD card logger test for ESP32
// Uses custom SPI pin mapping so it won't conflict with your HX711 + I2C LCD pins

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Keypad.h>

// ---------- SD card SPI pin mapping (custom) ----------
static const int SD_CS   = 5;   // Chip Select (default, reliable on DevKit when not held low at boot)
static const int SD_SCK  = 17;  // SCK (TX2/U2_TXD)
static const int SD_MISO = 25;  // MISO
static const int SD_MOSI = 21;  // MOSI

// ---------- Keypad (reuse your existing mapping) ----------
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
// Your project’s pins:
byte rowPins[ROWS] = {13, 12, 27, 14};
byte colPins[COLS] = {26, 33, 32};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// File name on SD
const char* LOG_FILE = "/keypad_log.txt";

// Helper to append text to the log file, with retry if needed
void appendToLog(const String &line) {
  File f = SD.open(LOG_FILE, FILE_WRITE);
  if (!f) {
    Serial.println("Failed to open log file (write). Retrying SD init...");
    // Try a quick re-init
    SD.end();
    delay(50);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 1000000)) {
      Serial.println("Re-init failed.");
      return;
    }
    // Retry open
    f = SD.open(LOG_FILE, FILE_WRITE);
    if (!f) {
      Serial.println("Failed to open log file after re-init");
      return;
    }
  }
  // Append by seeking to end for reliability
  f.seek(f.size());
  f.println(line);
  f.close();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Ensure CS idles high before touching SPI
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // Initialize remapped SPI bus
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  // Try to initialize SD (start slow for signal integrity)
  Serial.println("Initializing SD card...");
  bool sdOk = SD.begin(SD_CS, SPI); // default clock (~4 MHz on ESP32)
  if (!sdOk) {
    Serial.println("SD init failed! Check wiring and power.\n"
                   "- If your SD adapter has a regulator/level shifters (big 'Micro SD Card Module'), power it with 5V.\n"
                   "- If it's a bare microSD breakout/socket, power it with 3.3V only.\n"
                   "Wiring: CS=5, SCK=17, MOSI=21, MISO=25");
  } else {
    Serial.println("SD init successful.");

    // Optional diagnostics
    if (SD.cardType() != CARD_NONE) {
      Serial.print("Card type: ");
      Serial.println(SD.cardType());
      Serial.print("Card size (MB): ");
      Serial.println((uint32_t)(SD.cardSize() / (1024ULL * 1024ULL)));
    }

    // Create file with header if not present
    if (!SD.exists(LOG_FILE)) {
      File f = SD.open(LOG_FILE, FILE_WRITE);
      if (f) {
        f.println("timestamp_ms,key");
        f.close();
        Serial.println("Created log file with header.");
      } else {
        Serial.println("Failed to create log file header");
      }
    } else {
      Serial.println("Log file exists.");
    }
  }

  Serial.println("Ready. Press keypad keys to log.");
}

void loop() {
  char key = keypad.getKey();
  if (key) {
    unsigned long t = millis();
    String line = String(t) + "," + key;
    Serial.print("Key: ");
    Serial.println(line);
    appendToLog(line);
  }
}