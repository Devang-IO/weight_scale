/*
 * ULTRA FAST ESP32 Raw Data Sender - Industrial Grade
 * Features:
 * - Starts sending data immediately when item placed
 * - Stops sending and assigns plant when key pressed
 * - LCD stays blank until key press
 * - Session-based data grouping
 * - Ultra-fast industrial workflow
 */

#include <HX711_ADC.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ----------------- WiFi Configuration -----------------
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ----------------- Supabase Configuration -----------------
const char* supabaseUrl = "YOUR_SUPABASE_URL";
const char* supabaseKey = "YOUR_SUPABASE_ANON_KEY";

// ----------------- Hardware Pin Configuration -----------------
// HX711 Load Cell
const int HX711_DOUT = 23;
const int HX711_SCK  = 22;

// I2C LCD
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

// ----------------- Objects -----------------
HX711_ADC LoadCell(HX711_DOUT, HX711_SCK);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ----------------- Configuration -----------------
float calFactor = 2280.0;
const float MIN_WEIGHT_THRESHOLD = 5.0;
const int SEND_INTERVAL = 150; // Send data every 150ms

// ----------------- State Variables -----------------
bool itemPresent = false;
bool isRecording = false;
bool plantAssigned = false;
int currentPlant = -1;
String sessionId = "";
unsigned long lastSendTime = 0;
unsigned long keyPressTime = 0;
int readingCount = 0;

// Button animation character
byte buttonChar[8] = {
  0b11111,
  0b10001,
  0b10101,
  0b10001,
  0b10101,
  0b10001,
  0b11111,
  0b00000
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize hardware
  initializeHardware();
  
  // Connect to WiFi
  connectToWiFi();
  
  // Create custom button character
  lcd.createChar(0, buttonChar);
  
  // System ready - blank screen
  lcd.clear();
  Serial.println("System ready - Ultra Fast Raw Data Mode");
}

void loop() {
  // Update load cell
  LoadCell.update();
  float weight = LoadCell.getData();
  
  // Check item presence
  bool currentItemPresent = (abs(weight) >= MIN_WEIGHT_THRESHOLD);
  
  // Item state changed
  if (currentItemPresent != itemPresent) {
    itemPresent = currentItemPresent;
    
    if (itemPresent) {
      Serial.println("Item placed - starting data collection");
      startRecording();
    } else {
      Serial.println("Item removed");
      if (isRecording) {
        finishRecording();
      }
    }
  }
  
  // Handle keypad input
  char key = keypad.getKey();
  if (key) {
    handleKeyPress(key, weight);
  }
  
  // Send data if recording and plant not yet assigned
  if (isRecording && itemPresent && !plantAssigned) {
    sendWeightData(weight);
  }
  
  // Display current state (only after key press)
  displayCurrentState(weight);
  
  delay(20); // Ultra-fast loop
}

void initializeHardware() {
  // I2C LCD
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  
  // HX711 Load Cell
  LoadCell.begin();
  LoadCell.start(2000, true);
  LoadCell.setCalFactor(calFactor);
  
  Serial.println("Hardware initialized");
}

void connectToWiFi() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(attempts % 16, 1);
    lcd.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

void handleKeyPress(char key, float weight) {
  if (key >= '0' && key <= '9') {
    if (itemPresent && isRecording && !plantAssigned) {
      currentPlant = (key == '0') ? 0 : (key - '0');
      plantAssigned = true;
      keyPressTime = millis();
      
      // Assign plant to current session
      assignPlantToSession();
      
      // Show button animation and weight
      showButtonAnimation(key);
      
      Serial.print("Plant ");
      Serial.print(currentPlant);
      Serial.print(" assigned to session. Total readings: ");
      Serial.println(readingCount);
    }
  }
}

void startRecording() {
  isRecording = true;
  plantAssigned = false;
  readingCount = 0;
  lastSendTime = 0;
  currentPlant = -1;
  
  // Generate unique session ID (no plant assigned yet)
  sessionId = String(millis()) + "_temp";
  
  Serial.print("Started data collection - Session: ");
  Serial.println(sessionId);
}

void sendWeightData(float weight) {
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    int weightGrams = (int)round(abs(weight));
    
    // Send to Supabase with plant_number as NULL (will be updated later)
    bool success = sendRawDataToSupabase(-1, weightGrams, sessionId);
    
    if (success) {
      readingCount++;
      Serial.print("Sent reading #");
      Serial.print(readingCount);
      Serial.print(": ");
      Serial.print(weightGrams);
      Serial.println("g");
    }
    
    lastSendTime = millis();
  }
}

void finishRecording() {
  if (isRecording) {
    Serial.print("Finished session - Total readings: ");
    Serial.println(readingCount);
    
    // Reset state
    isRecording = false;
    plantAssigned = false;
    currentPlant = -1;
    sessionId = "";
    readingCount = 0;
    keyPressTime = 0;
    
    // Clear display
    lcd.clear();
  }
}

void assignPlantToSession() {
  // Update all readings in current session with the plant number
  updateSessionPlant(sessionId, currentPlant);
}

void showButtonAnimation(char key) {
  // Show weight and button animation for 1000ms
  lcd.clear();
  
  // Show current weight
  LoadCell.update();
  int weight = (int)round(abs(LoadCell.getData()));
  lcd.setCursor(0, 0);
  lcd.print(String(weight) + "g");
  
  // Show button animation
  lcd.setCursor(13, 0);
  lcd.write(byte(0)); // Custom button character
  lcd.setCursor(15, 0);
  lcd.print(key);
  
  lcd.setCursor(0, 1);
  lcd.print("Plant ");
  lcd.print((key == '0') ? 0 : (key - '0'));
  lcd.print(" saved!");
  
  delay(1000);
  lcd.clear();
}

void displayCurrentState(float weight) {
  // LCD stays blank until key is pressed
  // Only show display during the 1000ms after key press
  if (plantAssigned && (millis() - keyPressTime < 1000)) {
    // Display is handled in showButtonAnimation
    return;
  } else if (millis() - keyPressTime >= 1000 && plantAssigned) {
    // Clear display after animation
    lcd.clear();
  }
  // Otherwise, keep LCD blank
}

bool sendRawDataToSupabase(int plantNumber, int weight, String session) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  HTTPClient http;
  String url = String(supabaseUrl) + "/rest/v1/weight_readings";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  http.addHeader("Prefer", "return=minimal");
  
  // Create JSON payload
  DynamicJsonDocument doc(1024);
  if (plantNumber >= 0) {
    doc["plant_number"] = plantNumber;
  } else {
    doc["plant_number"] = nullptr; // Will be updated later
  }
  doc["weight"] = weight;
  doc["session_id"] = session;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  http.end();
  
  return (httpResponseCode == 200 || httpResponseCode == 201);
}

bool updateSessionPlant(String session, int plantNumber) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  
  HTTPClient http;
  String url = String(supabaseUrl) + "/rest/v1/weight_readings?session_id=eq." + session;
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  http.addHeader("Prefer", "return=minimal");
  
  // Create JSON payload for update
  DynamicJsonDocument doc(1024);
  doc["plant_number"] = plantNumber;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.sendRequest("PATCH", jsonString);
  http.end();
  
  return (httpResponseCode == 200 || httpResponseCode == 204);
}