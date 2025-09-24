/*
 * ESP32 Weight Scale Final v5
 * - Preserves UI flow: "Press plant no." -> keypad -> "Place item" -> stable measure -> send/queue -> show result -> wait removal -> back to start
 * - Improvements for accuracy & robustness:
 *   - Use signed readings everywhere (no fabs bias)
 *   - Stability test: collect many samples, compute stddev; accept only if stable
 *   - Larger averaging window with trimmed mean to reject spikes
 *   - Boot-time empty check and guided tare; user-triggered tare via '*' when idle
 *   - Optional auto-zero tracking when idle and stable
 *   - Decouple WiFi badge from live HTTP checks; background cache only
 * - RTC timestamp, SD offline queue, Supabase upload retained
 */

#include <Arduino.h>
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
#include <Preferences.h>

// ----------------- WiFi Configuration -----------------
const char* ssid = "YOUR_WIFI_SSID";            // TODO: set
const char* password = "YOUR_WIFI_PASSWORD";    // TODO: set

// ----------------- Supabase Configuration -----------------
const char* supabaseUrl = "https://zoblfvpwqodiuudwitwt.supabase.co";
const char* supabaseKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InpvYmxmdnB3cW9kaXV1ZHdpdHd0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTc5MzA0OTAsImV4cCI6MjA3MzUwNjQ5MH0.AG0TqekNZ505BodHamvdhQ3A4lk0OtsLrJBGC1YlP3g";

// ----------------- Hardware Pin Configuration -----------------
// HX711 Load Cell
const int HX711_DOUT = 23;
const int HX711_SCK  = 22;

// I2C (LCD + RTC share the same bus)
const int I2C_SDA = 18;
const int I2C_SCL = 19;

// Keypad (3x4)
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {13, 12, 27, 14};
byte colPins[COLS] = {26, 33, 32};

// SD Card (SPI)
const int SD_CS_PIN = 5;
const int SD_SCK_PIN = 16;
const int SD_MISO_PIN = 17;
const int SD_MOSI_PIN = 21;
const char* OFFLINE_FILE = "/offline_queue.ndjson";

// ----------------- Objects -----------------
HX711_ADC LoadCell(HX711_DOUT, HX711_SCK);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
RTC_DS3231 rtc;
Preferences prefs;

// ----------------- Configuration -----------------
// Calibration factor: load from NVS if available; default here
float calFactor = 360.0f;  // You can adjust later; persisted value overrides

const float MIN_WEIGHT_THRESHOLD = 5.0f;    // grams to consider item present
const int STABLE_DURATION_MS = 600;         // require present this long before capture
const int REMOVED_DURATION_MS = 150;        // require removed for this long before reset

// Stability measurement parameters
const int STABILITY_SAMPLES = 120;          // ~1.2-1.8s depending on delay
const int STABILITY_DELAY_MS = 12;          // delay between samples
const float STDDEV_THRESHOLD_G = 0.8f;      // max stddev allowed to accept weight
const float TRIM_FRACTION = 0.1f;           // trimmed mean: drop 10% low/high

// Boot empty check
const unsigned long BOOT_CHECK_DURATION_MS = 2000;
const float BOOT_EMPTY_MAX_ABS_G = 2.0f;    // if |mean| > this, prompt user to clear
const float BOOT_EMPTY_MAX_STD_G = 1.5f;    // if std too high, prompt as well

// Auto-zero tracking when idle
const bool ENABLE_AUTO_ZERO = true;
const float AUTO_ZERO_MAX_ABS_G = 1.0f;     // must be within +/-1g
const float AUTO_ZERO_MAX_STD_G = 0.5f;     // readings must be stable
const unsigned long AUTO_ZERO_MIN_IDLE_MS = 5000; // idle time before auto-zero
const unsigned long AUTO_ZERO_COOLDOWN_MS = 15000; // cooldown between auto-zeros

// API reachability cache interval
const unsigned long API_CHECK_INTERVAL_MS = 10000; // 10s

// ----------------- State Machine -----------------
enum State {
  SHOW_PRESS_PLANT,
  WAIT_FOR_PLANT_SELECTION,
  WAIT_FOR_ITEM_PRESENT_STABLE,
  MEASURE_AND_SEND,
  WAIT_FOR_ITEM_REMOVAL_STABLE,
  FLUSHING_OFFLINE
};

State state = SHOW_PRESS_PLANT;
int selectedPlant = -1;
unsigned long stateTs = 0;

// Flush progress tracking
int flushTotal = -1;
int flushSent = 0;

// WiFi connection tracking for toasts
bool lastWifiConnected = false;

// Item presence tracking
bool itemPresent = false;
unsigned long presentSince = 0;
unsigned long removedSince = 0;

// Background flags
bool apiReachableCached = false;
unsigned long lastApiCheckMs = 0;

// Auto-zero tracking
unsigned long idleSinceMs = 0;
unsigned long lastAutoZeroMs = 0;

// Forward declarations
void initializeHardware();
void connectToWiFi();
bool sendToSupabase(int plantNumber, float weight, const String& iso8601);
void showPressPlant();
void showWifiToast(const char* msg);
void updateWifiBadge();
bool isItemPresent(float weight);
bool measureStableWeight(float &outMeanG);
String getISO8601();
void ensureSD();
void queueOffline(int plantNumber, float weight, const String& iso8601);
bool flushOfflineQueue();
bool hasOfflineData();
bool isApiReachableCached();
int countOfflineLines();
void doUserTare();
bool bootEmptyCheckAndTare();
void maybeAutoZero(float currentW);

// Utils
static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

void setup() {
  Serial.begin(115200);
  delay(200);

  initializeHardware();
  connectToWiFi();

  // Boot-time empty check and guided tare
  if (!bootEmptyCheckAndTare()) {
    // If user never cleared, continue but warn
    Serial.println("Proceeding without confirmed empty tare.");
  }

  // Attempt to flush any offline data on boot if WiFi already connected
  if (WiFi.status() == WL_CONNECTED && hasOfflineData() && isApiReachableCached()) {
    lcd.clear();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Sending data...");
    lcd.setCursor(0, 1);
    lcd.print("Please wait");
    state = FLUSHING_OFFLINE;
    stateTs = millis();
    return;
  }

  // Start UI
  showPressPlant();
  state = WAIT_FOR_PLANT_SELECTION;
  stateTs = millis();
  lastWifiConnected = (WiFi.status() == WL_CONNECTED);
  idleSinceMs = millis();
}

void loop() {
  LoadCell.update();
  float currentW = LoadCell.getData(); // signed reading
  itemPresent = isItemPresent(currentW);

  // Auto-zero when idle and stable
  if (state == WAIT_FOR_PLANT_SELECTION && ENABLE_AUTO_ZERO) {
    maybeAutoZero(currentW);
  }

  // Handle WiFi reconnection and periodic flush
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 1000) {
    lastWiFiCheck = millis();
    bool nowConn = (WiFi.status() == WL_CONNECTED);
    if (!nowConn) {
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      if (lastWifiConnected && state == WAIT_FOR_PLANT_SELECTION) {
        showWifiToast("WiFi disconnected");
      }
    } else {
      if (!lastWifiConnected && state == WAIT_FOR_PLANT_SELECTION) {
        showWifiToast("WiFi connected");
      }
      // If connected and offline data exists, enter flushing mode immediately when idle
      if (hasOfflineData() && state == WAIT_FOR_PLANT_SELECTION) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Checking internet");
        lcd.setCursor(0, 1);
        lcd.print(" ");
        state = FLUSHING_OFFLINE;
        stateTs = millis();
      }
    }
    lastWifiConnected = nowConn;
  }

  // Background API reachability cache
  if (millis() - lastApiCheckMs > API_CHECK_INTERVAL_MS) {
    lastApiCheckMs = millis();
    apiReachableCached = isApiReachableCached();
  }

  char key = keypad.getKey();

  switch (state) {
    case SHOW_PRESS_PLANT:
      state = WAIT_FOR_PLANT_SELECTION;
      stateTs = millis();
      break;

    case WAIT_FOR_PLANT_SELECTION:
      if (key == '*') {
        doUserTare();
        idleSinceMs = millis();
        break;
      }
      if (key && key >= '0' && key <= '9') {
        selectedPlant = (key == '0') ? 0 : (key - '0');
        Serial.print("Plant selected: "); Serial.println(selectedPlant);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Plant "); lcd.print(selectedPlant);
        lcd.setCursor(0, 1);
        lcd.print("Place item");
        state = WAIT_FOR_ITEM_PRESENT_STABLE;
        presentSince = 0;
        stateTs = millis();
      }
      break;

    case WAIT_FOR_ITEM_PRESENT_STABLE:
      if (itemPresent) {
        if (presentSince == 0) presentSince = millis();
        if (millis() - presentSince >= (unsigned long)STABLE_DURATION_MS) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Please wait...");
          state = MEASURE_AND_SEND;
          stateTs = millis();
        }
      } else {
        presentSince = 0;
      }
      break;

    case MEASURE_AND_SEND: {
      float meanG = NAN;
      bool stable = measureStableWeight(meanG);
      if (!stable) {
        // Show unstable message briefly and retry waiting for stable presence
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Unstable weight");
        lcd.setCursor(0, 1);
        lcd.print("Try again");
        delay(600);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Plant "); lcd.print(selectedPlant);
        lcd.setCursor(0, 1);
        lcd.print("Place item");
        state = WAIT_FOR_ITEM_PRESENT_STABLE;
        presentSince = 0;
        stateTs = millis();
        break;
      }

      int gramsToSend = (int)lroundf(meanG);
      String ts = getISO8601();

      bool sent = sendToSupabase(selectedPlant, gramsToSend, ts);
      if (!sent) {
        queueOffline(selectedPlant, gramsToSend, ts);
      }

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Plant "); lcd.print(selectedPlant);
      lcd.setCursor(0, 1);
      lcd.print(gramsToSend); lcd.print("g ");
      lcd.print(sent ? "sent" : "queued");

      Serial.print("Plant "); Serial.print(selectedPlant);
      Serial.print(" weight "); Serial.print(meanG, 2);
      Serial.print("g at "); Serial.print(ts);
      Serial.println(sent ? " [SENT]" : " [QUEUED]");

      state = WAIT_FOR_ITEM_REMOVAL_STABLE;
      removedSince = 0;
      stateTs = millis();
      break; }

    case WAIT_FOR_ITEM_REMOVAL_STABLE:
      // Allow user to force reset with '#' or new selection
      if (key == '#' || (key && key >= '0' && key <= '9')) {
        selectedPlant = -1;
        showPressPlant();
        state = WAIT_FOR_PLANT_SELECTION;
        stateTs = millis();
        idleSinceMs = millis();
        break;
      }
      if (!itemPresent) {
        if (removedSince == 0) removedSince = millis();
        if (millis() - removedSince >= (unsigned long)REMOVED_DURATION_MS) {
          selectedPlant = -1;
          showPressPlant();
          state = WAIT_FOR_PLANT_SELECTION;
          stateTs = millis();
          idleSinceMs = millis();
        }
      } else {
        removedSince = 0;
      }
      break;

    case FLUSHING_OFFLINE: {
      if (WiFi.status() != WL_CONNECTED) {
        showPressPlant();
        state = WAIT_FOR_PLANT_SELECTION;
        stateTs = millis();
        break;
      }

      // Simple sending animation
      static unsigned long lastAnim = 0;
      static const char spinnerChars[4] = {'|','/','-','\\'};
      static uint8_t spIdx = 0;
      if (millis() - lastAnim > 200) {
        lastAnim = millis();
        spIdx = (spIdx + 1) & 0x03;
        lcd.setCursor(0, 0);
        lcd.print("Sending        ");
        lcd.setCursor(15, 0);
        lcd.print(spinnerChars[spIdx]);
        lcd.setCursor(0, 1);
        lcd.print("                ");
        updateWifiBadge();
      }

      bool done = flushOfflineQueue();
      if (done || !hasOfflineData()) {
        flushTotal = -1;
        flushSent = 0;
        showPressPlant();
        state = WAIT_FOR_PLANT_SELECTION;
        stateTs = millis();
        idleSinceMs = millis();
      }
      break; }
  }

  delay(10);
}

void initializeHardware() {
  // I2C (LCD + RTC)
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();

  // Custom chars for WiFi badge (char 0) and cross (char 1)
  uint8_t wifiGlyph[8] = {
    0b00000,
    0b00000,
    0b00100,
    0b01010,
    0b10001,
    0b00000,
    0b00100,
    0b00000
  };
  uint8_t xGlyph[8] = {
    0b00000,
    0b10001,
    0b01010,
    0b00100,
    0b01010,
    0b10001,
    0b00000,
    0b00000
  };
  lcd.createChar(0, wifiGlyph);
  lcd.createChar(1, xGlyph);

  // HX711
  LoadCell.begin();

  // Load calFactor from NVS if present
  prefs.begin("scale", true);
  if (prefs.isKey("cal")) {
    float stored = prefs.getFloat("cal", calFactor);
    if (isfinite(stored) && stored > 0.0f) calFactor = stored;
  }
  prefs.end();

  // Stabilize and set cal
  LoadCell.start(1000, false); // don't tare yet; we'll guide it
  LoadCell.setCalFactor(calFactor);

  // RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
  } else {
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, setting to compile time");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  // SD (custom SPI pins)
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI)) {
    Serial.println("SD init failed");
  } else {
    ensureSD();
  }

  Serial.println("Hardware initialized");
}

void connectToWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(250);
    lcd.setCursor(attempts % 16, 1);
    lcd.print(".");
    attempts++;
  }

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.setCursor(0, 0); lcd.print("WiFi connected");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    lcd.setCursor(0, 0); lcd.print("WiFi failed");
  }
  delay(800);
}

bool isItemPresent(float weight) {
  return fabsf(weight) >= MIN_WEIGHT_THRESHOLD;
}

// Measure many samples, check stability via stddev, and return trimmed mean
bool measureStableWeight(float &outMeanG) {
  const int N = STABILITY_SAMPLES;
  static float samples[STABILITY_SAMPLES];

  // Collect
  for (int i = 0; i < N; i++) {
    LoadCell.update();
    samples[i] = LoadCell.getData(); // signed
    delay(STABILITY_DELAY_MS);
  }

  // Compute mean and stddev
  double sum = 0.0;
  for (int i = 0; i < N; i++) sum += samples[i];
  float mean = (float)(sum / N);

  double sumsq = 0.0;
  for (int i = 0; i < N; i++) {
    double d = samples[i] - mean;
    sumsq += d * d;
  }
  float stddev = (float)sqrt(sumsq / (N - 1));

  if (stddev > STDDEV_THRESHOLD_G) {
    Serial.print("Unstable stddev="); Serial.println(stddev, 3);
    return false;
  }

  // Trimmed mean (drop TRIM_FRACTION low/high)
  // Copy to temp and sort (simple insertion sort due to small N)
  static float work[STABILITY_SAMPLES];
  for (int i = 0; i < N; i++) work[i] = samples[i];
  for (int i = 1; i < N; i++) {
    float key = work[i];
    int j = i - 1;
    while (j >= 0 && work[j] > key) { work[j+1] = work[j]; j--; }
    work[j+1] = key;
  }
  int trim = (int)floorf(TRIM_FRACTION * N);
  int start = trim;
  int end = N - trim;
  if (end - start < 5) { start = 0; end = N; } // ensure enough points
  double tsum = 0.0;
  int tcount = 0;
  for (int i = start; i < end; i++) { tsum += work[i]; tcount++; }
  float tmean = (float)(tsum / tcount);

  outMeanG = tmean;
  return true;
}

String getISO8601() {
  DateTime now = rtc.now();
  char buf[25];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return String(buf);
}

bool sendToSupabase(int plantNumber, float weight, const String& iso8601) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot send");
    return false;
  }

  HTTPClient http;
  String url = String(supabaseUrl) + "/rest/v1/weights";
  http.setTimeout(4000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  http.addHeader("Prefer", "return=minimal");

  StaticJsonDocument<128> doc;
  String plantKey = String("plant_") + String(plantNumber);
  doc[plantKey] = (int)lroundf(weight);
  doc["created_at"] = iso8601;

  String body;
  serializeJson(doc, body);

  Serial.print("POST "); Serial.println(url);
  Serial.print("Payload: "); Serial.println(body);

  int code = http.POST(body);
  http.end();

  Serial.print("HTTP code: "); Serial.println(code);
  return code == 200 || code == 201;
}

void showPressPlant() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Press plant no.");
  lcd.setCursor(0, 1);
  lcd.print("* tare  1-9,0 ");
  updateWifiBadge();
}

void showWifiToast(const char* msg) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(msg);
  lcd.setCursor(0, 1);
  lcd.print(" ");
  delay(500);
  if (state == WAIT_FOR_PLANT_SELECTION) {
    showPressPlant();
  }
}

// Bottom-right WiFi badge only reflects WiFi link (no live HTTP)
void updateWifiBadge() {
  lcd.setCursor(15, 1);
  if (WiFi.status() == WL_CONNECTED) {
    lcd.write((uint8_t)0); // WiFi glyph
  } else {
    lcd.write((uint8_t)1); // Cross
  }
}

void ensureSD() {
  if (!SD.exists(OFFLINE_FILE)) {
    File f = SD.open(OFFLINE_FILE, FILE_WRITE);
    if (f) { f.close(); }
  }
}

void queueOffline(int plantNumber, float weight, const String& iso8601) {
  ensureSD();
  File f = SD.open(OFFLINE_FILE, FILE_APPEND);
  if (!f) {
    Serial.println("Failed to open offline queue for append");
    return;
  }
  StaticJsonDocument<128> doc;
  String plantKey = String("plant_") + String(plantNumber);
  doc[plantKey] = (int)lroundf(weight);
  doc["created_at"] = iso8601;
  String line;
  serializeJson(doc, line);
  line += '\n';
  f.print(line);
  f.close();
  Serial.println("Queued offline: " + line);
}

bool flushOfflineQueue() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!SD.exists(OFFLINE_FILE)) return true;

  File in = SD.open(OFFLINE_FILE, FILE_READ);
  if (!in) return false;

  const char* TMP_FILE = "/offline_queue_tmp.ndjson";
  SD.remove(TMP_FILE);
  File out = SD.open(TMP_FILE, FILE_WRITE);
  if (!out) { in.close(); return false; }

  bool allSent = true;
  int processed = 0;
  const int MAX_LINES_PER_CALL = 1000;
  while (in.available() && processed < MAX_LINES_PER_CALL) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    HTTPClient http;
    String url = String(supabaseUrl) + "/rest/v1/weights";
    http.setTimeout(4000);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabaseKey);
    http.addHeader("Authorization", "Bearer " + String(supabaseKey));
    http.addHeader("Prefer", "return=minimal");

    int code = http.POST(line);
    http.end();

    if (!(code == 200 || code == 201)) {
      allSent = false;
      out.println(line);
    }
    processed++;
  }

  bool moreRemaining = in.available();
  while (in.available()) {
    String rest = in.readStringUntil('\n');
    rest.trim();
    if (rest.length() == 0) continue;
    out.println(rest);
  }
  in.close();
  out.close();

  SD.remove(OFFLINE_FILE);
  if (!allSent || moreRemaining) {
    SD.rename(TMP_FILE, OFFLINE_FILE);
  } else {
    SD.remove(TMP_FILE);
  }
  return allSent && !moreRemaining;
}

bool hasOfflineData() {
  if (!SD.exists(OFFLINE_FILE)) return false;
  File f = SD.open(OFFLINE_FILE, FILE_READ);
  if (!f) return false;
  bool hasData = f.available();
  f.close();
  return hasData;
}

// API reachability check with caching; avoid calling from UI draw paths
bool isApiReachableCached() {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  String url = String(supabaseUrl) + "/rest/v1/weights?select=id&limit=1";
  http.setTimeout(1000);
  http.begin(url);
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  int code = http.GET();
  http.end();
  return code == 200;
}

int countOfflineLines() {
  if (!SD.exists(OFFLINE_FILE)) return 0;
  File f = SD.open(OFFLINE_FILE, FILE_READ);
  if (!f) return 0;
  int count = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (line.length() > 0) count++;
  }
  f.close();
  return count;
}

void doUserTare() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Taring...");
  lcd.setCursor(0, 1);
  lcd.print("Keep empty");

  LoadCell.tareNoDelay();
  unsigned long start = millis();
  while (!LoadCell.getTareStatus() && millis() - start < 8000UL) {
    LoadCell.update();
    delay(5);
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  if (LoadCell.getTareStatus()) {
    lcd.print("Tare complete");
  } else {
    lcd.print("Tare timeout");
  }
  delay(600);
  showPressPlant();
}

bool bootEmptyCheckAndTare() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Checking empty");
  lcd.setCursor(0, 1);
  lcd.print("platform...");

  // Sample a short window
  const unsigned long tEnd = millis() + BOOT_CHECK_DURATION_MS;
  const int N = 100;
  float buf[100];
  int n = 0;
  while ((millis() < tEnd) && n < N) {
    LoadCell.update();
    buf[n++] = LoadCell.getData();
    delay(10);
  }
  if (n == 0) n = 1;

  double sum = 0.0; for (int i = 0; i < n; i++) sum += buf[i];
  float mean = (float)(sum / n);
  double sumsq = 0.0; for (int i = 0; i < n; i++) { double d = buf[i] - mean; sumsq += d*d; }
  float stddev = (float)sqrt(sumsq / max(1, n-1));

  if (fabsf(mean) > BOOT_EMPTY_MAX_ABS_G || stddev > BOOT_EMPTY_MAX_STD_G) {
    // Prompt user to clear, then tare
    while (true) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Clear the scale");
      lcd.setCursor(0, 1); lcd.print("Press * to tare");
      unsigned long start = millis();
      bool done = false;
      while (millis() - start < 20000UL) { // 20s window; can loop again
        LoadCell.update();
        char k = keypad.getKey();
        if (k == '*') { done = true; break; }
        delay(10);
      }
      if (done) break;
    }
  }

  // Perform tare now
  LoadCell.tareNoDelay();
  unsigned long start = millis();
  while (!LoadCell.getTareStatus() && millis() - start < 8000UL) {
    LoadCell.update();
    delay(5);
  }

  lcd.clear();
  if (LoadCell.getTareStatus()) {
    lcd.setCursor(0, 0); lcd.print("Ready");
    lcd.setCursor(0, 1); lcd.print("Press plant no.");
    return true;
  } else {
    lcd.setCursor(0, 0); lcd.print("Tare failed");
    lcd.setCursor(0, 1); lcd.print("Proceeding...");
    delay(800);
    return false;
  }
}

void maybeAutoZero(float currentW) {
  if (fabsf(currentW) < AUTO_ZERO_MAX_ABS_G) {
    if (idleSinceMs == 0) idleSinceMs = millis();
  } else {
    idleSinceMs = millis();
    return;
  }
  if (millis() - idleSinceMs >= AUTO_ZERO_MIN_IDLE_MS && millis() - lastAutoZeroMs >= AUTO_ZERO_COOLDOWN_MS) {
    // Check short-term stability before auto-zero
    const int N = 60;
    float b[N];
    for (int i = 0; i < N; i++) { LoadCell.update(); b[i] = LoadCell.getData(); delay(8); }
    double sum = 0.0; for (int i = 0; i < N; i++) sum += b[i];
    float mean = (float)(sum / N);
    double ss = 0.0; for (int i = 0; i < N; i++) { double d = b[i] - mean; ss += d*d; }
    float sd = (float)sqrt(ss / (N-1));
    if (fabsf(mean) < AUTO_ZERO_MAX_ABS_G && sd < AUTO_ZERO_MAX_STD_G) {
      Serial.println("Auto-zero engaged");
      LoadCell.tareNoDelay();
      unsigned long start = millis();
      while (!LoadCell.getTareStatus() && millis() - start < 2000UL) { LoadCell.update(); delay(5); }
      lastAutoZeroMs = millis();
    } else {
      idleSinceMs = millis(); // reset idle window if unstable
    }
  }
}
