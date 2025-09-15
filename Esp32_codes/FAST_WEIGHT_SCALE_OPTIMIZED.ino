/*
 * FAST OPTIMIZED ESP32 Weight Scale with Keypad and Supabase
 * Features:
 * - Ultra-fast HX711 Load Cell with stabilization detection
 * - 16x2 LCD I2C display with button press animation
 * - 3x4 Keypad for plant selection (keys 1-9, 0 for plant_0)
 * - WiFi connectivity and Supabase database integration
 * - Smart weight stabilization for instant readings
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
float calFactor = 2280.0; // Adjusted for proper 200g reading
const float MIN_WEIGHT_THRESHOLD = 5.0;
const float STABILITY_THRESHOLD = 2.0; // Weight must be stable within 2g
const int STABILITY_READINGS = 3; // Number of stable readings required
const int MAX_STABILIZATION_TIME = 2000; // Max time to wait for stabilization (ms)

// ----------------- State Variables -----------------
bool itemPresent = false;
float stableWeight = 0;
bool weightStabilized = false;
unsigned long stabilizationStartTime = 0;

// Button animation characters
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
}

void loop() {
  // Update load cell
  LoadCell.update();
  float weight = LoadCell.getData();
  
  // Check for weight stabilization
  checkWeightStabilization(weight);
  
  // Handle keypad input
  char key = keypad.getKey();
  if (key) {
    handleKeyPress(key);
  }
  
  // Display weight and status
  displayWeightOptimized(weight);
  
  delay(20); // Ultra-fast loop
}

void initializeHardware() {
  // I2C LCD
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  
  // HX711 Load Cell - optimized settings
  LoadCell.begin();
  LoadCell.start(2000, true); // Longer stabilization time, tare enabled
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

void checkWeightStabilization(float currentWeight) {
  static float previousReadings[STABILITY_READINGS];
  static int readingIndex = 0;
  static int stableCount = 0;
  
  bool currentItemPresent = (abs(currentWeight) >= MIN_WEIGHT_THRESHOLD);
  
  // Item state changed
  if (currentItemPresent != itemPresent) {
    itemPresent = currentItemPresent;
    weightStabilized = false;
    stableCount = 0;
    readingIndex = 0;
    
    if (itemPresent) {
      stabilizationStartTime = millis();
      Serial.println("Item placed - starting stabilization");
    } else {
      Serial.println("Item removed");
    }
  }
  
  // If item is present and not yet stabilized
  if (itemPresent && !weightStabilized) {
    previousReadings[readingIndex] = currentWeight;
    readingIndex = (readingIndex + 1) % STABILITY_READINGS;
    
    // Check if we have enough readings
    if (readingIndex == 0) {
      // Calculate stability
      float minReading = previousReadings[0];
      float maxReading = previousReadings[0];
      
      for (int i = 1; i < STABILITY_READINGS; i++) {
        if (previousReadings[i] < minReading) minReading = previousReadings[i];
        if (previousReadings[i] > maxReading) maxReading = previousReadings[i];
      }
      
      float variation = maxReading - minReading;
      
      if (variation <= STABILITY_THRESHOLD) {
        stableCount++;
        if (stableCount >= 2) { // Need 2 consecutive stable periods
          weightStabilized = true;
          stableWeight = (minReading + maxReading) / 2;
          Serial.print("Weight stabilized at: ");
          Serial.print((int)round(stableWeight));
          Serial.println("g");
        }
      } else {
        stableCount = 0;
      }
    }
    
    // Timeout check
    if (millis() - stabilizationStartTime > MAX_STABILIZATION_TIME) {
      weightStabilized = true;
      stableWeight = currentWeight;
      Serial.println("Stabilization timeout - using current weight");
    }
  }
}

void handleKeyPress(char key) {
  if (key >= '0' && key <= '9') {
    if (itemPresent && weightStabilized) {
      int plantNumber = (key == '0') ? 0 : (key - '0');
      
      // Show button press animation
      showButtonAnimation(key);
      
      // Process weight instantly using stable weight
      processWeightInstantly(plantNumber, stableWeight);
    } else if (itemPresent && !weightStabilized) {
      // Show "wait" message
      lcd.clear();
      lcd.setCursor(4, 0);
      lcd.print("Wait...");
      lcd.setCursor(2, 1);
      lcd.print("Stabilizing");
      delay(800);
    }
  }
}

void showButtonAnimation(char key) {
  // Show button press with custom character
  lcd.setCursor(15, 0);
  lcd.write(byte(0)); // Custom button character
  lcd.setCursor(15, 1);
  lcd.print(key);
  delay(200);
  
  // Clear animation
  lcd.setCursor(15, 0);
  lcd.print(" ");
  lcd.setCursor(15, 1);
  lcd.print(" ");
}

void processWeightInstantly(int plantNumber, float weight) {
  Serial.print("Plant ");
  Serial.print(plantNumber);
  Serial.print(" weight: ");
  Serial.print((int)round(weight));
  Serial.println("g");
  
  // Send to Supabase instantly
  sendToSupabase(plantNumber, weight);
  
  // Show "Done!" for 500ms
  lcd.clear();
  lcd.setCursor(6, 0);
  lcd.print("Done!");
  delay(500);
  
  // Reset stabilization for next item
  weightStabilized = false;
}

void displayWeightOptimized(float weight) {
  static String lastDisplay = "";
  static bool lastStabilized = false;
  String currentDisplay = "";
  
  if (itemPresent) {
    int wholeGrams = (int)round(abs(weight));
    currentDisplay = String(wholeGrams) + "g";
    
    if (wholeGrams >= 5000) {
      currentDisplay = "OVERLOAD";
    }
  }
  
  // Update display only when needed
  if (currentDisplay != lastDisplay || weightStabilized != lastStabilized) {
    lcd.clear();
    if (currentDisplay.length() > 0) {
      lcd.setCursor(0, 0);
      lcd.print(currentDisplay);
      
      if (weightStabilized) {
        lcd.setCursor(0, 1);
        lcd.print("Press key 0-9");
      } else {
        lcd.setCursor(0, 1);
        lcd.print("Stabilizing...");
      }
    }
    lastDisplay = currentDisplay;
    lastStabilized = weightStabilized;
  }
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
  doc[plantColumn] = (int)round
  
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