// ESP32 SD Card - List and Read Files
// Uses your current SD pin mapping: CS=5, SCK=17, MOSI=21, MISO=25

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

// SD pins (match your working test)
static const int SD_CS   = 5;   // CS
static const int SD_SCK  = 17;  // SCK
static const int SD_MOSI = 21;  // MOSI
static const int SD_MISO = 25;  // MISO

// Utility: list directory contents recursively (depth limited)
void listDir(fs::FS &fs, const char * dirname, uint8_t levels) {
  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("[DIR]  ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("[FILE] ");
      Serial.print(file.name());
      Serial.print("  ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    file = root.openNextFile();
  }
}

// Utility: read and print a file
void readFile(fs::FS &fs, const char * path) {
  Serial.print("\n--- Reading: ");
  Serial.println(path);
  File file = fs.open(path, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println("\n--- End of file ---\n");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Prepare SPI with your custom pins
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS, SPI)) { // default speed
    Serial.println("SD init failed! Check wiring and power.");
    Serial.println("Wiring: CS=5, SCK=17, MOSI=21, MISO=25");
    return;
  }

  Serial.println("SD init successful. Listing root directory:\n");
  listDir(SD, "/", 2); // list root up to 2 levels

  // Try to read the keypad log if present
  if (SD.exists("/keypad_log.txt")) {
    readFile(SD, "/keypad_log.txt");
  } else {
    Serial.println("/keypad_log.txt not found.");
  }

  Serial.println("Done. This sketch only runs once in setup().");
}

void loop() {
  // Nothing here. Open Serial Monitor to view the listing and file content.
}
