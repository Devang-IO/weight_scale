/*
 * Integrated ESP32 Weight Scale with Keypad and Supabase
 * Features:
 * - HX711 Load Cell for weight measurement
 * - 16x2 LCD I2C display
 * - 3x4 Keypad for plant selection (keys 1-9, 0 for plant_0)
 * - WiFi connectivity and Supabase database integration
 * - Automatic averaging of weight readings
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

// Tare button
const int TARE_BTN = 25;

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
float calFactor = 375.0;
const float MIN_WEIGHT_THRESHOLD = 5.0;
const int READINGS_COUNT = 10; // Number of readings to average
const int READING_DELAY = 500; // Delay between readings in ms

// ----------------- State Variables -----------------
String lastDisplay = "";
bool isCollectingData = false;
int currentPlant = -1;
float weightReadings[READINGS_COUNT];
int readingIndex = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize hardware
  initializeHardware();
  
  // Connect to WiFi
  connectToWiFi();
  
  // Show ready message
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("SYSTEM READY");
  lcd.setCursor(0, 1);
  lcd.print("Place item & key");
  delay(2000);
  lcd.clear();
}

void loop() {
  // Update load cell
  LoadCell.update();
  float weight = LoadCell.getData();
  
  // Handle keypad input
  char key = keypad.getKey();
  if (key) {
    handleKeyPress(key, weight);
  }
  
  // Handle tare button
  if (digitalRead(TARE_BTN) == LOW) {
    performTare();
  }
  
  // Display current weight if item is present
  displayWeight(weight);
  
  // Handle data collection
  if (isCollectingData) {
    collectWeightData(weight);
  }
  
  delay(200);
}

void initializeHardware() {
  // Tare button
  pinMode(TARE_BTN, INPUT_PULLUP);
  
  // I2C LCD
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  
  // HX711 Load Cell
  LoadCell.begin();
  LoadCell.start(1000);
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
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    delay(1000);
  } else {
    Serial.println("\nWiFi connection failed!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed");
    delay(2000);
  }
}

void handleKeyPress(char key, float weight) {
  if (key >= '0' && key <= '9') {
    // Plant selection keys
    if (abs(weight) >= MIN_WEIGHT_THRESHOLD && !isCollectingData) {
      currentPlant = (key == '0') ? 0 : (key - '0');
      startDataCollection();
    } else if (abs(weight) < MIN_WEIGHT_THRESHOLD) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Place item first");
      delay(1500);
      lcd.clear();
    }
  } else if (key == '*') {
    // Cancel current operation
    if (isCollectingData) {
      cancelDataCollection();
    }
  } else if (key == '#') {
    // Clear/Reset
    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print("CLEARED");
    delay(1000);
    lcd.clear();
  }
}

void startDataCollection() {
  isCollectingData = true;
  readingIndex = 0;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Plant ");
  lcd.print(currentPlant);
  lcd.print(" - Reading");
  lcd.setCursor(0, 1);
  lcd.print("Progress: 0/");
  lcd.print(READINGS_COUNT);
  
  Serial.print("Starting data collection for Plant ");
  Serial.println(currentPlant);
}

void collectWeightData(float weight) {
  static unsigned long lastReadingTime = 0;
  
  if (millis() - lastReadingTime >= READING_DELAY) {
    if (abs(weight) >= MIN_WEIGHT_THRESHOLD) {
      weightReadings[readingIndex] = abs(weight);
      readingIndex++;
      
      // Update progress display
      lcd.setCursor(10, 1);
      lcd.print(readingIndex);
      
      Serial.print("Reading ");
      Serial.print(readingIndex);
      Serial.print("/");
      Serial.print(READINGS_COUNT);
      Serial.print(": ");
      Serial.print(weight);
      Serial.println("g");
      
      if (readingIndex >= READINGS_COUNT) {
        finishDataCollection();
      }
    } else {
      // Item removed during collection
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Item removed!");
      lcd.setCursor(0, 1);
      lcd.print("Collection stopped");
      delay(2000);
      cancelDataCollection();
    }
    
    lastReadingTime = millis();
  }
}

void finishDataCollection() {
  // Calculate average
  float sum = 0;
  for (int i = 0; i < READINGS_COUNT; i++) {
    sum += weightReadings[i];
  }
  float averageWeight = sum / READINGS_COUNT;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Avg: ");
  lcd.print((int)round(averageWeight));
  lcd.print("g");
  lcd.setCursor(0, 1);
  lcd.print("Sending to DB...");
  
  Serial.print("Average weight for Plant ");
  Serial.print(currentPlant);
  Serial.print(": ");
  Serial.print(averageWeight);
  Serial.println("g");
  
  // Send to Supabase
  bool success = sendToSupabase(currentPlant, averageWeight);
  
  // Show result
  lcd.clear();
  if (success) {
    lcd.setCursor(0, 0);
    lcd.print("Plant ");
    lcd.print(currentPlant);
    lcd.print(" Saved!");
    lcd.setCursor(0, 1);
    lcd.print("Weight: ");
    lcd.print((int)round(averageWeight));
    lcd.print("g");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Upload Failed!");
    lcd.setCursor(0, 1);
    lcd.print("Check connection");
  }
  
  delay(3000);
  
  // Reset state
  isCollectingData = false;
  currentPlant = -1;
  lcd.clear();
}

void cancelDataCollection() {
  isCollectingData = false;
  currentPlant = -1;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Collection");
  lcd.setCursor(0, 1);
  lcd.print("Cancelled");
  delay(1500);
  lcd.clear();
}

bool sendToSupabase(int plantNumber, float weight) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return false;
  }
  
  HTTPClient http;
  String url = String(supabaseUrl) + "/rest/v1/weights";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  http.addHeader("Prefer", "return=minimal");
  
  // Create JSON payload
  DynamicJsonDocument doc(1024);
  String plantColumn = "plant_" + String(plantNumber);
  doc[plantColumn] = weight;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.print("Sending to Supabase: ");
  Serial.println(jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("HTTP Response: ");
    Serial.println(httpResponseCode);
    Serial.print("Response: ");
    Serial.println(response);
    
    http.end();
    return (httpResponseCode == 200 || httpResponseCode == 201);
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(httpResponseCode);
    http.end();
    return false;
  }
}

void performTare() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Taring...");
  
  LoadCell.start(1000);
  
  lcd.setCursor(0, 1);
  lcd.print("Complete");
  delay(1000);
  lcd.clear();
  
  Serial.println("Tare completed");
}

void displayWeight(float weight) {
  String currentDisplay = "";
  
  if (!isCollectingData && abs(weight) >= MIN_WEIGHT_THRESHOLD) {
    int wholeGrams = (int)round(abs(weight));
    bool isNegative = weight < 0;
    
    char weightStr[16];
    snprintf(weightStr, sizeof(weightStr), "%s%d g", isNegative ? "-" : "", wholeGrams);
    currentDisplay = String(weightStr);
    
    if (wholeGrams >= 5000) {
      currentDisplay = "OVERLOADED";
    }
  }
  
  if (currentDisplay != lastDisplay && !isCollectingData) {
    if (currentDisplay.length() > 0) {
      lcd.clear();
      
      if (currentDisplay == "OVERLOADED") {
        lcd.setCursor(2, 0);
        lcd.print("OVERLOADED");
        lcd.setCursor(3, 1);
        lcd.print("5KG MAX!");
      } else {
        int len = currentDisplay.length();
        int startPos = (16 - len) / 2;
        if (startPos < 0) startPos = 0;
        
        lcd.setCursor(startPos, 0);
        lcd.print(currentDisplay);
        lcd.setCursor(0, 1);
        lcd.print("Press key 0-9");
      }
    }
    
    lastDisplay = currentDisplay;
  }
}