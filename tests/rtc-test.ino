//ESP32 RTC Module Test with LCD Display and WiFi Time Sync
//Tests DS3231 RTC module with full error control, LCD output, and NTP sync

#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <time.h>

// WiFi Configuration - UPDATE THESE!
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Timezone Configuration for India (IST = UTC+5:30)
#define IST_OFFSET_SEC 19800  // 5 hours 30 minutes in seconds
#define DST_OFFSET_SEC 0      // India doesn't use DST

// I2C pins (from your existing code)
#define PIN_I2C_SDA 18
#define PIN_I2C_SCL 19

// Hardware objects
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// RTC status variables
bool rtcInitialized = false;
bool rtcTimeValid = false;
bool wifiConnected = false;
bool timeSynced = false;
DateTime lastValidTime;

// LCD flickering prevention
String lastLine0 = "";
String lastLine1 = "";
unsigned long lastLCDUpdate = 0;
const unsigned long LCD_UPDATE_INTERVAL = 1000; // Update LCD only once per second

// Display modes
enum DisplayMode {
  MODE_TIME_DATE,
  MODE_TEMPERATURE,
  MODE_UPTIME,
  MODE_DETAILED_INFO
};

DisplayMode currentMode = MODE_TIME_DATE;
unsigned long lastModeSwitch = 0;
const unsigned long MODE_SWITCH_INTERVAL = 5000; // Switch every 5 seconds

// Error tracking
enum RTCError {
  RTC_OK = 0,
  RTC_NOT_FOUND,
  RTC_TIME_INVALID,
  RTC_LOST_POWER,
  RTC_COMMUNICATION_ERROR,
  RTC_TEMPERATURE_ERROR
};

struct RTCStatus {
  bool initialized;
  bool timeValid;
  bool lostPower;
  float temperature;
  RTCError lastError;
  String errorMessage;
};

RTCStatus rtcStatus;

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 RTC Module Test with LCD ===");
  
  // Initialize I2C
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.println("I2C initialized on pins SDA:" + String(PIN_I2C_SDA) + " SCL:" + String(PIN_I2C_SCL));
  
  // Initialize LCD
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RTC Test v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  
  delay(2000);
  
  // Connect to WiFi for time sync
  connectToWiFi();
  
  // Initialize RTC
  initializeRTC();
  
  // Sync time with NTP if WiFi connected
  if (wifiConnected) {
    syncTimeWithNTP();
  }
  
  // Run comprehensive RTC tests
  runRTCTests();
  
  Serial.println("\n=== RTC Test Complete - Starting Live Display ===");
  Serial.println("Live display modes will cycle every 5 seconds:");
  Serial.println("1. Time & Date");
  Serial.println("2. Temperature");
  Serial.println("3. Uptime");
  Serial.println("4. Detailed Info");
}

void loop() {
  // Update RTC status
  updateRTCStatus();
  
  // Switch display modes automatically
  if (millis() - lastModeSwitch > MODE_SWITCH_INTERVAL) {
    switchDisplayMode();
    lastModeSwitch = millis();
  }
  
  // Update display based on current mode
  updateDisplay();
  
  // Print status to serial every 10 seconds
  static unsigned long lastSerialUpdate = 0;
  if (millis() - lastSerialUpdate > 10000) {
    printSerialStatus();
    lastSerialUpdate = millis();
  }
  
  // Check for WiFi reconnection every 30 seconds
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) {
    checkWiFiConnection();
    lastWiFiCheck = millis();
  }
  
  delay(100); // Faster loop, but LCD updates are controlled
}

// ===== WiFi Functions =====
void connectToWiFi() {
  Serial.println("\n--- Connecting to WiFi ---");
  
  updateLCDSafe("WiFi Connect...", "Attempting...");
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    updateLCDSafe("WiFi Connect...", "Try: " + String(attempts));
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    updateLCDSafe("WiFi Connected!", WiFi.localIP().toString());
    delay(2000);
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi connection failed!");
    
    updateLCDSafe("WiFi Failed!", "Offline Mode");
    delay(2000);
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    Serial.println("WiFi disconnected, attempting reconnect...");
    wifiConnected = false;
    connectToWiFi();
  }
}

void syncTimeWithNTP() {
  if (!wifiConnected) {
    Serial.println("Cannot sync time - WiFi not connected");
    return;
  }
  
  Serial.println("\n--- Syncing Time with NTP ---");
  updateLCDSafe("NTP Sync...", "Getting time...");
  
  // Configure NTP for Indian Standard Time (IST = UTC+5:30)
  Serial.println("Configuring timezone for IST (UTC+5:30)...");
  configTime(IST_OFFSET_SEC, DST_OFFSET_SEC, "in.pool.ntp.org", "pool.ntp.org", "time.nist.gov");
  
  struct tm timeinfo;
  int attempts = 0;
  
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    delay(1000);
    attempts++;
    Serial.print(".");
    updateLCDSafe("NTP Sync...", "Try: " + String(attempts));
  }
  
  if (attempts < 10) {
    // Successfully got NTP time
    Serial.println("\nNTP time received!");
    
    // Convert to DateTime and set RTC
    DateTime ntpTime = DateTime(
      timeinfo.tm_year + 1900,
      timeinfo.tm_mon + 1,
      timeinfo.tm_mday,
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec
    );
    
    // Get current RTC time for comparison
    DateTime rtcTime = rtc.now();
    long timeDiff = abs((long)(ntpTime.unixtime() - rtcTime.unixtime()));
    
    Serial.printf("RTC Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  rtcTime.year(), rtcTime.month(), rtcTime.day(),
                  rtcTime.hour(), rtcTime.minute(), rtcTime.second());
    
    Serial.printf("NTP Time (IST): %04d-%02d-%02d %02d:%02d:%02d\n",
                  ntpTime.year(), ntpTime.month(), ntpTime.day(),
                  ntpTime.hour(), ntpTime.minute(), ntpTime.second());
    
    Serial.printf("Time difference: %ld seconds\n", timeDiff);
    
    if (timeDiff > 5) { // Only update if difference is more than 5 seconds
      rtc.adjust(ntpTime);
      Serial.println("RTC time updated with NTP time!");
      updateLCDSafe("Time Synced!", "Diff: " + String(timeDiff) + "s");
      timeSynced = true;
    } else {
      Serial.println("RTC time is already accurate");
      updateLCDSafe("Time OK!", "Diff: " + String(timeDiff) + "s");
      timeSynced = true;
    }
    
    delay(3000);
  } else {
    Serial.println("\nFailed to get NTP time");
    updateLCDSafe("NTP Failed!", "Using RTC time");
    delay(2000);
  }
}

// ===== LCD Helper Functions =====
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

// ===== RTC Initialization =====
void initializeRTC() {
  Serial.println("\n--- Initializing RTC Module ---");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Init RTC...");
  
  if (!rtc.begin()) {
    Serial.println("ERROR: RTC not found!");
    rtcStatus.initialized = false;
    rtcStatus.lastError = RTC_NOT_FOUND;
    rtcStatus.errorMessage = "RTC module not found";
    
    lcd.setCursor(0, 1);
    lcd.print("RTC NOT FOUND!");
    delay(3000);
    return;
  }
  
  rtcStatus.initialized = true;
  rtcInitialized = true;
  Serial.println("SUCCESS: RTC module found and initialized!");
  
  // Check if RTC lost power
  if (rtc.lostPower()) {
    Serial.println("WARNING: RTC lost power, setting time to compile time");
    rtcStatus.lostPower = true;
    
    // Set to compile time
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    
    lcd.setCursor(0, 1);
    lcd.print("Power Lost!");
    delay(2000);
  } else {
    rtcStatus.lostPower = false;
    Serial.println("RTC time is valid (no power loss detected)");
  }
  
  // Test temperature sensor
  float temp = rtc.getTemperature();
  if (temp > -40 && temp < 85) { // Valid temperature range for DS3231
    rtcStatus.temperature = temp;
    Serial.printf("RTC Temperature: %.2f°C\n", temp);
  } else {
    rtcStatus.lastError = RTC_TEMPERATURE_ERROR;
    rtcStatus.errorMessage = "Temperature sensor error";
    Serial.println("WARNING: Temperature sensor may not be working");
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RTC Ready!");
  lcd.setCursor(0, 1);
  lcd.print("Temp: " + String(temp, 1) + "C");
  delay(2000);
}

// ===== RTC Test Functions =====
void runRTCTests() {
  if (!rtcInitialized) {
    Serial.println("Cannot run tests - RTC not initialized");
    return;
  }
  
  Serial.println("\n=== Running RTC Tests ===");
  
  // Test 1: Basic time reading
  testTimeReading();
  
  // Test 2: Time setting and verification
  testTimeSetting();
  
  // Test 3: Temperature reading
  testTemperatureReading();
  
  // Test 4: Alarm functionality (if supported)
  testAlarmFunctionality();
  
  // Test 5: 32kHz output test
  test32kHzOutput();
  
  Serial.println("=== All RTC Tests Complete ===");
}

void testTimeReading() {
  Serial.println("\n--- Test 1: Time Reading ---");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Test: Time Read");
  
  for (int i = 0; i < 5; i++) {
    DateTime now = rtc.now();
    
    if (now.year() < 2020 || now.year() > 2050) {
      Serial.println("ERROR: Invalid year detected: " + String(now.year()));
      rtcStatus.timeValid = false;
      rtcStatus.lastError = RTC_TIME_INVALID;
      
      lcd.setCursor(0, 1);
      lcd.print("Invalid Time!");
      delay(2000);
      return;
    }
    
    Serial.printf("Reading %d: %04d-%02d-%02d %02d:%02d:%02d\n", 
                  i+1, now.year(), now.month(), now.day(), 
                  now.hour(), now.minute(), now.second());
    
    lcd.setCursor(0, 1);
    lcd.printf("%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    
    delay(1000);
  }
  
  rtcStatus.timeValid = true;
  Serial.println("SUCCESS: Time reading test passed");
}

void testTimeSetting() {
  Serial.println("\n--- Test 2: Time Setting ---");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Test: Set Time");
  
  // Save current time
  DateTime originalTime = rtc.now();
  
  // Set a test time (1 minute in the future)
  DateTime testTime = DateTime(originalTime.unixtime() + 60);
  rtc.adjust(testTime);
  
  delay(1000);
  
  // Read back the time
  DateTime readTime = rtc.now();
  
  // Check if the time was set correctly (within 2 seconds tolerance)
  long timeDiff = abs((long)(readTime.unixtime() - testTime.unixtime()));
  
  if (timeDiff <= 2) {
    Serial.println("SUCCESS: Time setting test passed");
    lcd.setCursor(0, 1);
    lcd.print("Set Time: OK");
  } else {
    Serial.printf("ERROR: Time setting failed. Diff: %ld seconds\n", timeDiff);
    lcd.setCursor(0, 1);
    lcd.print("Set Time: FAIL");
  }
  
  // Restore original time
  rtc.adjust(originalTime);
  
  delay(2000);
}

void testTemperatureReading() {
  Serial.println("\n--- Test 3: Temperature Reading ---");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Test: Temp Read");
  
  for (int i = 0; i < 3; i++) {
    float temp = rtc.getTemperature();
    
    Serial.printf("Temperature reading %d: %.2f°C\n", i+1, temp);
    
    lcd.setCursor(0, 1);
    lcd.printf("%.1fC (Read %d)", temp, i+1);
    
    if (temp < -40 || temp > 85) {
      Serial.println("WARNING: Temperature out of expected range");
      rtcStatus.lastError = RTC_TEMPERATURE_ERROR;
    } else {
      rtcStatus.temperature = temp;
    }
    
    delay(1000);
  }
  
  Serial.println("Temperature reading test complete");
}

void testAlarmFunctionality() {
  Serial.println("\n--- Test 4: Alarm Functionality ---");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Test: Alarms");
  
  // Clear any existing alarms
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  
  // Set alarm 1 for 5 seconds from now
  DateTime now = rtc.now();
  DateTime alarmTime = DateTime(now.unixtime() + 5);
  
  rtc.setAlarm1(alarmTime, DS3231_A1_Hour);
  
  Serial.println("Alarm set for 5 seconds from now");
  lcd.setCursor(0, 1);
  lcd.print("Alarm in 5s");
  
  // Wait and check for alarm
  for (int i = 5; i > 0; i--) {
    lcd.setCursor(12, 1);
    lcd.print(String(i));
    delay(1000);
  }
  
  if (rtc.alarmFired(1)) {
    Serial.println("SUCCESS: Alarm 1 fired correctly");
    lcd.setCursor(0, 1);
    lcd.print("Alarm: OK");
    rtc.clearAlarm(1);
  } else {
    Serial.println("WARNING: Alarm 1 did not fire");
    lcd.setCursor(0, 1);
    lcd.print("Alarm: No Fire");
  }
  
  delay(2000);
}

void test32kHzOutput() {
  Serial.println("\n--- Test 5: Square Wave Output ---");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Test: SQW Out");
  
  // Enable 1Hz square wave output (DS3231_SquareWave32kHz not available in all libraries)
  rtc.writeSqwPinMode(DS3231_SquareWave1Hz);
  
  Serial.println("1Hz square wave output enabled");
  lcd.setCursor(0, 1);
  lcd.print("1Hz SQW Enabled");
  
  delay(2000);
  
  // Disable square wave output (return to normal)
  rtc.writeSqwPinMode(DS3231_OFF);
  
  Serial.println("Square wave output disabled");
  lcd.setCursor(0, 1);
  lcd.print("SQW Disabled");
  
  delay(1000);
}

// ===== Display Functions =====
void updateRTCStatus() {
  if (!rtcInitialized) return;
  
  try {
    DateTime now = rtc.now();
    
    // Check if time is valid
    if (now.year() >= 2020 && now.year() <= 2050) {
      rtcStatus.timeValid = true;
      lastValidTime = now;
      rtcStatus.lastError = RTC_OK;
    } else {
      rtcStatus.timeValid = false;
      rtcStatus.lastError = RTC_TIME_INVALID;
    }
    
    // Update temperature
    rtcStatus.temperature = rtc.getTemperature();
    
  } catch (...) {
    rtcStatus.lastError = RTC_COMMUNICATION_ERROR;
    rtcStatus.errorMessage = "Communication error";
  }
}

void switchDisplayMode() {
  currentMode = (DisplayMode)((currentMode + 1) % 4);
  
  String modeNames[] = {"Time & Date", "Temperature", "Uptime", "Detailed Info"};
  Serial.println("Switched to display mode: " + modeNames[currentMode]);
}

void updateDisplay() {
  if (!rtcInitialized) {
    displayError();
    return;
  }
  
  switch (currentMode) {
    case MODE_TIME_DATE:
      displayTimeDate();
      break;
    case MODE_TEMPERATURE:
      displayTemperature();
      break;
    case MODE_UPTIME:
      displayUptime();
      break;
    case MODE_DETAILED_INFO:
      displayDetailedInfo();
      break;
  }
}

void displayTimeDate() {
  if (!rtcStatus.timeValid) {
    displayError();
    return;
  }
  
  DateTime now = rtc.now();
  
  // Format time and day
  String timeStr = "";
  if (now.hour() < 10) timeStr += "0";
  timeStr += String(now.hour()) + ":";
  if (now.minute() < 10) timeStr += "0";
  timeStr += String(now.minute()) + ":";
  if (now.second() < 10) timeStr += "0";
  timeStr += String(now.second());
  
  String daysOfWeek[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  timeStr += " " + daysOfWeek[now.dayOfTheWeek()];
  
  // Format date
  String dateStr = "";
  if (now.day() < 10) dateStr += "0";
  dateStr += String(now.day()) + "/";
  if (now.month() < 10) dateStr += "0";
  dateStr += String(now.month()) + "/" + String(now.year());
  
  // Add sync indicator and timezone
  if (timeSynced) {
    dateStr += " IST";
  }
  
  updateLCDSafe(timeStr, dateStr);
}

void displayTemperature() {
  String line1 = "RTC Temp:";
  String line2 = String(rtcStatus.temperature, 1) + "C";
  
  // Add temperature trend
  static float lastTemp = rtcStatus.temperature;
  if (rtcStatus.temperature > lastTemp + 0.1) {
    line2 += " Rising";
  } else if (rtcStatus.temperature < lastTemp - 0.1) {
    line2 += " Falling";
  } else {
    line2 += " Stable";
  }
  lastTemp = rtcStatus.temperature;
  
  updateLCDSafe(line1, line2);
}

void displayUptime() {
  unsigned long uptimeSeconds = millis() / 1000;
  unsigned long days = uptimeSeconds / 86400;
  unsigned long hours = (uptimeSeconds % 86400) / 3600;
  unsigned long minutes = (uptimeSeconds % 3600) / 60;
  unsigned long seconds = uptimeSeconds % 60;
  
  String line1 = "System Uptime:";
  String line2 = "";
  
  if (days > 0) {
    line2 = String(days) + "d " + 
            (hours < 10 ? "0" : "") + String(hours) + ":" +
            (minutes < 10 ? "0" : "") + String(minutes) + ":" +
            (seconds < 10 ? "0" : "") + String(seconds);
  } else {
    line2 = (hours < 10 ? "0" : "") + String(hours) + ":" +
            (minutes < 10 ? "0" : "") + String(minutes) + ":" +
            (seconds < 10 ? "0" : "") + String(seconds);
  }
  
  updateLCDSafe(line1, line2);
}

void displayDetailedInfo() {
  static int infoPage = 0;
  static unsigned long lastPageSwitch = 0;
  
  // Switch info page every 2 seconds
  if (millis() - lastPageSwitch > 2000) {
    infoPage = (infoPage + 1) % 4; // Now 4 pages including WiFi status
    lastPageSwitch = millis();
  }
  
  String line1 = "";
  String line2 = "";
  
  switch (infoPage) {
    case 0: // RTC Status
      line1 = "RTC Status:";
      if (rtcStatus.timeValid) {
        line2 = "OK - Time Valid";
      } else {
        line2 = "ERROR - Bad Time";
      }
      break;
      
    case 1: // Power Status
      line1 = "Power Status:";
      if (rtcStatus.lostPower) {
        line2 = "Lost Power!";
      } else {
        line2 = "Power OK";
      }
      break;
      
    case 2: // WiFi Status
      line1 = "WiFi Status:";
      if (wifiConnected) {
        line2 = "Connected";
        if (timeSynced) line2 += " *Sync";
      } else {
        line2 = "Disconnected";
      }
      break;
      
    case 3: // Error Info
      line1 = "Last Error:";
      if (rtcStatus.lastError == RTC_OK) {
        line2 = "No Errors";
      } else {
        line2 = "Err:" + String(rtcStatus.lastError);
      }
      break;
  }
  
  updateLCDSafe(line1, line2);
}

void displayError() {
  String line1 = "RTC ERROR!";
  String line2 = "";
  
  switch (rtcStatus.lastError) {
    case RTC_NOT_FOUND:
      line2 = "Not Found";
      break;
    case RTC_TIME_INVALID:
      line2 = "Invalid Time";
      break;
    case RTC_COMMUNICATION_ERROR:
      line2 = "Comm Error";
      break;
    default:
      line2 = "Unknown Error";
      break;
  }
  
  updateLCDSafe(line1, line2);
}

// ===== Serial Status Functions =====
void printSerialStatus() {
  Serial.println("\n=== RTC Status Update ===");
  
  if (rtcStatus.initialized) {
    Serial.println("RTC: Initialized ✓");
    
    if (rtcStatus.timeValid) {
      DateTime now = rtc.now();
      Serial.printf("Current Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                    now.year(), now.month(), now.day(),
                    now.hour(), now.minute(), now.second());
      
      String daysOfWeek[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
      Serial.println("Day of Week: " + daysOfWeek[now.dayOfTheWeek()]);
    } else {
      Serial.println("Time: Invalid ✗");
    }
    
    Serial.printf("Temperature: %.2f°C\n", rtcStatus.temperature);
    Serial.println("Power Lost: " + String(rtcStatus.lostPower ? "Yes" : "No"));
    
    if (rtcStatus.lastError != RTC_OK) {
      Serial.println("Last Error: " + String(rtcStatus.lastError));
    }
  } else {
    Serial.println("RTC: Not Initialized ✗");
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