    /*
    * ESP32 Weight Scale Final v3
    * - UI flow: "Press plant no." -> user presses keypad -> "Please place item" -> detect item -> measure -> send to Supabase -> on success show weight sent -> when item removed show "Press plant no." again
    * - RTC (DS3231) for accurate timestamp; include timestamp in Supabase payload as created_at (ISO 8601)
    * - SD card offline queue (NDJSON). If WiFi down or API fails, append JSON line to /offline_queue.ndjson
    *   On reconnection, flush all queued lines before accepting new measurements (shows "Please wait data sending...")
    * - Stable, non-blocking state machine
    */

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

    // ----------------- WiFi Configuration -----------------
    // Use the same values as frontend/index.html for convenience
    const char* ssid = "YOUR_WIFI_SSID";            // TODO: set
    const char* password = "YOUR_WIFI_PASSWORD";    // TODO: set

    // ----------------- Supabase Configuration -----------------
    // If you prefer, you can paste from frontend/index.html config
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
    const int SD_CS_PIN = 5; // Adjust if your wiring differs
    const int SD_SCK_PIN = 16; // Chosen to avoid conflicts with keypad and I2C
    const int SD_MISO_PIN = 17;
    const int SD_MOSI_PIN = 21;
    const char* OFFLINE_FILE = "/offline_queue.ndjson";

    // ----------------- Objects -----------------
    HX711_ADC LoadCell(HX711_DOUT, HX711_SCK);
    LiquidCrystal_I2C lcd(0x27, 16, 2);
    Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
    RTC_DS3231 rtc;

    // ----------------- Configuration -----------------
    float calFactor = 375.0;
    const float MIN_WEIGHT_THRESHOLD = 5.0;      // minimum grams to consider item present
    const int READINGS_COUNT = 5;                 // for averaging
    const int READING_DELAY_MS = 60;              // delay between samples
    const int STABLE_DURATION_MS = 600;           // require item present for this long before capture
    const int REMOVED_DURATION_MS = 150;          // require item removed for this long before reset

    // ----------------- State Machine -----------------
    enum State {
    SHOW_PRESS_PLANT,
    WAIT_FOR_PLANT_SELECTION,
    PROMPT_PLACE_ITEM,
    WAIT_FOR_ITEM_PRESENT_STABLE,
    MEASURE_AND_SEND,
    SHOW_SENT_RESULT,
    WAIT_FOR_ITEM_REMOVAL_STABLE,
    FLUSHING_OFFLINE
    };

    State state = SHOW_PRESS_PLANT;
    int selectedPlant = -1;
    unsigned long stateTs = 0;
    // Flush progress tracking
    int flushTotal = -1;
    int flushSent = 0;

    // Item presence tracking
    bool itemPresent = false;
    unsigned long presentSince = 0;
    unsigned long removedSince = 0;

    // Forward declarations
    void initializeHardware();
    void connectToWiFi();
    bool sendToSupabase(int plantNumber, float weight, const String& iso8601);
    void showPressPlant();
    bool isItemPresent(float weight);
    float measureAverageWeight();
    String getISO8601();
    void ensureSD();
    void queueOffline(int plantNumber, float weight, const String& iso8601);
    bool flushOfflineQueue();
    bool hasOfflineData();
    bool isApiReachable();
    int countOfflineLines();

    void setup() {
    Serial.begin(115200);
    delay(200);

    initializeHardware();
    connectToWiFi();

    // Attempt to flush any offline data on boot if WiFi already connected
    if (WiFi.status() == WL_CONNECTED && hasOfflineData() && isApiReachable()) {
        lcd.clear();
        lcd.backlight();
        lcd.setCursor(0, 0);
        lcd.print("Sending data...");
        lcd.setCursor(0, 1);
        lcd.print("Please wait");
        state = FLUSHING_OFFLINE;
        stateTs = millis();
        return; // skip showing main UI until flushed
    }

    // Start UI
    showPressPlant();
    state = WAIT_FOR_PLANT_SELECTION;
    stateTs = millis();
    }

    void loop() {
    LoadCell.update();
    float weight = LoadCell.getData();
    itemPresent = isItemPresent(weight);

    // Handle WiFi reconnection and periodic flush
    static unsigned long lastWiFiCheck = 0;
    if (millis() - lastWiFiCheck > 5000) {
        lastWiFiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
        // Try reconnect
        WiFi.disconnect();
        WiFi.begin(ssid, password);
        } else {
        // If connected and offline data exists, enter flushing mode only when idle and API reachable
        if (hasOfflineData() && state == WAIT_FOR_PLANT_SELECTION && isApiReachable()) {
            lcd.clear();
            lcd.setCursor(0, 0);
            flushTotal = countOfflineLines();
            flushSent = 0;
            lcd.print("Sending 0/"); lcd.print(flushTotal);
            lcd.setCursor(0, 1);
            lcd.print("[                ]");
            state = FLUSHING_OFFLINE;
            stateTs = millis();
        }
        }
    }

    char key = keypad.getKey();

    switch (state) {
        case SHOW_PRESS_PLANT:
        // fallthrough handled by WAIT state
        state = WAIT_FOR_PLANT_SELECTION;
        stateTs = millis();
        break;

        case WAIT_FOR_PLANT_SELECTION:
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

        case PROMPT_PLACE_ITEM:
        // Not used separately; merged into WAIT_FOR_ITEM_PRESENT_STABLE
        break;

        case WAIT_FOR_ITEM_PRESENT_STABLE:
        if (itemPresent) {
            if (presentSince == 0) presentSince = millis();
            if (millis() - presentSince >= STABLE_DURATION_MS) {
          // Show brief please wait before measuring/sending
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
        // Measure average
        float avgWeight = measureAverageWeight();
        int gramsToSend = (int)roundf(avgWeight);
        String ts = getISO8601();

        // Do NOT show wait... until after successful send
        bool sent = sendToSupabase(selectedPlant, gramsToSend, ts);
        if (!sent) {
            // Queue offline
            queueOffline(selectedPlant, gramsToSend, ts);
        }

        // Now show the result that was sent/queued
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Plant "); lcd.print(selectedPlant);
        lcd.setCursor(0, 1);
        int grams = (int)roundf(avgWeight);
        lcd.print(grams); lcd.print("g ");
        if (sent) {
          lcd.print("sent");
        } else {
          lcd.print("queued");
        }

        Serial.print("Plant "); Serial.print(selectedPlant);
        Serial.print(" weight "); Serial.print(avgWeight);
        Serial.print("g at "); Serial.print(ts);
        Serial.println(sent ? " [SENT]" : " [QUEUED]");

        state = WAIT_FOR_ITEM_REMOVAL_STABLE;
        removedSince = 0;
        stateTs = millis();
        break; }

        case SHOW_SENT_RESULT:
        // merged into MEASURE_AND_SEND
        break;

        case WAIT_FOR_ITEM_REMOVAL_STABLE:
        // Allow user to force reset with '#' if removal is problematic
        if (key == '#' || (key && key >= '0' && key <= '9')) {
            selectedPlant = -1;
            showPressPlant();
            state = WAIT_FOR_PLANT_SELECTION;
            stateTs = millis();
            break;
        }
        if (!itemPresent) {
            if (removedSince == 0) removedSince = millis();
            if (millis() - removedSince >= REMOVED_DURATION_MS) {
            // Reset to initial state
            selectedPlant = -1;
            showPressPlant();
            state = WAIT_FOR_PLANT_SELECTION;
            stateTs = millis();
            }
        } else {
            removedSince = 0;
        }
        break;

        case FLUSHING_OFFLINE: {
          // Abort flushing if WiFi/API not reachable anymore
          if (WiFi.status() != WL_CONNECTED || !isApiReachable()) {
            showPressPlant();
            state = WAIT_FOR_PLANT_SELECTION;
            stateTs = millis();
            break;
          }

          // Update progress (remaining lines) about once a second
          static unsigned long lastProg = 0;
          if (millis() - lastProg > 1000) {
            lastProg = millis();
            int remaining = countOfflineLines();
            if (flushTotal < remaining) {
              // New data added while flushing; extend total so progress stays sane
              flushTotal = remaining + flushSent;
            }
            int total = (flushTotal <= 0) ? (remaining) : flushTotal;
            int sent = max(0, total - remaining);
            flushSent = sent;
            // Spinner animation
            const char spinnerChars[4] = {'|','/','-','\\'};
            static uint8_t spIdx = 0; spIdx = (spIdx + 1) & 0x03;

            // Line 1 only: "Sending s/t pp% <spin>"
            lcd.setCursor(0, 0);
            lcd.print("                "); // clear line
            lcd.setCursor(0, 0);
            int pct = (total > 0) ? (sent * 100) / total : 0;
            lcd.print("Sending "); lcd.print(sent); lcd.print("/"); lcd.print(total); lcd.print(" "); lcd.print(pct); lcd.print("%");
            lcd.setCursor(15, 0); lcd.print(spinnerChars[spIdx]);
            // Clear line 2 text-only
            lcd.setCursor(0, 1);
            lcd.print("                ");
          }

          // Flush a small batch each loop
          bool done = flushOfflineQueue();
          if (done || !hasOfflineData()) {
            // Finished sending queued data; go to idle prompt
            flushTotal = -1;
            flushSent = 0;
            showPressPlant();
            state = WAIT_FOR_PLANT_SELECTION;
            stateTs = millis();
          }
          break; }
    }

    delay(15);
    }

    void initializeHardware() {
    // I2C (LCD + RTC)
    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.init();
    lcd.backlight(); // Keep on during UI usage

    // HX711
    LoadCell.begin();
    LoadCell.start(1000);
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

    // SD
    // Initialize custom SPI pins for SD (HSPI-style mapping). RTC shares I2C (GPIO 18/19) with LCD; no conflict.
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, SPI)) {
        Serial.println("SD init failed");
    } else {
        // Ensure file exists
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
    return fabs(weight) >= MIN_WEIGHT_THRESHOLD;
    }

    float measureAverageWeight() {
    float sum = 0;
    for (int i = 0; i < READINGS_COUNT; i++) {
        LoadCell.update();
        sum += fabs(LoadCell.getData());
        delay(READING_DELAY_MS);
    }
    return sum / READINGS_COUNT;
    }

    String getISO8601() {
    DateTime now = rtc.now();
    char buf[25];
    // Example: 2025-09-19T19:45:30Z (use Z since RTC has no TZ)
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

    // Build JSON payload with created_at as measurement time
    StaticJsonDocument<128> doc;
    String plantKey = String("plant_") + String(plantNumber);
    doc[plantKey] = (int)roundf(weight);           // e.g., {"plant_1": 123}
    doc["created_at"] = iso8601;     // ensure DB shows the actual measurement time

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
    lcd.print("1-9, 0 for P0");
    }

    void ensureSD() {
    if (!SD.exists(OFFLINE_FILE)) {
        File f = SD.open(OFFLINE_FILE, FILE_WRITE);
        if (f) f.close();
    }
    }

    void queueOffline(int plantNumber, float weight, const String& iso8601) {
      ensureSD();
      File f = SD.open(OFFLINE_FILE, FILE_APPEND);
      if (!f) {
        Serial.println("Failed to open offline queue for append");
        return;
      }
      // Write NDJSON line
      StaticJsonDocument<128> doc;
      String plantKey = String("plant_") + String(plantNumber);
      doc[plantKey] = (int)roundf(weight);
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

      // Read all lines, attempt to send, write back failures to a temp file
      File in = SD.open(OFFLINE_FILE, FILE_READ);
      if (!in) return false;

      const char* TMP_FILE = "/offline_queue_tmp.ndjson";
      SD.remove(TMP_FILE);
      File out = SD.open(TMP_FILE, FILE_WRITE);
      if (!out) {
        in.close();
        return false;
      }

      bool allSent = true;
      int processed = 0;
      const int MAX_LINES_PER_CALL = 1000; // process many per call during flush
      while (in.available() && processed < MAX_LINES_PER_CALL) {
        String line = in.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        // Attempt POST
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
          out.println(line); // keep for next time
        }
        processed++;
      }

      // Preserve any remaining unprocessed lines
      bool moreRemaining = in.available();
      while (in.available()) {
        String rest = in.readStringUntil('\n');
        rest.trim();
        if (rest.length() == 0) continue;
        out.println(rest);
      }
      in.close();
      out.close();

      // Replace original with tmp (keeping remaining lines if any)
      SD.remove(OFFLINE_FILE);
      if (!allSent || moreRemaining) {
        // Not all sent or more lines remain; keep tmp as the new queue
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

    // Quick API reachability check with 3s cache
    bool isApiReachable() {
      if (WiFi.status() != WL_CONNECTED) return false;
      static unsigned long lastCheckMs = 0;
      static bool lastResult = false;
      if (millis() - lastCheckMs < 3000) {
        return lastResult;
      }
      lastCheckMs = millis();

      HTTPClient http;
      String url = String(supabaseUrl) + "/rest/v1/weights?select=id&limit=1";
      http.setTimeout(1000);
      http.begin(url);
      http.addHeader("apikey", supabaseKey);
      http.addHeader("Authorization", "Bearer " + String(supabaseKey));
      int code = http.GET();
      http.end();
      lastResult = (code == 200);
      return lastResult;
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
