//ESP32 Digital Weight Scale - Background Data Processing Version
//This version stores data locally first, then sends in background

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

// ===== Configuration =====
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define SUPABASE_URL "https://zoblfvpwqodiuudwitwt.supabase.co"
#define SUPABASE_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InpvYmxmdnB3cW9kaXV1ZHdpdHd0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTc5MzA0OTAsImV4cCI6MjA3MzUwNjQ5MH0.AG0TqekNZ505BodHamvdhQ3A4lk0OtsLrJBGC1YlP3g"
#define SUPABASE_WEIGHTS_ENDPOINT "/rest/v1/weights"
#define PLANT_KEY_PREFIX "plant_"

#define CAL_FACTOR 360.0f

// Pin definitions
#define PIN_HX711_DOUT  23
#define PIN_HX711_SCK   22
#define PIN_I2C_SDA     18
#define PIN_I2C_SCL     19
#define PIN_TARE_BUTTON 25

// Keypad pins - Updated pin configuration
static const byte KEYPAD_ROWS = 4;
static const byte KEYPAD_COLS = 3;
static const char KEYPAD_KEYS[4][3] = {
  { '1','2','3' },
  { '4','5','6' },
  { '7','8','9' },
  { '*','0','#' }
};
static byte KEYPAD_ROW_PINS[4] = { 13, 12, 14, 27 }; // Updated: swapped index 2 & 3
static byte KEYPAD_COL_PINS[3] = { 26, 33, 25 };     // Updated: changed pin 32 to 25

// SD Card pins
#define PIN_SD_CS   5
#define PIN_SD_SCK  16
#define PIN_SD_MISO 17
#define PIN_SD_MOSI 21
#define QUEUE_FILE_PATH "/data_queue.ndjson"

// ===== Hardware Objects =====
HX711_ADC LoadCell(PIN_HX711_DOUT, PIN_HX711_SCK);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Keypad keypad = Keypad(makeKeymap(KEYPAD_KEYS), KEYPAD_ROW_PINS, KEYPAD_COL_PINS, KEYPAD_ROWS, KEYPAD_COLS);
RTC_DS3231 rtc;

// ===== System State Variables =====
enum SystemState {
  STATE_PLANT_SELECTION,
  STATE_PLACE_ITEM,
  STATE_WEIGHING,
  STATE_SAVING,
  STATE_SUCCESS,
  STATE_ERROR
};

SystemState currentState = STATE_PLANT_SELECTION;
String selectedPlant = "";
float stableWeight = 0;
bool wifiConnected = false;
bool sdCardReady = false;

// Simple weight reading for better accuracy (from esp32-simple-scale.ino)
float currentWeight = 0;
float lastDisplayWeight = 0;
const float MIN_WEIGHT_THRESHOLD = 2.0;
const float STABLE_THRESHOLD = 0.5; // Only update if change is significant
int stableCount = 0;
const int REQUIRED_STABLE_READINGS = 8; // More stable readings required

// Background processing
unsigned long lastBackgroundProcess = 0;
const unsigned long BACKGROUND_INTERVAL = 2000; // Process queue every 2 seconds
bool backgroundProcessing = false;
int pendingItems = 0;

// Timing
unsigned long lastWeightTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long stateStartTime = 0;
const unsigned long WEIGHT_READ_INTERVAL = 50; // Faster reading for better stability
const unsigned long DISPLAY_UPDATE_INTERVAL = 300;
const unsigned long SUCCESS_DISPLAY_TIME = 2500;

// ===== Setup Function =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Digital Scale v2.0 - Background Processing ===");
  
  // Initialize I2C
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  
  // Initialize LCD
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Scale v2.0");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  
  // Initialize Load Cell with longer stabilization
  LoadCell.begin();
  LoadCell.start(3000); // Longer stabilization time
  LoadCell.setCalFactor(CAL_FACTOR);
  
  // Initialize weight variables
  currentWeight = 0;
  lastDisplayWeight = 0;
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    lcd.clear();
    lcd.print("RTC Error!");
    delay(2000);
  } else {
    Serial.println("RTC initialized");
  }
  
  // Initialize SD Card
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS)) {
    Serial.println("SD Card failed!");
    sdCardReady = false;
    lcd.clear();
    lcd.print("SD Card Error!");
    delay(2000);
  } else {
    Serial.println("SD Card ready");
    sdCardReady = true;
    checkPendingItems(); // Count pending items on startup
  }
  
  // Connect to WiFi
  connectToWiFi();
  
  // Sync RTC with NTP if WiFi connected
  if (wifiConnected) {
    syncRTCWithNTP();
  }
  
  // Auto-tare on boot - simple method
  lcd.clear();
  lcd.print("Auto Taring...");
  LoadCell.start(2000);
  delay(1000);
  
  // Initialize state
  currentState = STATE_PLANT_SELECTION;
  stateStartTime = millis();
  
  Serial.println("Setup complete! Background processing enabled.");
}

// ===== Main Loop =====
void loop() {
  // Handle keypad input
  char key = keypad.getKey();
  if (key) {
    handleKeypadInput(key);
  }
  
  // Update load cell
  LoadCell.update();
  
  // Read weight periodically - simple method
  if (millis() - lastWeightTime >= WEIGHT_READ_INTERVAL) {
    readWeightSimple();
    lastWeightTime = millis();
  }
  
  // Update display periodically
  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // Handle state machine
  handleStateMachine();
  
  // Background processing - send queued data without blocking user
  if (millis() - lastBackgroundProcess >= BACKGROUND_INTERVAL) {
    if (wifiConnected && sdCardReady && !backgroundProcessing) {
      processQueueInBackground();
    }
    checkWiFiStatus();
    lastBackgroundProcess = millis();
  }
}

// ===== Simple Weight Reading for Accuracy =====
void readWeightSimple() {
  // Get direct reading from load cell (like esp32-simple-scale.ino)
  float rawWeight = LoadCell.getData();
  
  // Handle negative values (make them positive)
  if (rawWeight < 0) {
    rawWeight = rawWeight * (-1);
  }
  
  // Update current weight
  currentWeight = rawWeight;
  
  // Check for stability in weighing state using simple method
  if (currentState == STATE_WEIGHING) {
    if (abs(currentWeight - lastDisplayWeight) < STABLE_THRESHOLD) {
      stableCount++;
    } else {
      stableCount = 0;
      lastDisplayWeight = currentWeight;
    }
    
    // If weight is stable and above threshold, proceed to save
    if (stableCount >= REQUIRED_STABLE_READINGS && currentWeight >= MIN_WEIGHT_THRESHOLD) {
      stableWeight = currentWeight;
      currentState = STATE_SAVING;
      stateStartTime = millis();
      Serial.printf("Stable weight detected: %.2fg\n", stableWeight);
      stableCount = 0; // Reset counter
    }
  }
}

// ===== WiFi Functions =====
void connectToWiFi() {
  lcd.clear();
  lcd.print("WiFi Connecting");
  Serial.print("Connecting to WiFi");
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 15) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(0, 1);
    lcd.print("Try: " + String(attempts + 1));
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connected!");
    lcd.clear();
    lcd.print("WiFi OK!");
    delay(1500);
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi failed - Offline mode");
    lcd.clear();
    lcd.print("Offline Mode");
    delay(1500);
  }
}

void checkWiFiStatus() {
  bool wasConnected = wifiConnected;
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  
  // Reboot system on WiFi state changes for stability
  if (!wasConnected && wifiConnected) {
    Serial.println("WiFi connected! Rebooting system for stability...");
    lcd.clear();
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print("Rebooting...");
    delay(2000);
    ESP.restart();
  } else if (wasConnected && !wifiConnected) {
    Serial.println("WiFi disconnected! Rebooting system for stability...");
    lcd.clear();
    lcd.print("WiFi Lost!");
    lcd.setCursor(0, 1);
    lcd.print("Rebooting...");
    delay(2000);
    ESP.restart();
  }
}

// ===== RTC Functions =====
void syncRTCWithNTP() {
  lcd.clear();
  lcd.print("Syncing time...");
  
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 8) {
    delay(1000);
    attempts++;
  }
  
  if (attempts < 8) {
    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                       timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
    Serial.println("RTC synced with NTP");
  }
  delay(1000);
}

String getCurrentTimestamp() {
  DateTime now = rtc.now();
  char timestamp[25];
  sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02dZ", 
          now.year(), now.month(), now.day(), 
          now.hour(), now.minute(), now.second());
  return String(timestamp);
}

// ===== Keypad Input Handler =====
void handleKeypadInput(char key) {
  Serial.print("Key pressed: ");
  Serial.println(key);
  
  if (key == '*') {
    performManualTare();
    return;
  } else if (key == '#') {
    lcd.clear();
    lcd.print("Rebooting...");
    delay(1000);
    ESP.restart();
    return;
  }
  
  if (currentState == STATE_PLANT_SELECTION) {
    if (key >= '0' && key <= '9') {
      selectedPlant = String(key);
      currentState = STATE_PLACE_ITEM;
      stateStartTime = millis();
      Serial.print("Selected plant: ");
      Serial.println(selectedPlant);
    }
  }
}

void performManualTare() {
  SystemState previousState = currentState;
  
  lcd.clear();
  lcd.print("Manual Tare...");
  LoadCell.start(2000);
  
  // Reset weight variables
  currentWeight = 0;
  lastDisplayWeight = 0;
  stableCount = 0;
  stableWeight = 0;
  
  delay(1000);
  currentState = previousState;
}

// ===== Display Functions =====
void updateDisplay() {
  switch (currentState) {
    case STATE_PLANT_SELECTION:
      displayPlantSelection();
      break;
    case STATE_PLACE_ITEM:
      displayPlaceItem();
      break;
    case STATE_WEIGHING:
      displayWeighing();
      break;
    case STATE_SAVING:
      displaySaving();
      break;
    case STATE_SUCCESS:
      displaySuccess();
      break;
    case STATE_ERROR:
      displayError();
      break;
  }
  
  // Show status indicators
  lcd.setCursor(14, 1);
  lcd.print(wifiConnected ? "+" : "x");
  
  // Show pending items count if any
  if (pendingItems > 0) {
    lcd.setCursor(12, 1);
    lcd.print(String(pendingItems));
  }
}

void displayPlantSelection() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Select Plant 0-9");
  lcd.setCursor(0, 1);
  lcd.print("* Tare  # Reboot");
}

void displayPlaceItem() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Plant " + selectedPlant + " Ready");
  lcd.setCursor(0, 1);
  lcd.print("Place item now");
  
  // Check for weight to start weighing - simple method
  if (currentWeight > MIN_WEIGHT_THRESHOLD) {
    currentState = STATE_WEIGHING;
    stateStartTime = millis();
    stableCount = 0;
    lastDisplayWeight = currentWeight;
    Serial.println("Starting weighing process");
  }
}

void displayWeighing() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Weighing...");
  
  // Show current weight - simple method
  lcd.setCursor(0, 1);
  lcd.print(String(currentWeight, 2) + "g");
  
  // Show stability progress (now requires 8 readings)
  int stabilityPercent = min(100, (stableCount * 100) / REQUIRED_STABLE_READINGS);
  lcd.setCursor(10, 1);
  lcd.print(String(stabilityPercent) + "%");
}

void displaySaving() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Saving Data...");
  lcd.setCursor(0, 1);
  lcd.print(String(stableWeight, 2) + "g");
}

void displaySuccess() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Saved! Plant " + selectedPlant);
  lcd.setCursor(0, 1);
  lcd.print(String(stableWeight, 2) + "g");
}

void displayError() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Save Error!");
  lcd.setCursor(0, 1);
  lcd.print("Try again");
}

// ===== State Machine Handler =====
void handleStateMachine() {
  switch (currentState) {
    case STATE_SAVING:
      if (millis() - stateStartTime > 500) { // Quick save without waiting for API
        bool success = saveDataToQueue();
        if (success) {
          currentState = STATE_SUCCESS;
          stateStartTime = millis();
        } else {
          currentState = STATE_ERROR;
          stateStartTime = millis();
        }
      }
      break;
      
    case STATE_SUCCESS:
      if (millis() - stateStartTime > SUCCESS_DISPLAY_TIME) {
        // Check if item is removed - simple method
        if (currentWeight < MIN_WEIGHT_THRESHOLD) {
          currentState = STATE_PLANT_SELECTION;
          selectedPlant = "";
          stableWeight = 0;
          lastDisplayWeight = 0;
          stableCount = 0;
          stateStartTime = millis();
        }
      }
      break;
      
    case STATE_ERROR:
      if (millis() - stateStartTime > 2000) {
        currentState = STATE_PLANT_SELECTION;
        selectedPlant = "";
        stableWeight = 0;
        stateStartTime = millis();
      }
      break;
  }
}

// ===== Data Storage and Background Processing =====
bool saveDataToQueue() {
  if (!sdCardReady) return false;
  
  String timestamp = getCurrentTimestamp();
  String jsonPayload = createJSONPayload(selectedPlant, stableWeight, timestamp);
  
  File file = SD.open(QUEUE_FILE_PATH, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open queue file");
    return false;
  }
  
  file.println(jsonPayload);
  file.close();
  
  pendingItems++;
  Serial.println("Data saved to queue: " + jsonPayload);
  return true;
}

String createJSONPayload(String plant, float weight, String timestamp) {
  StaticJsonDocument<200> doc;
  doc[PLANT_KEY_PREFIX + plant] = weight;
  doc["created_at"] = timestamp;
  
  String output;
  serializeJson(doc, output);
  return output;
}

void processQueueInBackground() {
  if (!wifiConnected || !sdCardReady || backgroundProcessing) return;
  
  backgroundProcessing = true;
  
  File file = SD.open(QUEUE_FILE_PATH, FILE_READ);
  if (!file) {
    backgroundProcessing = false;
    return;
  }
  
  // Read and send one item at a time to avoid blocking
  String line = file.readStringUntil('\n');
  file.close();
  
  if (line.length() > 0) {
    line.trim();
    if (sendToSupabase(line)) {
      // Remove the sent item from queue
      removeSentItemFromQueue();
      pendingItems = max(0, pendingItems - 1);
      Serial.println("Background: Item sent successfully");
    } else {
      Serial.println("Background: Failed to send item");
    }
  }
  
  backgroundProcessing = false;
}

bool sendToSupabase(String jsonPayload) {
  if (!wifiConnected) return false;
  
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + String(SUPABASE_WEIGHTS_ENDPOINT));
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("apikey", SUPABASE_KEY);
  http.setTimeout(5000); // 5 second timeout to avoid long blocking
  
  int httpResponseCode = http.POST(jsonPayload);
  
  bool success = (httpResponseCode >= 200 && httpResponseCode < 300);
  
  if (success) {
    Serial.println("API Success: " + String(httpResponseCode));
  } else {
    Serial.println("API Failed: " + String(httpResponseCode));
  }
  
  http.end();
  return success;
}

void removeSentItemFromQueue() {
  if (!sdCardReady) return;
  
  // Read all lines except the first one
  File readFile = SD.open(QUEUE_FILE_PATH, FILE_READ);
  if (!readFile) return;
  
  String tempContent = "";
  String line = readFile.readStringUntil('\n'); // Skip first line
  
  while (readFile.available()) {
    line = readFile.readStringUntil('\n');
    if (line.length() > 0) {
      tempContent += line + "\n";
    }
  }
  readFile.close();
  
  // Write back the remaining content
  File writeFile = SD.open(QUEUE_FILE_PATH, FILE_WRITE);
  if (writeFile) {
    writeFile.print(tempContent);
    writeFile.close();
  }
}

void checkPendingItems() {
  if (!sdCardReady) return;
  
  File file = SD.open(QUEUE_FILE_PATH, FILE_READ);
  if (!file) {
    pendingItems = 0;
    return;
  }
  
  pendingItems = 0;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() > 10) { // Valid JSON line
      pendingItems++;
    }
  }
  file.close();
  
  Serial.println("Pending items found: " + String(pendingItems));
}
