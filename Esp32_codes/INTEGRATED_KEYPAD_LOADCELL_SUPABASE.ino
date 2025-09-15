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
const int READINGS_COUNT = 5; // Reduced for speed
const int READING_DELAY = 100; // Faster readings

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
  
  // System ready - blank screen
  lcd.clear();
}

void loop() {
  // Update load cell
  LoadCell.update();
  float weight = LoadCell.getData();
  
  // Check item presence
  itemPresent = (abs(weight) >= MIN_WEIGHT_THRESHOLD);
  
  // Handle keypad input
  char key = keypad.getKey();
  if (key) {
    handleKeyPress(key, weight);
  }
  

  
  // Keep screen blank unless showing result
  
  delay(50); // Faster loop
}

void initializeHardware() {
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
    if (itemPresent) {
      int plantNumber = (key == '0') ? 0 : (key - '0');
      processWeightInstantly(plantNumber, weight);
    }
  }
}

void processWeightInstantly(int plantNumber, float weight) {
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
  
  Serial.print("Plant ");
  Serial.print(plantNumber);
  Serial.print(" weight: ");
  Serial.print(averageWeight);
  Serial.println("g");
  
  // Send to Supabase instantly
  sendToSupabase(plantNumber, averageWeight);
  
  // Show "Done!" for 500ms
  lcd.clear();
  lcd.setCursor(6, 0);
  lcd.print("Done!");
  delay(500);
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

