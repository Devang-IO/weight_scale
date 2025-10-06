/*
 * new_final.ino
 * Clean, reliable ESP32 weight logger with online/offline modes and RTC sync.
 *
 * Features
 * - Same pins and Supabase API config as existing sketches
 * - calFactor = 360.0 (initial); simple stable measurement with 2-decimal display
 * - Online send: try twice; if fails or WiFi down -> queue to SD (NDJSON, same JSON per line as POST body)
 * - Instant flush of entire queue as soon as WiFi is connected and API reachable
 * - WiFi connect/disconnect toast + WiFi badge in LCD corner
 * - Boot time NTP sync to set DS3231 RTC (UTC); created_at uses RTC time
 * - Flow: Ask plant -> Prompt place item -> Wait stable -> Measure -> Send/Queue -> Show result -> Wait removal -> Back to ask plant
 */

#include <Arduino.h>
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
#include <Preferences.h>

// ----------------- WiFi Configuration -----------------
const char *ssid = "YOUR_WIFI_SSID";         // TODO: set
const char *password = "YOUR_WIFI_PASSWORD"; // TODO: set

// ----------------- Supabase Configuration -----------------
const char *supabaseUrl = "https://zoblfvpwqodiuudwitwt.supabase.co";
const char *supabaseKey =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InpvYmxmdnB3cW9kaXV1ZHdpdHd0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTc5MzA0OTAsImV4cCI6MjA3MzUwNjQ5MH0."
    "AG0TqekNZ505BodHamvdhQ3A4lk0OtsLrJBGC1YlP3g";

// ----------------- Hardware Pin Configuration -----------------
// HX711 Load Cell
const int HX711_DOUT = 23;
const int HX711_SCK = 22;
// I2C (LCD + RTC)
const int I2C_SDA = 18;
const int I2C_SCL = 19;
// Keypad (3x4)
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}};
byte rowPins[ROWS] = {13, 12, 27, 14};
byte colPins[COLS] = {26, 33, 32};
// SD (SPI)
const int SD_CS_PIN = 5; // HSPI mapping via SPI.begin() below
const int SD_SCK_PIN = 16;
const int SD_MISO_PIN = 17;
const int SD_MOSI_PIN = 21;
const char *OFFLINE_FILE = "/offline_queue.ndjson";

// ----------------- Objects -----------------
HX711_ADC LoadCell(HX711_DOUT, HX711_SCK);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
RTC_DS3231 rtc;
Preferences prefs;

// ----------------- Configuration -----------------
float calFactor = 360.0f;                // initial calibration factor
const float MIN_WEIGHT_THRESHOLD = 5.0f; // grams to consider item present
// Stable measurement parameters
const int STABILITY_SAMPLES = 80;
const int STABILITY_DELAY_MS = 15;
const float STDDEV_THRESHOLD_G = 0.8f; // threshold for stability
const float TRIM_FRACTION = 0.1f;      // trim extreme values before mean

const int STABLE_DURATION_MS = 600;  // present for this long before capture
const int REMOVED_DURATION_MS = 150; // removed for this long to reset

// ----------------- State Machine -----------------
enum State
{
  WAIT_FOR_PLANT,
  WAIT_FOR_ITEM_STABLE,
  MEASURE_AND_SEND,
  WAIT_FOR_REMOVAL,
  FLUSHING
};

State state = WAIT_FOR_PLANT;
int selectedPlant = -1;
unsigned long stateTs = 0;
bool itemPresent = false;
unsigned long presentSince = 0;
unsigned long removedSince = 0;

bool lastWifiConnected = false;
bool apiReachableCached = false;
unsigned long lastApiCheckMs = 0;

// ----------------- Fwds -----------------
void initializeHardware();
void connectToWiFi();
void syncRtcFromNtp();
String getISO8601();
bool sendToSupabase(int plantNumber, float weight, const String &iso8601);
void ensureSD();
void queueOffline(int plantNumber, float weight, const String &iso8601);
bool flushOfflineQueue();
bool hasOfflineData();
bool isApiReachableCached();

void showPromptPlant();
void showWifiToast(const char *msg);
void updateWifiBadge();
bool measureStableWeight(float &outMeanG);
bool isItemPresent(float g);

// ----------------- Setup/Loop -----------------
void setup()
{
  Serial.begin(115200);
  delay(200);
  initializeHardware();
  connectToWiFi();
  syncRtcFromNtp();

  // If online and have backlog, flush immediately
  if (WiFi.status() == WL_CONNECTED && hasOfflineData() && isApiReachableCached())
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sending data...");
    lcd.setCursor(0, 1);
    lcd.print("Please wait");
    state = FLUSHING;
    stateTs = millis();
  }
  else
  {
    showPromptPlant();
    state = WAIT_FOR_PLANT;
    stateTs = millis();
  }
  lastWifiConnected = (WiFi.status() == WL_CONNECTED);
}

void loop()
{
  // WiFi keep-alive and banner
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 1000)
  {
    lastWiFiCheck = millis();
    bool nowConn = (WiFi.status() == WL_CONNECTED);
    if (!nowConn)
    {
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      if (lastWifiConnected && state == WAIT_FOR_PLANT)
        showWifiToast("WiFi disconnected");
    }
    else
    {
      if (!lastWifiConnected && state == WAIT_FOR_PLANT)
        showWifiToast("WiFi connected");
      // Opportunistic flush when idle
      if (hasOfflineData() && state == WAIT_FOR_PLANT)
      {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Sending data...");
        lcd.setCursor(0, 1);
        lcd.print("Please wait");
        state = FLUSHING;
        stateTs = millis();
      }
    }
    lastWifiConnected = nowConn;
  }

  if (millis() - lastApiCheckMs > 8000)
  {
    lastApiCheckMs = millis();
    apiReachableCached = isApiReachableCached();
  }

  // Read keypad
  char key = keypad.getKey();

  // Read raw weight for presence detection
  LoadCell.update();
  float w = LoadCell.getData();
  itemPresent = isItemPresent(fabsf(w));

  switch (state)
  {
  case WAIT_FOR_PLANT:
  {
    if (key && key >= '0' && key <= '9')
    {
      selectedPlant = (key == '0') ? 0 : (key - '0');
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Plant ");
      lcd.print(selectedPlant);
      lcd.setCursor(0, 1);
      lcd.print("Place item");
      presentSince = 0;
      state = WAIT_FOR_ITEM_STABLE;
      stateTs = millis();
    }
    break;
  }

  case WAIT_FOR_ITEM_STABLE:
  {
    if (itemPresent)
    {
      if (presentSince == 0)
        presentSince = millis();
      if (millis() - presentSince >= (unsigned long)STABLE_DURATION_MS)
      {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Please wait...");
        state = MEASURE_AND_SEND;
        stateTs = millis();
      }
    }
    else
    {
      presentSince = 0;
    }
    break;
  }

  case MEASURE_AND_SEND:
  {
    float meanG = NAN;
    bool stable = measureStableWeight(meanG);
    int gramsToSend = (int)lroundf(meanG);
    String ts = getISO8601();

    bool sent = false;
    if (WiFi.status() == WL_CONNECTED)
    {
      // Try twice while online
      for (int attempt = 0; attempt < 2 && !sent; ++attempt)
      {
        sent = sendToSupabase(selectedPlant, gramsToSend, ts);
      }
      if (!sent)
      {
        // As requested, if internet goes away or send fails, queue as well
        queueOffline(selectedPlant, gramsToSend, ts);
      }
    }
    else
    {
      queueOffline(selectedPlant, gramsToSend, ts);
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Plant ");
    lcd.print(selectedPlant);
    lcd.setCursor(0, 1);
    lcd.print(String(meanG, 2));
    lcd.print("g ");
    lcd.print(sent ? "sent" : "queued");

    Serial.print("Plant ");
    Serial.print(selectedPlant);
    Serial.print(" weight ");
    Serial.print(meanG, 2);
    Serial.print("g at ");
    Serial.print(ts);
    Serial.println(sent ? " [SENT]" : " [QUEUED]");

    removedSince = 0;
    state = WAIT_FOR_REMOVAL;
    stateTs = millis();
    break;
  }

  case WAIT_FOR_REMOVAL:
  {
    if (key == '#' || (key && key >= '0' && key <= '9'))
    {
      showPromptPlant();
      state = WAIT_FOR_PLANT;
      stateTs = millis();
      selectedPlant = -1;
      break;
    }
    if (!itemPresent)
    {
      if (removedSince == 0)
        removedSince = millis();
      if (millis() - removedSince >= (unsigned long)REMOVED_DURATION_MS)
      {
        showPromptPlant();
        state = WAIT_FOR_PLANT;
        stateTs = millis();
        selectedPlant = -1;
      }
    }
    else
    {
      removedSince = 0;
    }
    break;
  }

  case FLUSHING:
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      showPromptPlant();
      state = WAIT_FOR_PLANT;
      stateTs = millis();
      break;
    }
    // Simple sending spinner
    static unsigned long lastAnim = 0;
    static const char spinnerChars[4] = {'|', '/', '-', '\\'};
    static uint8_t spIdx = 0;
    if (millis() - lastAnim > 180)
    {
      lastAnim = millis();
      spIdx = (spIdx + 1) & 0x03;
      lcd.setCursor(0, 0);
      lcd.print("Sending        ");
      lcd.setCursor(15, 0);
      lcd.print(spinnerChars[spIdx]);
      lcd.setCursor(0, 1);
      lcd.print("                ");
      updateWifiBadge();
    }
    bool done = flushOfflineQueue();
    if (done || !hasOfflineData())
    {
      showPromptPlant();
      state = WAIT_FOR_PLANT;
      stateTs = millis();
    }
    break;
  }
  }

  delay(10);
}

// ----------------- Helpers -----------------
void initializeHardware()
{
  // I2C devices
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  // Create WiFi glyph (0) and cross (1)
  uint8_t wifiGlyph[8] = {0, 0, 0b00100, 0b01010, 0b10001, 0, 0b00100, 0};
  uint8_t xGlyph[8] = {0, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0, 0};
  lcd.createChar(0, wifiGlyph);
  lcd.createChar(1, xGlyph);

  // HX711
  LoadCell.begin();
  LoadCell.start(1000);
  LoadCell.setCalFactor(calFactor);

  // RTC
  if (!rtc.begin())
  {
    Serial.println("RTC not found!");
  }

  // SD
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI))
  {
    Serial.println("SD init failed");
  }
  else
  {
    ensureSD();
  }
}

void connectToWiFi()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40)
  {
    delay(250);
    lcd.setCursor(attempts % 16, 1);
    lcd.print(".");
    attempts++;
  }
  lcd.clear();
  if (WiFi.status() == WL_CONNECTED)
  {
    lcd.setCursor(0, 0);
    lcd.print("WiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    lcd.setCursor(0, 0);
    lcd.print("WiFi failed");
  }
  delay(800);
}

void syncRtcFromNtp()
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sync time...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm tminfo;
  time_t nowSecs = 0;
  int tries = 0;
  while (tries < 20)
  {
    if (getLocalTime(&tminfo, 500))
    {
      nowSecs = mktime(&tminfo);
      break;
    }
    tries++;
  }
  if (nowSecs > 0)
  {
    rtc.adjust(DateTime((uint32_t)nowSecs));
    Serial.println("RTC set from NTP.");
  }
}

void showPromptPlant()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Press plant no.");
  lcd.setCursor(0, 1);
  lcd.print("1-9, 0 for P0");
  updateWifiBadge();
}

void showWifiToast(const char *msg)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(msg);
  lcd.setCursor(0, 1);
  lcd.print(" ");
  delay(500);
  if (state == WAIT_FOR_PLANT)
    showPromptPlant();
}

void updateWifiBadge()
{
  lcd.setCursor(15, 1);
  if (WiFi.status() == WL_CONNECTED)
    lcd.write((uint8_t)0);
  else
    lcd.write((uint8_t)1);
}

bool isItemPresent(float g) { return g >= MIN_WEIGHT_THRESHOLD; }

bool measureStableWeight(float &outMeanG)
{
  // Collect samples
  const int N = STABILITY_SAMPLES;
  static float samples[STABILITY_SAMPLES];
  for (int i = 0; i < N; i++)
  {
    LoadCell.update();
    samples[i] = fabsf(LoadCell.getData());
    delay(STABILITY_DELAY_MS);
  }
  // Mean
  double sum = 0.0;
  for (int i = 0; i < N; i++)
    sum += samples[i];
  float mean = (float)(sum / N);
  // Stddev
  double ss = 0.0;
  for (int i = 0; i < N; i++)
  {
    double d = samples[i] - mean;
    ss += d * d;
  }
  float sd = (float)sqrt(ss / (N - 1));
  if (sd > STDDEV_THRESHOLD_G)
    return false;
  // Trim extremes, then mean
  static float work[STABILITY_SAMPLES];
  for (int i = 0; i < N; i++)
    work[i] = samples[i];
  for (int i = 1; i < N; i++)
  {
    float key = work[i];
    int j = i - 1;
    while (j >= 0 && work[j] > key)
    {
      work[j + 1] = work[j];
      j--;
    }
    work[j + 1] = key;
  }
  int trim = (int)floorf(TRIM_FRACTION * N);
  int start = trim;
  int end = N - trim;
  if (end - start < 5)
  {
    start = 0;
    end = N;
  }
  double tsum = 0.0;
  int tcount = 0;
  for (int i = start; i < end; i++)
  {
    tsum += work[i];
    tcount++;
  }
  outMeanG = (float)(tsum / tcount);
  return true;
}

String getISO8601()
{
  DateTime now = rtc.now();
  char buf[25];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return String(buf);
}

bool sendToSupabase(int plantNumber, float weight, const String &iso8601)
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  HTTPClient http;
  String url = String(supabaseUrl) + "/rest/v1/weights";
  http.setTimeout(4000);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  http.addHeader("Prefer", "return=minimal");
  StaticJsonDocument<128> doc;
  String plantKey = String("plant_") + String(plantNumber);
  doc[plantKey] = (int)lroundf(weight);
  doc["created_at"] = iso8601;
  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  http.end();
  return code == 200 || code == 201;
}

void ensureSD()
{
  if (!SD.exists(OFFLINE_FILE))
  {
    File f = SD.open(OFFLINE_FILE, FILE_WRITE);
    if (f)
      f.close();
  }
}

void queueOffline(int plantNumber, float weight, const String &iso8601)
{
  ensureSD();
  File f = SD.open(OFFLINE_FILE, FILE_APPEND);
  if (!f)
  {
    Serial.println("Offline queue open failed");
    return;
  }
  StaticJsonDocument<128> doc;
  String plantKey = String("plant_") + String(plantNumber);
  doc[plantKey] = (int)lroundf(weight);
  doc["created_at"] = iso8601;
  String line;
  serializeJson(doc, line);
  line += '\n';
  f.print(line);
  f.close();
}

bool flushOfflineQueue()
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  if (!SD.exists(OFFLINE_FILE))
    return true;
  File in = SD.open(OFFLINE_FILE, FILE_READ);
  if (!in)
    return false;
  const char *TMP_FILE = "/offline_queue_tmp.ndjson";
  SD.remove(TMP_FILE);
  File out = SD.open(TMP_FILE, FILE_WRITE);
  if (!out)
  {
    in.close();
    return false;
  }

  bool allSent = true;
  int processed = 0;
  const int MAX_LINES = 2000;
  while (in.available() && processed < MAX_LINES)
  {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.length() == 0)
      continue;
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
    if (!(code == 200 || code == 201))
    {
      allSent = false;
      out.println(line);
    }
    processed++;
  }
  // Copy remaining if any
  while (in.available())
  {
    String rest = in.readStringUntil('\n');
    rest.trim();
    if (rest.length() == 0)
      continue;
    out.println(rest);
  }
  in.close();
  out.close();
  SD.remove(OFFLINE_FILE);
  if (allSent)
  {
    SD.remove(TMP_FILE);
    return true;
  }
  else
  {
    SD.rename(TMP_FILE, OFFLINE_FILE);
    return false;
  }
}

bool hasOfflineData()
{
  if (!SD.exists(OFFLINE_FILE))
    return false;
  File f = SD.open(OFFLINE_FILE, FILE_READ);
  if (!f)
    return false;
  bool has = f.available();
  f.close();
  return has;
}

bool isApiReachableCached()
{
  if (WiFi.status() != WL_CONNECTED)
    return false;
  HTTPClient http;
  String url = String(supabaseUrl) + "/rest/v1/weights?select=id&limit=1";
  http.setTimeout(1000);
  http.begin(url);
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", "Bearer " + String(supabaseKey));
  int code = http.GET();
  http.end();
  return code == 200;
}
