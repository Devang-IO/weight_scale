//ESP32 Digital Weight Scale / Background Data Processing Version
#include <HX711_ADC.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>

// ===== Configuration =====
#define WIFI_SSID "strox"
#define WIFI_PASSWORD "applered"
#define SUPABASE_URL "https://zoblfvpwqodiuudwitwt.supabase.co"
#define SUPABASE_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InpvYmxmdnB3cW9kaXV1ZHdpdHd0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTc5MzA0OTAsImV4cCI6MjA3MzUwNjQ5MH0.AG0TqekNZ505BodHamvdhQ3A4lk0OtsLrJBGC1YlP3g"
#define SUPABASE_WEIGHTS_ENDPOINT "/rest/v1/weights"
#define PLANT_KEY_PREFIX "plant_"
#define CAL_FACTOR 360.0f

// Pins
#define PIN_HX711_DOUT  23
#define PIN_HX711_SCK   22
#define PIN_I2C_SDA     18
#define PIN_I2C_SCL     19
#define BUTTON_WAKE_PIN 4
#define BUTTON_MODE_PIN 15
#define BUTTON_WEB_PIN 32
#define BUZZER_PIN 2

// Keypad pins
static const byte KEYPAD_ROWS = 4;
static const byte KEYPAD_COLS = 3;
static const char KEYPAD_KEYS[4][3] = {
  { '1','2','3' },
  { '4','5','6' },
  { '7','8','9' },
  { '*','0','#' }
};
static byte KEYPAD_ROW_PINS[4] = { 13, 12, 14, 27 };
static byte KEYPAD_COL_PINS[3] = { 26, 33, 25 };

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
WebServer server(80);
Preferences preferences;

// ===== System State Variables =====
enum SystemState {
  STATE_PLANT_SELECTION,
  STATE_PLACE_ITEM,
  STATE_WEIGHING,
  STATE_SAVING,
  STATE_SUCCESS,
  STATE_ERROR
};

enum SystemMode {
  MODE_FULL,
  MODE_SIMPLE,
  MODE_WEBAPP
};

SystemState currentState = STATE_PLANT_SELECTION;
SystemMode currentMode = MODE_FULL;
String selectedPlant = "";
float stableWeight = 0;
bool wifiConnected = false;
bool sdCardReady = false;
bool isIdle = false;
bool rtcReady = false;

String wifiSSID = WIFI_SSID;
String wifiPassword = WIFI_PASSWORD;
float calibrationFactor = CAL_FACTOR;
unsigned long idleTimeout = 120000;

// Simple weight reading for better accuracy
float currentWeight = 0;
float lastDisplayWeight = 0;
float currentTemperature = 0; // ESP32 internal temperature in Celsius
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
unsigned long lastActivity = 0;
const unsigned long WEIGHT_READ_INTERVAL = 50;
const unsigned long DISPLAY_UPDATE_INTERVAL = 300;
const unsigned long SUCCESS_DISPLAY_TIME = 2500;

bool lastButton1State = HIGH;
bool lastButton2State = HIGH;
bool lastButton3State = HIGH;
bool wifiApActive = false;
bool webHandlersConfigured = false;
unsigned long systemStartTime = 0;
unsigned long readingsSent = 0;

// ===== Function Declarations =====
void loadSettings();
void saveSettings();
void handleButtons();
void beep(int duration);
void switchMode();
void startWebApp();
void stopWebApp(bool showMessage = true);
void enterIdleMode();
void wakeFromIdle();
void resetIdleTimer();
void runFullMode();
void runSimpleMode();
void runWebAppMode();
void showStatsScreen();
void updateDisplaySimple();
void checkIdleTimeout();
void setupWebServer();
void handleDashboard();
void handleAPIStatus();
void handleGetSettings();
void handlePostSettings();
void handleSDList();
void handleSDDownload();
void handleSDDelete();
String formatUptime(unsigned long milliseconds);
void checkSDCardSpace();
void cleanupOldData();

void loadSettings() {
  wifiSSID = preferences.getString("ssid", WIFI_SSID);
  wifiPassword = preferences.getString("pass", WIFI_PASSWORD);
  calibrationFactor = preferences.getFloat("cal", CAL_FACTOR);
  idleTimeout = preferences.getULong("idle", 120000);
}

void saveSettings() {
  preferences.putString("ssid", wifiSSID);
  preferences.putString("pass", wifiPassword);
  preferences.putFloat("cal", calibrationFactor);
  preferences.putULong("idle", idleTimeout);
}

// ===== Setup Function =====
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Digital Scale v2.0 - Background Processing ===");

  systemStartTime = millis();

  if (!preferences.begin("scale", false)) {
    Serial.println("Preferences init failed, using defaults");
  } else {
    loadSettings();
  }

  pinMode(BUTTON_WAKE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_WEB_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

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
  LoadCell.start(3000);
  LoadCell.setCalFactor(calibrationFactor);

  currentWeight = 0;
  lastDisplayWeight = 0;
  currentTemperature = temperatureRead(); // Initialize temperature

  rtcReady = rtc.begin();
  if (!rtcReady) {
    Serial.println("RTC not found!");
    lcd.clear();
    lcd.print("RTC Error!");
    delay(2000);
  }

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
    checkPendingItems();
    checkSDCardSpace();
  }

  connectToWiFi();

  if (wifiConnected) {
    syncRTCWithNTP();
  }

  lcd.clear();
  lcd.print("Auto Taring...");
  LoadCell.start(2000);
  delay(1000);

  currentState = STATE_PLANT_SELECTION;
  stateStartTime = millis();
  resetIdleTimer();

  // Startup beeps - 3 consecutive beeps
  beep(100);
  delay(100);
  beep(100);
  delay(100);
  beep(100);

  Serial.println("Setup complete! Background processing enabled.");
}

// ===== Main Loop =====
void loop() {
  handleButtons();

  char key = keypad.getKey();
  if (key) {
    beep(40);
    handleKeypadInput(key);
    resetIdleTimer();
  }

  checkIdleTimeout();
  if (isIdle) {
    delay(100);
    return;
  }

  switch (currentMode) {
    case MODE_FULL:
      runFullMode();
      break;
    case MODE_SIMPLE:
      runSimpleMode();
      break;
    case MODE_WEBAPP:
      runWebAppMode();
      break;
  }
}

void handleButtons() {
  bool button1Pressed = digitalRead(BUTTON_WAKE_PIN) == LOW;
  bool button2Pressed = digitalRead(BUTTON_MODE_PIN) == LOW;
  bool button3Pressed = digitalRead(BUTTON_WEB_PIN) == LOW;

  if (button1Pressed && lastButton1State == HIGH) {
    beep(60);
    if (isIdle) {
      wakeFromIdle();
    }
    resetIdleTimer();
  }

  if (button2Pressed && lastButton2State == HIGH) {
    beep(80);
    if (currentMode == MODE_WEBAPP) {
      stopWebApp(true);
    } else {
      switchMode();
    }
    resetIdleTimer();
  }

  if (button3Pressed && lastButton3State == HIGH) {
    beep(100);
    if (currentMode != MODE_WEBAPP) {
      startWebApp();
    } else {
      stopWebApp(true);
    }
    resetIdleTimer();
  }

  lastButton1State = button1Pressed;
  lastButton2State = button2Pressed;
  lastButton3State = button3Pressed;
}

void beep(int duration) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}

void resetIdleTimer() {
  lastActivity = millis();
}

void checkIdleTimeout() {
  if (!isIdle && (millis() - lastActivity) > idleTimeout) {
    enterIdleMode();
  }
}

void enterIdleMode() {
  if (isIdle) return;
  isIdle = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Going Idle...");
  delay(1000);
  lcd.noBacklight();
  lcd.noDisplay();
  LoadCell.powerDown();
}

void wakeFromIdle() {
  if (!isIdle) return;
  isIdle = false;
  LoadCell.powerUp();
  lcd.display();
  lcd.backlight();
  lcd.clear();
  lcd.print("System Active");
  lcd.setCursor(0, 1);
  lcd.print("Welcome Back");
  delay(1000);
  currentState = STATE_PLANT_SELECTION;
  stateStartTime = millis();
  resetIdleTimer();
}

void switchMode() {
  if (currentMode == MODE_WEBAPP) {
    return;
  }

  // Mode change beeps - 2 beeps
  beep(80);
  delay(100);
  beep(80);

  if (currentMode == MODE_FULL) {
    currentMode = MODE_SIMPLE;
    lcd.clear();
    lcd.print("Simple Mode");
    lcd.setCursor(0, 1);
    lcd.print("Scale Only");
  } else {
    currentMode = MODE_FULL;
    lcd.clear();
    lcd.print("Full Mode");
    lcd.setCursor(0, 1);
    lcd.print("All Systems");
    currentState = STATE_PLANT_SELECTION;
  }
  delay(1000);
  lastDisplayUpdate = 0;
}

void startWebApp() {
  if (currentMode == MODE_WEBAPP) {
    return;
  }

  stopWebApp(false);

  WiFi.mode(WIFI_AP);
  WiFi.softAPdisconnect(true);
  delay(100);

  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP("Scale_Dashboard", "12345678");

  setupWebServer();
  server.begin();
  wifiApActive = true;

  lcd.clear();
  lcd.print("Web App Ready");
  lcd.setCursor(0, 1);
  lcd.print("192.168.4.1");

  currentMode = MODE_WEBAPP;
  lastDisplayUpdate = 0;
}

void stopWebApp(bool showMessage) {
  if (wifiApActive) {
    server.stop();
    WiFi.softAPdisconnect(true);
    wifiApActive = false;
  }

  WiFi.mode(WIFI_STA);
  connectToWiFi();

  if (showMessage) {
    lcd.clear();
    lcd.print("Exiting WebApp");
    lcd.setCursor(0, 1);
    lcd.print("Restoring...");
    delay(800);
  }

  currentMode = MODE_FULL;
  currentState = STATE_PLANT_SELECTION;
  lastDisplayUpdate = 0;
}

void runFullMode() {
  LoadCell.update();

  if (millis() - lastWeightTime >= WEIGHT_READ_INTERVAL) {
    readWeightSimple();
    lastWeightTime = millis();
  }

  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }

  handleStateMachine();

  if (millis() - lastBackgroundProcess >= BACKGROUND_INTERVAL) {
    if (wifiConnected && sdCardReady && !backgroundProcessing) {
      processQueueInBackground();
    }
    checkWiFiStatus();
    checkSDCardSpace();
    currentTemperature = temperatureRead(); // Update temperature reading
    lastBackgroundProcess = millis();
  }
}

void runSimpleMode() {
  LoadCell.update();

  if (millis() - lastWeightTime >= WEIGHT_READ_INTERVAL) {
    // Use the same weight reading logic as advanced mode
    float rawWeight = LoadCell.getData();
    if (rawWeight < 0) {
      rawWeight = rawWeight * (-1);
    }
    currentWeight = rawWeight;
    
    // Check for stability using same logic as advanced mode
    if (currentWeight >= MIN_WEIGHT_THRESHOLD) {
      if (abs(currentWeight - lastDisplayWeight) < STABLE_THRESHOLD) {
        stableCount++;
        if (stableCount >= REQUIRED_STABLE_READINGS) {
          stableWeight = currentWeight;
        }
      } else {
        stableCount = 0;
        lastDisplayWeight = currentWeight;
      }
    } else {
      // Item removed - reset
      stableCount = 0;
      stableWeight = 0;
      lastDisplayWeight = 0;
    }
    
    lastWeightTime = millis();
  }

  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplaySimple();
    lastDisplayUpdate = millis();
  }

  if (millis() - lastBackgroundProcess >= BACKGROUND_INTERVAL) {
    checkWiFiStatus();
    lastBackgroundProcess = millis();
  }
}

void runWebAppMode() {
  server.handleClient();

  if (millis() - lastDisplayUpdate >= 2000) {
    lcd.setCursor(0, 0);
    lcd.print("Web App Active  ");
    lcd.setCursor(0, 1);
    lcd.print("192.168.4.1   ");
    lastDisplayUpdate = millis();
  }

  if (millis() - lastBackgroundProcess >= BACKGROUND_INTERVAL) {
    checkSDCardSpace();
    lastBackgroundProcess = millis();
  }
}

void updateDisplaySimple() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Simple Scale");
  lcd.setCursor(0, 1);
  
  // Use same stableWeight variable as advanced mode
  if (stableWeight > 0) {
    // Round to 1 decimal place for cleaner display (no fluctuation)
    lcd.print(String(stableWeight, 1) + " g");
  } else if (currentWeight >= MIN_WEIGHT_THRESHOLD) {
    lcd.print("Weighing...");
  } else {
    lcd.print("Place item");
  }
  
  // Show WiFi status indicator in bottom right corner
  lcd.setCursor(15, 1);
  lcd.print(wifiConnected ? "+" : "x");
}

void showStatsScreen() {
  String uptime = formatUptime(millis() - systemStartTime);
  lcd.clear();
  lcd.print("Uptime:");
  lcd.setCursor(0, 1);
  lcd.print(uptime.substring(0, 16));
  delay(1500);

  // Show ESP32 internal temperature
  lcd.clear();
  lcd.print("Temperature:");
  lcd.setCursor(0, 1);
  lcd.print(String(currentTemperature, 1) + " C");
  delay(1500);

  lcd.clear();
  lcd.print("WiFi:");
  lcd.setCursor(0, 1);
  lcd.print(wifiConnected ? "Connected" : "Offline");
  delay(1500);

  lcd.clear();
  lcd.print("SD Card:");
  lcd.setCursor(0, 1);
  if (sdCardReady) {
    lcd.print("OK");
  } else {
    lcd.print("Not OK");
  }
  delay(1500);

  lcd.clear();
  lcd.print("Queue:");
  lcd.setCursor(0, 1);
  lcd.print(String(pendingItems) + " pending");
  delay(1500);

  lcd.clear();
  lcd.print("Back to Work");
  delay(800);
  lastDisplayUpdate = 0;
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
  if (currentMode == MODE_WEBAPP) {
    return;
  }

  lcd.clear();
  lcd.print("WiFi Connecting");
  Serial.print("Connecting to WiFi: ");
  Serial.print(wifiSSID);

  if (wifiSSID.length() == 0) {
    wifiConnected = false;
    lcd.setCursor(0, 1);
    lcd.print("No SSID set");
    delay(1500);
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print('.');
    lcd.setCursor(0, 1);
    lcd.print("Try: " + String(attempts + 1) + "    ");
    attempts++;
  }

  wifiConnected = (WiFi.status() == WL_CONNECTED);

  lcd.clear();
  if (wifiConnected) {
    Serial.println("\nWiFi connected");
    lcd.print("WiFi Connected");
  } else {
    Serial.println("\nWiFi failed");
    lcd.print("Offline Mode");
  }
  delay(1200);
}

void checkWiFiStatus() {
  bool wasConnected = wifiConnected;
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  
  // Log WiFi state changes without rebooting - system handles offline mode gracefully
  if (!wasConnected && wifiConnected) {
    Serial.println("WiFi reconnected! System now online.");
  } else if (wasConnected && !wifiConnected) {
    Serial.println("WiFi disconnected! System now in offline mode.");
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
  
  // Universal functions that work in ALL modes (including webapp)
  if (key == '*') {
    if (currentMode != MODE_WEBAPP) {
      showStatsScreen();
    } else {
      // In webapp mode, * performs tare
      performManualTare();
    }
    return;
  } else if (key == '#') {
    // Universal reboot - works in ALL modes
    lcd.clear();
    lcd.print("Rebooting...");
    delay(1000);
    ESP.restart();
    return;
  }
  
  // Plant selection and override functionality
  if (currentState == STATE_PLANT_SELECTION || currentState == STATE_PLACE_ITEM) {
    if (key >= '0' && key <= '9') {
      bool isOverride = (currentState == STATE_PLACE_ITEM); // Check if changing existing selection
      
      selectedPlant = String(key);
      currentState = STATE_PLACE_ITEM;
      stateStartTime = millis();
      
      if (isOverride) {
        Serial.print("Plant changed to: ");
        Serial.println(selectedPlant);
        // Show confirmation message for override
        lcd.clear();
        lcd.print("Plant Changed!");
        lcd.setCursor(0, 1);
        lcd.print("Now Plant " + selectedPlant);
        delay(800);
      } else {
        Serial.print("Plant selected: ");
        Serial.println(selectedPlant);
      }
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
  
  // Show status indicators at extreme right corner
  lcd.setCursor(15, 1);
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
  lcd.print("* Stats  # Reboot");
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
  
  // Hide weight during weighing - just show please wait message
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");
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
          // Success beeps - 1 long + 1 short
          beep(200); // Long beep
          delay(100);
          beep(80);  // Short beep
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

// ===== Missing Function Implementations =====

String formatUptime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  
  String uptime = "";
  if (days > 0) {
    uptime += String(days) + "d ";
  }
  if (hours > 0) {
    uptime += String(hours) + "h ";
  }
  if (minutes > 0) {
    uptime += String(minutes) + "m ";
  }
  uptime += String(seconds) + "s";
  
  return uptime;
}

void checkSDCardSpace() {
  if (!sdCardReady) return;
  
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  uint64_t freeBytes = totalBytes - usedBytes;
  
  // Convert to MB for easier reading
  float totalMB = totalBytes / (1024.0 * 1024.0);
  float usedMB = usedBytes / (1024.0 * 1024.0);
  float freeMB = freeBytes / (1024.0 * 1024.0);
  
  Serial.printf("SD Card: %.1fMB total, %.1fMB used, %.1fMB free\n", totalMB, usedMB, freeMB);
  
  // Clean up if less than 10MB free
  if (freeMB < 10.0) {
    Serial.println("Low SD space, cleaning up old data...");
    cleanupOldData();
  }
}

void cleanupOldData() {
  if (!sdCardReady) return;
  
  // Simple cleanup - just truncate the queue file if it gets too large
  File file = SD.open(QUEUE_FILE_PATH, FILE_READ);
  if (!file) return;
  
  size_t fileSize = file.size();
  file.close();
  
  // If queue file is larger than 1MB, keep only the last 100 entries
  if (fileSize > 1024 * 1024) {
    Serial.println("Queue file too large, truncating...");
    
    File readFile = SD.open(QUEUE_FILE_PATH, FILE_READ);
    if (!readFile) return;
    
    String lines[100]; // Keep last 100 entries
    int lineCount = 0;
    
    // Read all lines
    while (readFile.available() && lineCount < 100) {
      String line = readFile.readStringUntil('\n');
      if (line.length() > 10) {
        lines[lineCount] = line;
        lineCount++;
      }
    }
    readFile.close();
    
    // Rewrite file with last entries only
    File writeFile = SD.open(QUEUE_FILE_PATH, FILE_WRITE);
    if (writeFile) {
      for (int i = 0; i < lineCount; i++) {
        writeFile.println(lines[i]);
      }
      writeFile.close();
      Serial.println("Queue file truncated successfully");
    }
  }
}

void setupWebServer() {
  // Basic web server setup for dashboard
  server.on("/", handleDashboard);
  server.on("/api/status", handleAPIStatus);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/sd/list", handleSDList);
  server.on("/api/sd/download", handleSDDownload);
  server.on("/api/sd/delete", handleSDDelete);
  
  Serial.println("Web server handlers configured");
}

void handleDashboard() {
  String html = "<!DOCTYPE html><html><head><title>Scale Dashboard</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
  html += ".card{background:white;padding:20px;margin:10px 0;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}";
  html += ".status{font-size:24px;font-weight:bold;color:#333;}";
  html += ".value{font-size:36px;color:#007bff;margin:10px 0;}";
  html += "button{background:#007bff;color:white;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;margin:5px;}";
  html += "button:hover{background:#0056b3;}";
  html += "input{padding:8px;margin:5px;border:1px solid #ddd;border-radius:4px;width:200px;}";
  html += "</style></head><body>";
  
  html += "<h1>ESP32 Scale Dashboard</h1>";
  
  html += "<div class='card'>";
  html += "<div class='status'>System Status</div>";
  html += "<p>WiFi: " + String(wifiConnected ? "Connected" : "Offline") + "</p>";
  html += "<p>SD Card: " + String(sdCardReady ? "Ready" : "Error") + "</p>";
  html += "<p>Pending Items: " + String(pendingItems) + "</p>";
  html += "<p>Uptime: " + formatUptime(millis() - systemStartTime) + "</p>";
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<div class='status'>Settings</div>";
  html += "<form action='/api/settings' method='post'>";
  html += "<p>WiFi SSID: <input type='text' name='ssid' value='" + wifiSSID + "'></p>";
  html += "<p>WiFi Password: <input type='text' name='pass' value='" + wifiPassword + "'></p>";
  html += "<p>Calibration Factor: <input type='number' step='0.1' name='cal' value='" + String(calibrationFactor) + "'></p>";
  html += "<p>Idle Timeout (seconds): <input type='number' name='idle' value='" + String(idleTimeout / 1000) + "'></p>";
  html += "<button type='submit'>Save Settings</button>";
  html += "</form>";
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<button onclick='location.reload()'>Refresh</button>";
  html += "<button onclick='fetch(\"/api/sd/delete\").then(()=>alert(\"Queue cleared!\"))'>Clear Queue</button>";
  html += "</div>";
  
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleAPIStatus() {
  StaticJsonDocument<300> doc;
  doc["weight"] = currentWeight;
  doc["wifi"] = wifiConnected;
  doc["sd_ready"] = sdCardReady;
  doc["pending"] = pendingItems;
  doc["uptime"] = millis() - systemStartTime;
  doc["mode"] = (currentMode == MODE_FULL) ? "full" : (currentMode == MODE_SIMPLE) ? "simple" : "webapp";
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetSettings() {
  StaticJsonDocument<200> doc;
  doc["ssid"] = wifiSSID;
  doc["pass"] = wifiPassword;
  doc["cal"] = calibrationFactor;
  doc["idle"] = idleTimeout / 1000; // Convert milliseconds to seconds
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handlePostSettings() {
  if (server.hasArg("ssid")) wifiSSID = server.arg("ssid");
  if (server.hasArg("pass")) wifiPassword = server.arg("pass");
  if (server.hasArg("cal")) calibrationFactor = server.arg("cal").toFloat();
  if (server.hasArg("idle")) {
    // Convert seconds to milliseconds
    idleTimeout = server.arg("idle").toInt() * 1000;
  }
  
  saveSettings();
  LoadCell.setCalFactor(calibrationFactor);
  
  server.send(200, "text/plain", "Settings saved! Restart to apply WiFi changes.");
}

void handleSDList() {
  String response = "[";
  if (sdCardReady) {
    File root = SD.open("/");
    File file = root.openNextFile();
    bool first = true;
    while (file) {
      if (!first) response += ",";
      response += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
      first = false;
      file = root.openNextFile();
    }
  }
  response += "]";
  server.send(200, "application/json", response);
}

void handleSDDownload() {
  String filename = server.arg("file");
  if (filename.length() == 0) filename = QUEUE_FILE_PATH;
  
  if (sdCardReady && SD.exists(filename)) {
    File file = SD.open(filename, FILE_READ);
    if (file) {
      server.streamFile(file, "application/octet-stream");
      file.close();
      return;
    }
  }
  server.send(404, "text/plain", "File not found");
}

void handleSDDelete() {
  if (sdCardReady && SD.exists(QUEUE_FILE_PATH)) {
    SD.remove(QUEUE_FILE_PATH);
    pendingItems = 0;
    server.send(200, "text/plain", "Queue cleared");
  } else {
    server.send(404, "text/plain", "Queue file not found");
  }
}
