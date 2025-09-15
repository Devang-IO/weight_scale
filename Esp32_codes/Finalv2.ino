/*
 * Integrated ESP32 Weight Scale with Keypad and Supabase
 * Features:
 * - HX711 Load Cell for weight measurement
 * - 16x2 LCD I2C display
 * - 3x4 Keypad for plant selection (keys 1-9, 0 for plant_0)
 * - WiFi connectivity and Supabase database integration
 * - Automatic averaging of weight readings
 */

 //current response time - 1.27 - 2.08 seconds

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
float calFactor = 375.0;
const float MIN_WEIGHT_THRESHOLD = 5.0;
const int READINGS_COUNT = 3; // Further reduced for speed
const int READING_DELAY = 50; // Even faster readings

// ----------------- State Variables -----------------
bool itemPresent = false;
bool lastItemState = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize hardware
  initializeHardware();
  
  // Connect to WiFi
  connectToWiFi();
  
  // System ready - LCD off and blank screen
  lcd.clear();
  lcd.noBacklight();
}

void loop() {
  // Update load cell
  LoadCell.update();
  float weight = LoadCell.getData();
  
  // Debug output every 2 seconds
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 2000) {
    Serial.print("Weight: ");
    Serial.print(weight);
    Serial.print("g, Item present: ");
    Serial.println((abs(weight) >= MIN_WEIGHT_THRESHOLD) ? "YES" : "NO");
    lastDebug = millis();
  }
  
  // Check item presence
  itemPresent = (abs(weight) >= MIN_WEIGHT_THRESHOLD);
  
  // Handle keypad input
  char key = keypad.getKey();
  if (key) {
    Serial.print("Key pressed: ");
    Serial.print(key);
    Serial.print(", Weight: ");
    Serial.print(weight);
    Serial.print("g, Item present: ");
    Serial.println(itemPresent ? "YES" : "NO");
    handleKeyPress(key, weight);
  }
  
  delay(25); // Even faster loop
}

void initializeHardware() {
  // I2C LCD
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  // Keep backlight off as requested
  
  // HX711 Load Cell
  LoadCell.begin();
  LoadCell.start(1000);
  LoadCell.setCalFactor(calFactor);
  
  Serial.println("Hardware initialized");
}

void connectToWiFi() {
  // Turn on LCD only for WiFi connection status
  lcd.backlight();
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
    lcd.print("WiFi Connected!");
    delay(1000);
  } else {
    Serial.println("\nWiFi connection failed!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
    delay(1000);
  }
  
  // Turn off LCD after WiFi setup
  lcd.clear();
  lcd.noBacklight();
}

void handleKeyPress(char key, float weight) {
  if (key >= '0' && key <= '9') {
    // Plant selection keys
    if (itemPresent) {
      int plantNumber = (key == '0') ? 0 : (key - '0');
      processWeightInstantly(plantNumber, weight);
    }
  }
}

void processWeightInstantly(int plantNumber, float weight) {
  // Turn ON LCD backlight when displaying data
  lcd.backlight();
  
  // Take quick readings for average
  float readings[READINGS_COUNT];
  float sum = 0;
  
  for (int i = 0; i < READINGS_COUNT; i++) {
    LoadCell.update();
    readings[i] = abs(LoadCell.getData());
    sum += readings[i];
    delay(READING_DELAY);
  }
  
  float averageWeight = sum / READINGS_COUNT;
  
  // Show weight immediately when key is pressed
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Plant ");
  lcd.print(plantNumber);
  lcd.setCursor(0, 1);
  lcd.print(averageWeight, 1);
  lcd.print("g");
  
  Serial.print("Plant ");
  Serial.print(plantNumber);
  Serial.print(" weight: ");
  Serial.print(averageWeight);
  Serial.println("g");
  
  // Send to Supabase instantly (non-blocking approach)
  bool success = sendToSupabase(plantNumber, averageWeight);
  
  // Show result briefly
  lcd.setCursor(10, 1);
  if (success) {
    lcd.print("Sent!");
  } else {
    lcd.print("Error");
  }
  
  delay(1000); // Show result for 1 second
  
  // Turn OFF LCD backlight after displaying data
  lcd.clear();
  lcd.noBacklight();
}

bool sendToSupabase(int plantNumber, float weight) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return false;
  }
  
  HTTPClient http;
  String url = String(supabaseUrl) + "/rest/v1/weights";
  
  // Set shorter timeout for faster response
  http.setTimeout(3000); // 3 second timeout
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  http.addHeader("Prefer", "return=minimal");
  http.addHeader("Connection", "close"); // Close connection immediately
  
  // Create minimal JSON payload
  String jsonString = "{\"plant_" + String(plantNumber) + "\":" + String(weight, 1) + "}";
  
  Serial.print("Sending to Supabase: ");
  Serial.println(jsonString);
  
  unsigned long startTime = millis();
  int httpResponseCode = http.POST(jsonString);
  unsigned long responseTime = millis() - startTime;
  
  Serial.print("Response time: ");
  Serial.print(responseTime);
  Serial.println("ms");
  
  bool success = false;
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response: ");
    Serial.println(httpResponseCode);
    success = (httpResponseCode == 200 || httpResponseCode == 201);
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
  return success;
}

