//ESP32 HX711 Load Cell Test with LCD Display
//Comprehensive testing of HX711 module with calibration and error control

#include <HX711_ADC.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// Pin definitions (from your existing code)
#define PIN_HX711_DOUT  23
#define PIN_HX711_SCK   22
#define PIN_I2C_SDA     18
#define PIN_I2C_SCL     19
#define PIN_TARE_BUTTON 25

// EEPROM address for calibration factor
#define EEPROM_CAL_ADDR 0
#define EEPROM_SIZE 512

// Hardware objects
HX711_ADC LoadCell(PIN_HX711_DOUT, PIN_HX711_SCK);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Load cell variables
float calibrationFactor = 360.0f; // Default calibration factor
float currentWeight = 0.0f;
float maxWeight = 0.0f;
float minWeight = 0.0f;
bool loadCellReady = false;
bool tareInProgress = false;

// Display modes
enum DisplayMode {
  MODE_WEIGHT,
  MODE_RAW_VALUE,
  MODE_CALIBRATION,
  MODE_STATISTICS,
  MODE_DIAGNOSTICS
};

DisplayMode currentMode = MODE_WEIGHT;
unsigned long lastModeSwitch = 0;
const unsigned long MODE_SWITCH_INTERVAL = 3000; // Switch every 3 seconds

// Error tracking
enum HX711Error {
  HX711_OK = 0,
  HX711_NOT_READY,
  HX711_TIMEOUT,
  HX711_CALIBRATION_ERROR,
  HX711_COMMUNICATION_ERROR,
  HX711_UNSTABLE_READING
};

struct HX711Status {
  bool initialized;
  bool ready;
  float lastReading;
  unsigned long lastReadTime;
  HX711Error lastError;
  String errorMessage;
  int consecutiveErrors;
};

HX711Status hx711Status;

// Statistics
struct WeightStats {
  float totalReadings;
  int readingCount;
  float averageWeight;
  float variance;
  unsigned long startTime;
};

WeightStats stats;

// LCD update control
String lastLine0 = "";
String lastLine1 = "";
unsigned long lastLCDUpdate = 0;
const unsigned long LCD_UPDATE_INTERVAL = 200; // Update LCD 5 times per second

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 HX711 Load Cell Test ===");
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize I2C and LCD
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  
  updateLCDSafe("HX711 Test v1.0", "Starting...");
  delay(2000);
  
  // Initialize tare button
  pinMode(PIN_TARE_BUTTON, INPUT_PULLUP);
  
  // Load calibration factor from EEPROM
  loadCalibrationFactor();
  
  // Initialize HX711
  initializeHX711();
  
  // Run comprehensive tests
  runHX711Tests();
  
  // Initialize statistics
  resetStatistics();
  
  Serial.println("\n=== HX711 Test Complete - Starting Live Display ===");
  Serial.println("Display modes will cycle every 3 seconds:");
  Serial.println("1. Weight Display");
  Serial.println("2. Raw ADC Value");
  Serial.println("3. Calibration Info");
  Serial.println("4. Statistics");
  Serial.println("5. Diagnostics");
  Serial.println("\nPress tare button (GPIO 25) to tare the scale");
}

void loop() {
  // Update HX711 readings
  updateHX711();
  
  // Handle tare button
  handleTareButton();
  
  // Switch display modes automatically
  if (millis() - lastModeSwitch > MODE_SWITCH_INTERVAL) {
    switchDisplayMode();
    lastModeSwitch = millis();
  }
  
  // Update display based on current mode
  updateDisplay();
  
  // Update statistics
  updateStatistics();
  
  // Print status to serial every 5 seconds
  static unsigned long lastSerialUpdate = 0;
  if (millis() - lastSerialUpdate > 5000) {
    printSerialStatus();
    lastSerialUpdate = millis();
  }
  
  delay(50); // 20Hz update rate
}

// ===== HX711 Initialization =====
void initializeHX711() {
  Serial.println("\n--- Initializing HX711 Load Cell ---");
  updateLCDSafe("Init HX711...", "Please wait...");
  
  // Initialize HX711
  LoadCell.begin();
  
  // Check if HX711 is connected
  unsigned long stabilizingtime = 2000; // Stabilizing time
  boolean _tare = true; // Set to false if you don't want tare to be performed
  
  LoadCell.start(stabilizingtime, _tare);
  
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("ERROR: HX711 timeout, check wiring and pin definitions");
    hx711Status.initialized = false;
    hx711Status.lastError = HX711_TIMEOUT;
    hx711Status.errorMessage = "Initialization timeout";
    
    updateLCDSafe("HX711 ERROR!", "Check Wiring");
    delay(3000);
    return;
  }
  
  // Set calibration factor
  LoadCell.setCalFactor(calibrationFactor);
  
  hx711Status.initialized = true;
  loadCellReady = true;
  
  Serial.println("SUCCESS: HX711 initialized successfully!");
  Serial.printf("Calibration factor: %.2f\n", calibrationFactor);
  
  updateLCDSafe("HX711 Ready!", "Cal: " + String(calibrationFactor, 1));
  delay(2000);
}

// ===== HX711 Test Functions =====
void runHX711Tests() {
  if (!hx711Status.initialized) {
    Serial.println("Cannot run tests - HX711 not initialized");
    return;
  }
  
  Serial.println("\n=== Running HX711 Tests ===");
  
  // Test 1: Basic reading test
  testBasicReading();
  
  // Test 2: Stability test
  testStability();
  
  // Test 3: Tare function test
  testTareFunction();
  
  // Test 4: Calibration verification
  testCalibrationVerification();
  
  // Test 5: Speed test
  testReadingSpeed();
  
  Serial.println("=== All HX711 Tests Complete ===");
}

void testBasicReading() {
  Serial.println("\n--- Test 1: Basic Reading ---");
  updateLCDSafe("Test: Reading", "Sampling...");
  
  for (int i = 0; i < 10; i++) {
    LoadCell.update();
    
    if (LoadCell.getData() != hx711Status.lastReading || i == 0) {
      float weight = LoadCell.getData();
      hx711Status.lastReading = weight;
      
      Serial.printf("Reading %d: %.2f g\n", i+1, weight);
      
      updateLCDSafe("Test: Reading", String(weight, 2) + "g (" + String(i+1) + "/10)");
      delay(500);
    }
  }
  
  Serial.println("SUCCESS: Basic reading test completed");
}

void testStability() {
  Serial.println("\n--- Test 2: Stability Test ---");
  updateLCDSafe("Test: Stability", "10 sec test...");
  
  float readings[20];
  float sum = 0;
  
  // Take 20 readings over 10 seconds
  for (int i = 0; i < 20; i++) {
    LoadCell.update();
    readings[i] = LoadCell.getData();
    sum += readings[i];
    
    updateLCDSafe("Test: Stability", String(readings[i], 2) + "g (" + String(i+1) + "/20)");
    delay(500);
  }
  
  // Calculate statistics
  float average = sum / 20;
  float variance = 0;
  
  for (int i = 0; i < 20; i++) {
    variance += pow(readings[i] - average, 2);
  }
  variance /= 20;
  float stdDev = sqrt(variance);
  
  Serial.printf("Average: %.2f g\n", average);
  Serial.printf("Standard Deviation: %.2f g\n", stdDev);
  Serial.printf("Variance: %.2f\n", variance);
  
  updateLCDSafe("Stability Test", "StdDev: " + String(stdDev, 2) + "g");
  delay(2000);
  
  if (stdDev < 1.0) {
    Serial.println("SUCCESS: Load cell is stable");
  } else {
    Serial.println("WARNING: Load cell readings are unstable");
    hx711Status.lastError = HX711_UNSTABLE_READING;
  }
}

void testTareFunction() {
  Serial.println("\n--- Test 3: Tare Function ---");
  updateLCDSafe("Test: Tare", "Before tare...");
  
  // Read weight before tare
  LoadCell.update();
  float beforeTare = LoadCell.getData();
  Serial.printf("Weight before tare: %.2f g\n", beforeTare);
  
  delay(2000);
  
  // Perform tare
  updateLCDSafe("Test: Tare", "Taring...");
  LoadCell.tareNoDelay();
  
  // Wait for tare to complete
  boolean tareStatus = false;
  unsigned long tareStart = millis();
  
  while (!tareStatus && (millis() - tareStart) < 5000) {
    LoadCell.update();
    tareStatus = LoadCell.getTareStatus();
    delay(10);
  }
  
  if (tareStatus) {
    LoadCell.update();
    float afterTare = LoadCell.getData();
    Serial.printf("Weight after tare: %.2f g\n", afterTare);
    
    updateLCDSafe("Test: Tare", "After: " + String(afterTare, 2) + "g");
    delay(2000);
    
    if (abs(afterTare) < 1.0) {
      Serial.println("SUCCESS: Tare function working correctly");
    } else {
      Serial.println("WARNING: Tare function may not be working properly");
    }
  } else {
    Serial.println("ERROR: Tare timeout");
    hx711Status.lastError = HX711_TIMEOUT;
  }
}

void testCalibrationVerification() {
  Serial.println("\n--- Test 4: Calibration Verification ---");
  updateLCDSafe("Test: Cal Check", "Current cal...");
  
  float currentCal = LoadCell.getCalFactor();
  Serial.printf("Current calibration factor: %.2f\n", currentCal);
  
  updateLCDSafe("Test: Cal Check", "Cal: " + String(currentCal, 1));
  delay(2000);
  
  // Test with different calibration factors
  float testFactors[] = {300.0, 360.0, 420.0};
  
  for (int i = 0; i < 3; i++) {
    LoadCell.setCalFactor(testFactors[i]);
    LoadCell.update();
    float weight = LoadCell.getData();
    
    Serial.printf("Cal %.0f: Weight = %.2f g\n", testFactors[i], weight);
    updateLCDSafe("Cal: " + String(testFactors[i], 0), "Weight: " + String(weight, 2) + "g");
    delay(1500);
  }
  
  // Restore original calibration factor
  LoadCell.setCalFactor(calibrationFactor);
  Serial.println("Calibration factor restored to: " + String(calibrationFactor));
}

void testReadingSpeed() {
  Serial.println("\n--- Test 5: Reading Speed Test ---");
  updateLCDSafe("Test: Speed", "Measuring...");
  
  unsigned long startTime = millis();
  int readingCount = 0;
  
  // Take readings for 5 seconds
  while (millis() - startTime < 5000) {
    LoadCell.update();
    if (LoadCell.getData() != hx711Status.lastReading) {
      readingCount++;
      hx711Status.lastReading = LoadCell.getData();
    }
  }
  
  float readingsPerSecond = readingCount / 5.0;
  Serial.printf("Readings per second: %.1f Hz\n", readingsPerSecond);
  
  updateLCDSafe("Speed Test", String(readingsPerSecond, 1) + " Hz");
  delay(2000);
  
  if (readingsPerSecond >= 10) {
    Serial.println("SUCCESS: Reading speed is good");
  } else {
    Serial.println("WARNING: Reading speed is low");
  }
}

// ===== HX711 Update Functions =====
void updateHX711() {
  if (!loadCellReady) return;
  
  static unsigned long lastUpdate = 0;
  
  // Update HX711 at high frequency
  LoadCell.update();
  
  // Get new reading every 50ms
  if (millis() - lastUpdate >= 50) {
    if (LoadCell.getData() != hx711Status.lastReading) {
      currentWeight = LoadCell.getData();
      hx711Status.lastReading = currentWeight;
      hx711Status.lastReadTime = millis();
      hx711Status.ready = true;
      hx711Status.lastError = HX711_OK;
      hx711Status.consecutiveErrors = 0;
      
      // Update min/max
      if (currentWeight > maxWeight) maxWeight = currentWeight;
      if (currentWeight < minWeight) minWeight = currentWeight;
      
    } else {
      // Check for communication timeout
      if (millis() - hx711Status.lastReadTime > 1000) {
        hx711Status.lastError = HX711_COMMUNICATION_ERROR;
        hx711Status.consecutiveErrors++;
      }
    }
    
    lastUpdate = millis();
  }
}

void handleTareButton() {
  static bool lastButtonState = HIGH;
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 50;
  
  bool buttonState = digitalRead(PIN_TARE_BUTTON);
  
  if (buttonState != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (buttonState == LOW && lastButtonState == HIGH) {
      // Button pressed
      performTare();
    }
  }
  
  lastButtonState = buttonState;
}

void performTare() {
  if (!loadCellReady || tareInProgress) return;
  
  Serial.println("Performing tare...");
  tareInProgress = true;
  
  updateLCDSafe("Taring Scale...", "Please wait...");
  
  LoadCell.tareNoDelay();
  
  // Wait for tare to complete
  boolean tareStatus = false;
  unsigned long tareStart = millis();
  
  while (!tareStatus && (millis() - tareStart) < 5000) {
    LoadCell.update();
    tareStatus = LoadCell.getTareStatus();
    delay(10);
  }
  
  if (tareStatus) {
    Serial.println("Tare completed successfully");
    updateLCDSafe("Tare Complete!", "Scale zeroed");
    
    // Reset min/max values
    maxWeight = 0;
    minWeight = 0;
    resetStatistics();
  } else {
    Serial.println("Tare timeout!");
    updateLCDSafe("Tare Failed!", "Timeout error");
    hx711Status.lastError = HX711_TIMEOUT;
  }
  
  delay(1500);
  tareInProgress = false;
}

// ===== Display Functions =====
void updateLCDSafe(String line0, String line1) {
  // Only update LCD if content has changed or enough time has passed
  if (millis() - lastLCDUpdate < LCD_UPDATE_INTERVAL && 
      line0 == lastLine0 && line1 == lastLine1) {
    return;
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);
  
  lastLine0 = line0;
  lastLine1 = line1;
  lastLCDUpdate = millis();
}

void switchDisplayMode() {
  currentMode = (DisplayMode)((currentMode + 1) % 5);
  
  String modeNames[] = {"Weight", "Raw Value", "Calibration", "Statistics", "Diagnostics"};
  Serial.println("Switched to display mode: " + modeNames[currentMode]);
}

void updateDisplay() {
  if (!loadCellReady || tareInProgress) {
    return;
  }
  
  switch (currentMode) {
    case MODE_WEIGHT:
      displayWeight();
      break;
    case MODE_RAW_VALUE:
      displayRawValue();
      break;
    case MODE_CALIBRATION:
      displayCalibration();
      break;
    case MODE_STATISTICS:
      displayStatistics();
      break;
    case MODE_DIAGNOSTICS:
      displayDiagnostics();
      break;
  }
}

void displayWeight() {
  String line1 = "Weight:";
  String line2 = String(currentWeight, 2) + " g";
  
  // Add status indicators
  if (hx711Status.lastError != HX711_OK) {
    line2 += " ERR";
  } else if (abs(currentWeight) < 0.5) {
    line2 += " ZERO";
  } else if (currentWeight > 0) {
    line2 += " +";
  }
  
  updateLCDSafe(line1, line2);
}

void displayRawValue() {
  LoadCell.update();
  long rawValue = LoadCell.getData() * LoadCell.getCalFactor();
  
  String line1 = "Raw ADC Value:";
  String line2 = String(rawValue);
  
  updateLCDSafe(line1, line2);
}

void displayCalibration() {
  String line1 = "Calibration:";
  String line2 = String(calibrationFactor, 1);
  
  updateLCDSafe(line1, line2);
}

void displayStatistics() {
  static int statsPage = 0;
  static unsigned long lastStatsSwitch = 0;
  
  // Switch stats page every 2 seconds
  if (millis() - lastStatsSwitch > 2000) {
    statsPage = (statsPage + 1) % 3;
    lastStatsSwitch = millis();
  }
  
  String line1 = "";
  String line2 = "";
  
  switch (statsPage) {
    case 0: // Average
      line1 = "Average Weight:";
      line2 = String(stats.averageWeight, 2) + " g";
      break;
      
    case 1: // Min/Max
      line1 = "Min: " + String(minWeight, 1) + "g";
      line2 = "Max: " + String(maxWeight, 1) + "g";
      break;
      
    case 2: // Reading count
      line1 = "Readings:";
      line2 = String(stats.readingCount);
      break;
  }
  
  updateLCDSafe(line1, line2);
}

void displayDiagnostics() {
  static int diagPage = 0;
  static unsigned long lastDiagSwitch = 0;
  
  // Switch diagnostic page every 2 seconds
  if (millis() - lastDiagSwitch > 2000) {
    diagPage = (diagPage + 1) % 3;
    lastDiagSwitch = millis();
  }
  
  String line1 = "";
  String line2 = "";
  
  switch (diagPage) {
    case 0: // Status
      line1 = "HX711 Status:";
      if (hx711Status.ready) {
        line2 = "Ready";
      } else {
        line2 = "Not Ready";
      }
      break;
      
    case 1: // Last Error
      line1 = "Last Error:";
      if (hx711Status.lastError == HX711_OK) {
        line2 = "None";
      } else {
        line2 = "Code: " + String(hx711Status.lastError);
      }
      break;
      
    case 2: // Uptime
      line1 = "Uptime:";
      unsigned long uptimeSeconds = millis() / 1000;
      unsigned long hours = uptimeSeconds / 3600;
      unsigned long minutes = (uptimeSeconds % 3600) / 60;
      line2 = String(hours) + "h " + String(minutes) + "m";
      break;
  }
  
  updateLCDSafe(line1, line2);
}

// ===== Statistics Functions =====
void resetStatistics() {
  stats.totalReadings = 0;
  stats.readingCount = 0;
  stats.averageWeight = 0;
  stats.variance = 0;
  stats.startTime = millis();
  maxWeight = currentWeight;
  minWeight = currentWeight;
}

void updateStatistics() {
  static unsigned long lastStatsUpdate = 0;
  
  if (millis() - lastStatsUpdate >= 100) { // Update stats every 100ms
    stats.totalReadings += currentWeight;
    stats.readingCount++;
    stats.averageWeight = stats.totalReadings / stats.readingCount;
    
    lastStatsUpdate = millis();
  }
}

// ===== EEPROM Functions =====
void loadCalibrationFactor() {
  float storedCal;
  EEPROM.get(EEPROM_CAL_ADDR, storedCal);
  
  // Check if stored value is valid
  if (storedCal > 0 && storedCal < 10000) {
    calibrationFactor = storedCal;
    Serial.println("Loaded calibration factor from EEPROM: " + String(calibrationFactor));
  } else {
    Serial.println("Using default calibration factor: " + String(calibrationFactor));
    saveCalibrationFactor(); // Save default value
  }
}

void saveCalibrationFactor() {
  EEPROM.put(EEPROM_CAL_ADDR, calibrationFactor);
  EEPROM.commit();
  Serial.println("Calibration factor saved to EEPROM: " + String(calibrationFactor));
}

// ===== Serial Status Functions =====
void printSerialStatus() {
  Serial.println("\n=== HX711 Status Update ===");
  
  if (hx711Status.initialized) {
    Serial.println("HX711: Initialized ✓");
    Serial.printf("Current Weight: %.2f g\n", currentWeight);
    Serial.printf("Calibration Factor: %.2f\n", calibrationFactor);
    Serial.printf("Min Weight: %.2f g\n", minWeight);
    Serial.printf("Max Weight: %.2f g\n", maxWeight);
    Serial.printf("Average Weight: %.2f g\n", stats.averageWeight);
    Serial.printf("Total Readings: %d\n", stats.readingCount);
    
    if (hx711Status.lastError != HX711_OK) {
      Serial.println("Last Error: " + String(hx711Status.lastError));
      Serial.println("Consecutive Errors: " + String(hx711Status.consecutiveErrors));
    }
  } else {
    Serial.println("HX711: Not Initialized ✗");
  }
  
  Serial.println("Display Mode: " + String(currentMode));
  Serial.println("Uptime: " + formatUptime(millis()));
}

String formatUptime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  
  String uptime = "";
  if (days > 0) uptime += String(days) + "d ";
  if (hours > 0) uptime += String(hours) + "h ";
  if (minutes > 0) uptime += String(minutes) + "m ";
  uptime += String(seconds) + "s";
  
  return uptime;
}