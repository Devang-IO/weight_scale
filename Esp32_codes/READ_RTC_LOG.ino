// ESP32 - Read RTC log from SD (/rtc_log.csv)
// Uses your SD pin mapping: CS=5, SCK=17, MOSI=21, MISO=25
// Serial Monitor: 115200 baud

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

static const int SD_CS   = 5;   // CS
static const int SD_SCK  = 17;  // SCK
static const int SD_MOSI = 21;  // MOSI
static const int SD_MISO = 25;  // MISO

const char* LOG_FILE = "/rtc_log.csv";

// Read and print entire file (streaming)
void readWholeFile(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.print("\n--- BEGIN ");
  Serial.print(path);
  Serial.println(" ---");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();
  Serial.print("\n--- END ");
  Serial.print(path);
  Serial.println(" ---\n");
}

// Print last N lines without loading whole file into RAM
void printTailLines(const char* path, size_t maxLines) {
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.println("Failed to open file for reading (tail)");
    return;
  }
  size_t size = f.size();
  const size_t chunk = 256;
  size_t pos = (size > chunk) ? size - chunk : 0;
  size_t lines = 0;

  // Count lines from end until we reach maxLines
  while (true) {
    f.seek(pos);
    size_t toRead = (size - pos);
    if (toRead > chunk) toRead = chunk;

    char buf[chunk + 1];
    size_t n = f.readBytes(buf, toRead);
    buf[n] = '\0';

    for (int i = n - 1; i >= 0; --i) {
      if (buf[i] == '\n') {
        lines++;
        if (lines >= maxLines + 1) { // +1 to start at next newline
          pos += i + 1;
          f.seek(pos);
          goto PRINT_FROM_POS;
        }
      }
    }

    if (pos == 0) break; // reached start
    if (pos < chunk) pos = 0; else pos -= chunk;
  }

  // If we didn't find enough lines, print from beginning
  f.seek(0);

PRINT_FROM_POS:
  Serial.print("\n--- LAST ");
  Serial.print(maxLines);
  Serial.print(" LINES OF ");
  Serial.print(path);
  Serial.println(" ---");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();
  Serial.println("\n--- END TAIL ---\n");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS, SPI)) {
    Serial.println("SD init failed! Check wiring: CS=5, SCK=17, MOSI=21, MISO=25");
    return;
  }
  Serial.println("SD init successful.");

  if (!SD.exists(LOG_FILE)) {
    Serial.print(LOG_FILE);
    Serial.println(" not found.");
    return;
  }

  // Choose what to print:
  // 1) Print last 40 lines (typical usage for logs)
  printTailLines(LOG_FILE, 40);

  // 2) Or uncomment to print whole file (can be long):
  // readWholeFile(LOG_FILE);
}

void loop() {
  // Nothing here.
}
