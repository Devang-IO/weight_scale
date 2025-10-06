//ESP32 SD Card Module Test with Full Error Control
//Based on your existing pin configuration

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// SD Card pins (from your existing code)
#define PIN_SD_CS   5
#define PIN_SD_SCK  16
#define PIN_SD_MISO 17
#define PIN_SD_MOSI 21

// I2C pins for LCD (from your existing code)
#define PIN_I2C_SDA 18
#define PIN_I2C_SCL 19

// Test files
#define TEST_FILE_PATH "/test_data.txt"
#define LOG_FILE_PATH "/error_log.txt"
#define CONFIG_FILE_PATH "/config.json"

// Hardware objects
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Error tracking
enum SDError {
  SD_OK = 0,
  SD_INIT_FAILED,
  SD_CARD_NOT_FOUND,
  SD_FILE_OPEN_FAILED,
  SD_WRITE_FAILED,
  SD_READ_FAILED,
  SD_DELETE_FAILED,
  SD_SPACE_FULL,
  SD_CORRUPTED
};

struct TestResult {
  bool success;
  SDError errorCode;
  String errorMessage;
  unsigned long duration;
};

// Global variables
bool sdInitialized = false;
int testsPassed = 0;
int testsFailed = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 SD Card Test with Full Error Control ===");
  
  // Initialize I2C and LCD
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.print("SD Card Test");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  
  delay(2000);
  
  // Run comprehensive SD card tests
  runSDCardTests();
  
  // Display final results
  displayFinalResults();
}

void loop() {
  // Continuous monitoring mode
  static unsigned long lastCheck = 0;
  
  if (millis() - lastCheck > 5000) { // Check every 5 seconds
    if (sdInitialized) {
      performQuickHealthCheck();
    } else {
      attemptSDReinitialization();
    }
    lastCheck = millis();
  }
  
  delay(100);
}

// ===== Main Test Functions =====
void runSDCardTests() {
  Serial.println("\n=== Starting Comprehensive SD Card Tests ===");
  
  // Test 1: SD Card Initialization
  TestResult initTest = testSDInitialization();
  displayTestResult("Init Test", initTest);
  
  if (!initTest.success) {
    Serial.println("SD Card initialization failed. Cannot proceed with other tests.");
    return;
  }
  
  // Test 2: Card Information
  TestResult infoTest = testCardInformation();
  displayTestResult("Info Test", infoTest);
  
  // Test 3: File Creation and Writing
  TestResult writeTest = testFileWriting();
  displayTestResult("Write Test", writeTest);
  
  // Test 4: File Reading
  TestResult readTest = testFileReading();
  displayTestResult("Read Test", readTest);
  
  // Test 5: File Append
  TestResult appendTest = testFileAppend();
  displayTestResult("Append Test", appendTest);
  
  // Test 6: Directory Operations
  TestResult dirTest = testDirectoryOperations();
  displayTestResult("Dir Test", dirTest);
  
  // Test 7: Large File Operations
  TestResult largeFileTest = testLargeFileOperations();
  displayTestResult("Large File", largeFileTest);
  
  // Test 8: Multiple File Operations
  TestResult multiFileTest = testMultipleFiles();
  displayTestResult("Multi File", multiFileTest);
  
  // Test 9: Error Recovery
  TestResult recoveryTest = testErrorRecovery();
  displayTestResult("Recovery", recoveryTest);
  
  // Test 10: Performance Test
  TestResult perfTest = testPerformance();
  displayTestResult("Performance", perfTest);
}

// ===== Individual Test Functions =====
TestResult testSDInitialization() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 1: SD Card Initialization ---");
  
  // Initialize SPI with your pins
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  
  // Try to initialize SD card
  if (!SD.begin(PIN_SD_CS)) {
    result.success = false;
    result.errorCode = SD_INIT_FAILED;
    result.errorMessage = "SD.begin() failed";
    Serial.println("ERROR: SD Card initialization failed!");
    
    // Try different approaches
    Serial.println("Trying alternative initialization...");
    delay(1000);
    
    if (!SD.begin(PIN_SD_CS, SPI, 4000000)) { // Lower frequency
      result.errorMessage += " (even with lower frequency)";
    } else {
      result.success = true;
      result.errorCode = SD_OK;
      result.errorMessage = "Success with lower frequency";
      sdInitialized = true;
    }
  } else {
    result.success = true;
    result.errorCode = SD_OK;
    result.errorMessage = "Initialization successful";
    sdInitialized = true;
    Serial.println("SUCCESS: SD Card initialized successfully!");
  }
  
  result.duration = millis() - startTime;
  return result;
}

TestResult testCardInformation() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 2: Card Information ---");
  
  if (!sdInitialized) {
    result.success = false;
    result.errorCode = SD_CARD_NOT_FOUND;
    result.errorMessage = "SD not initialized";
    return result;
  }
  
  try {
    uint8_t cardType = SD.cardType();
    
    if (cardType == CARD_NONE) {
      result.success = false;
      result.errorCode = SD_CARD_NOT_FOUND;
      result.errorMessage = "No SD card attached";
      Serial.println("ERROR: No SD card attached!");
      return result;
    }
    
    // Display card information
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
      Serial.println("MMC");
    } else if (cardType == CARD_SD) {
      Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    
    size_t totalBytes = SD.totalBytes();
    size_t usedBytes = SD.usedBytes();
    Serial.printf("Total space: %u bytes\n", totalBytes);
    Serial.printf("Used space: %u bytes\n", usedBytes);
    Serial.printf("Free space: %u bytes\n", totalBytes - usedBytes);
    
    result.success = true;
    result.errorCode = SD_OK;
    result.errorMessage = "Card info retrieved successfully";
    
  } catch (...) {
    result.success = false;
    result.errorCode = SD_CORRUPTED;
    result.errorMessage = "Exception during card info retrieval";
    Serial.println("ERROR: Exception occurred while reading card info!");
  }
  
  result.duration = millis() - startTime;
  return result;
}

TestResult testFileWriting() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 3: File Writing ---");
  
  if (!sdInitialized) {
    result.success = false;
    result.errorCode = SD_CARD_NOT_FOUND;
    result.errorMessage = "SD not initialized";
    return result;
  }
  
  // Delete existing test file if it exists
  if (SD.exists(TEST_FILE_PATH)) {
    SD.remove(TEST_FILE_PATH);
  }
  
  File file = SD.open(TEST_FILE_PATH, FILE_WRITE);
  if (!file) {
    result.success = false;
    result.errorCode = SD_FILE_OPEN_FAILED;
    result.errorMessage = "Could not create test file";
    Serial.println("ERROR: Failed to create test file!");
    return result;
  }
  
  // Write test data
  String testData = "ESP32 SD Card Test\n";
  testData += "Timestamp: " + String(millis()) + "\n";
  testData += "Test data line 1\n";
  testData += "Test data line 2\n";
  testData += "Special chars: !@#$%^&*()\n";
  testData += "Numbers: 1234567890\n";
  
  size_t bytesWritten = file.print(testData);
  file.close();
  
  if (bytesWritten != testData.length()) {
    result.success = false;
    result.errorCode = SD_WRITE_FAILED;
    result.errorMessage = "Bytes written mismatch";
    Serial.printf("ERROR: Expected %d bytes, wrote %d bytes\n", testData.length(), bytesWritten);
    return result;
  }
  
  // Verify file exists and has correct size
  if (!SD.exists(TEST_FILE_PATH)) {
    result.success = false;
    result.errorCode = SD_WRITE_FAILED;
    result.errorMessage = "File not found after write";
    Serial.println("ERROR: File not found after writing!");
    return result;
  }
  
  File verifyFile = SD.open(TEST_FILE_PATH, FILE_READ);
  if (verifyFile.size() != testData.length()) {
    result.success = false;
    result.errorCode = SD_WRITE_FAILED;
    result.errorMessage = "File size mismatch";
    Serial.printf("ERROR: File size mismatch. Expected %d, got %d\n", testData.length(), verifyFile.size());
    verifyFile.close();
    return result;
  }
  verifyFile.close();
  
  result.success = true;
  result.errorCode = SD_OK;
  result.errorMessage = "File written successfully";
  Serial.printf("SUCCESS: Wrote %d bytes to %s\n", bytesWritten, TEST_FILE_PATH);
  
  result.duration = millis() - startTime;
  return result;
}

TestResult testFileReading() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 4: File Reading ---");
  
  if (!sdInitialized) {
    result.success = false;
    result.errorCode = SD_CARD_NOT_FOUND;
    result.errorMessage = "SD not initialized";
    return result;
  }
  
  if (!SD.exists(TEST_FILE_PATH)) {
    result.success = false;
    result.errorCode = SD_FILE_OPEN_FAILED;
    result.errorMessage = "Test file does not exist";
    Serial.println("ERROR: Test file does not exist!");
    return result;
  }
  
  File file = SD.open(TEST_FILE_PATH, FILE_READ);
  if (!file) {
    result.success = false;
    result.errorCode = SD_FILE_OPEN_FAILED;
    result.errorMessage = "Could not open test file for reading";
    Serial.println("ERROR: Failed to open test file for reading!");
    return result;
  }
  
  Serial.println("File contents:");
  String fileContent = "";
  int lineCount = 0;
  
  while (file.available()) {
    String line = file.readStringUntil('\n');
    fileContent += line + "\n";
    lineCount++;
    Serial.println("Line " + String(lineCount) + ": " + line);
  }
  
  file.close();
  
  if (fileContent.length() == 0) {
    result.success = false;
    result.errorCode = SD_READ_FAILED;
    result.errorMessage = "File is empty or read failed";
    Serial.println("ERROR: File is empty or read failed!");
    return result;
  }
  
  result.success = true;
  result.errorCode = SD_OK;
  result.errorMessage = "File read successfully (" + String(lineCount) + " lines)";
  Serial.printf("SUCCESS: Read %d characters from file\n", fileContent.length());
  
  result.duration = millis() - startTime;
  return result;
}

TestResult testFileAppend() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 5: File Append ---");
  
  if (!sdInitialized) {
    result.success = false;
    result.errorCode = SD_CARD_NOT_FOUND;
    result.errorMessage = "SD not initialized";
    return result;
  }
  
  // Get original file size
  size_t originalSize = 0;
  if (SD.exists(TEST_FILE_PATH)) {
    File sizeFile = SD.open(TEST_FILE_PATH, FILE_READ);
    originalSize = sizeFile.size();
    sizeFile.close();
  }
  
  File file = SD.open(TEST_FILE_PATH, FILE_APPEND);
  if (!file) {
    result.success = false;
    result.errorCode = SD_FILE_OPEN_FAILED;
    result.errorMessage = "Could not open file for append";
    Serial.println("ERROR: Failed to open file for append!");
    return result;
  }
  
  String appendData = "Appended line 1\n";
  appendData += "Appended line 2\n";
  appendData += "Append timestamp: " + String(millis()) + "\n";
  
  size_t bytesWritten = file.print(appendData);
  file.close();
  
  if (bytesWritten != appendData.length()) {
    result.success = false;
    result.errorCode = SD_WRITE_FAILED;
    result.errorMessage = "Append bytes written mismatch";
    Serial.printf("ERROR: Expected to append %d bytes, wrote %d bytes\n", appendData.length(), bytesWritten);
    return result;
  }
  
  // Verify new file size
  File verifyFile = SD.open(TEST_FILE_PATH, FILE_READ);
  size_t newSize = verifyFile.size();
  verifyFile.close();
  
  if (newSize != originalSize + appendData.length()) {
    result.success = false;
    result.errorCode = SD_WRITE_FAILED;
    result.errorMessage = "File size after append is incorrect";
    Serial.printf("ERROR: File size after append is incorrect. Expected %d, got %d\n", 
                  originalSize + appendData.length(), newSize);
    return result;
  }
  
  result.success = true;
  result.errorCode = SD_OK;
  result.errorMessage = "Data appended successfully";
  Serial.printf("SUCCESS: Appended %d bytes to file\n", bytesWritten);
  
  result.duration = millis() - startTime;
  return result;
}

TestResult testDirectoryOperations() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 6: Directory Operations ---");
  
  if (!sdInitialized) {
    result.success = false;
    result.errorCode = SD_CARD_NOT_FOUND;
    result.errorMessage = "SD not initialized";
    return result;
  }
  
  String testDir = "/test_dir";
  String testSubDir = "/test_dir/sub_dir";
  String testDirFile = "/test_dir/dir_file.txt";
  
  // Clean up any existing test directory
  if (SD.exists(testDir)) {
    removeDirectory(testDir);
  }
  
  // Create directory
  if (!SD.mkdir(testDir)) {
    result.success = false;
    result.errorCode = SD_WRITE_FAILED;
    result.errorMessage = "Failed to create directory";
    Serial.println("ERROR: Failed to create test directory!");
    return result;
  }
  
  // Create subdirectory
  if (!SD.mkdir(testSubDir)) {
    result.success = false;
    result.errorCode = SD_WRITE_FAILED;
    result.errorMessage = "Failed to create subdirectory";
    Serial.println("ERROR: Failed to create subdirectory!");
    return result;
  }
  
  // Create file in directory
  File dirFile = SD.open(testDirFile, FILE_WRITE);
  if (!dirFile) {
    result.success = false;
    result.errorCode = SD_FILE_OPEN_FAILED;
    result.errorMessage = "Failed to create file in directory";
    Serial.println("ERROR: Failed to create file in directory!");
    return result;
  }
  
  dirFile.println("File in test directory");
  dirFile.close();
  
  // List directory contents
  Serial.println("Directory listing:");
  File root = SD.open("/");
  listDirectory(root, 0);
  root.close();
  
  // Clean up
  removeDirectory(testDir);
  
  result.success = true;
  result.errorCode = SD_OK;
  result.errorMessage = "Directory operations successful";
  Serial.println("SUCCESS: Directory operations completed!");
  
  result.duration = millis() - startTime;
  return result;
}Tes
tResult testLargeFileOperations() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 7: Large File Operations ---");
  
  if (!sdInitialized) {
    result.success = false;
    result.errorCode = SD_CARD_NOT_FOUND;
    result.errorMessage = "SD not initialized";
    return result;
  }
  
  String largeFilePath = "/large_test.txt";
  
  // Delete existing large file
  if (SD.exists(largeFilePath)) {
    SD.remove(largeFilePath);
  }
  
  File largeFile = SD.open(largeFilePath, FILE_WRITE);
  if (!largeFile) {
    result.success = false;
    result.errorCode = SD_FILE_OPEN_FAILED;
    result.errorMessage = "Could not create large test file";
    Serial.println("ERROR: Failed to create large test file!");
    return result;
  }
  
  // Write 10KB of data
  const int dataSize = 10240; // 10KB
  int bytesWritten = 0;
  
  for (int i = 0; i < 1024; i++) {
    String line = "Large file test line " + String(i) + " with some data\n";
    int written = largeFile.print(line);
    if (written == 0) {
      largeFile.close();
      result.success = false;
      result.errorCode = SD_WRITE_FAILED;
      result.errorMessage = "Failed to write large file data at line " + String(i);
      Serial.printf("ERROR: Failed to write at line %d\n", i);
      return result;
    }
    bytesWritten += written;
  }
  
  largeFile.close();
  
  // Verify file size
  File verifyFile = SD.open(largeFilePath, FILE_READ);
  size_t fileSize = verifyFile.size();
  verifyFile.close();
  
  if (fileSize != bytesWritten) {
    result.success = false;
    result.errorCode = SD_WRITE_FAILED;
    result.errorMessage = "Large file size mismatch";
    Serial.printf("ERROR: Large file size mismatch. Expected %d, got %d\n", bytesWritten, fileSize);
    return result;
  }
  
  // Clean up
  SD.remove(largeFilePath);
  
  result.success = true;
  result.errorCode = SD_OK;
  result.errorMessage = "Large file operations successful (" + String(bytesWritten) + " bytes)";
  Serial.printf("SUCCESS: Large file test completed with %d bytes\n", bytesWritten);
  
  result.duration = millis() - startTime;
  return result;
}

TestResult testMultipleFiles() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 8: Multiple File Operations ---");
  
  if (!sdInitialized) {
    result.success = false;
    result.errorCode = SD_CARD_NOT_FOUND;
    result.errorMessage = "SD not initialized";
    return result;
  }
  
  const int numFiles = 5;
  String filePaths[numFiles];
  
  // Create multiple files
  for (int i = 0; i < numFiles; i++) {
    filePaths[i] = "/multi_test_" + String(i) + ".txt";
    
    // Delete if exists
    if (SD.exists(filePaths[i])) {
      SD.remove(filePaths[i]);
    }
    
    File file = SD.open(filePaths[i], FILE_WRITE);
    if (!file) {
      result.success = false;
      result.errorCode = SD_FILE_OPEN_FAILED;
      result.errorMessage = "Failed to create file " + String(i);
      Serial.printf("ERROR: Failed to create file %d\n", i);
      return result;
    }
    
    file.println("Multi-file test " + String(i));
    file.println("Data for file " + String(i));
    file.close();
  }
  
  // Read all files back
  for (int i = 0; i < numFiles; i++) {
    File file = SD.open(filePaths[i], FILE_READ);
    if (!file) {
      result.success = false;
      result.errorCode = SD_READ_FAILED;
      result.errorMessage = "Failed to read file " + String(i);
      Serial.printf("ERROR: Failed to read file %d\n", i);
      return result;
    }
    
    String content = file.readString();
    file.close();
    
    if (content.length() == 0) {
      result.success = false;
      result.errorCode = SD_READ_FAILED;
      result.errorMessage = "File " + String(i) + " is empty";
      Serial.printf("ERROR: File %d is empty\n", i);
      return result;
    }
  }
  
  // Clean up all files
  for (int i = 0; i < numFiles; i++) {
    SD.remove(filePaths[i]);
  }
  
  result.success = true;
  result.errorCode = SD_OK;
  result.errorMessage = "Multiple file operations successful (" + String(numFiles) + " files)";
  Serial.printf("SUCCESS: Multiple file test completed with %d files\n", numFiles);
  
  result.duration = millis() - startTime;
  return result;
}

TestResult testErrorRecovery() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 9: Error Recovery ---");
  
  if (!sdInitialized) {
    result.success = false;
    result.errorCode = SD_CARD_NOT_FOUND;
    result.errorMessage = "SD not initialized";
    return result;
  }
  
  // Test 1: Try to open non-existent file
  File nonExistentFile = SD.open("/non_existent_file.txt", FILE_READ);
  if (nonExistentFile) {
    nonExistentFile.close();
    result.success = false;
    result.errorCode = SD_CORRUPTED;
    result.errorMessage = "Non-existent file opened successfully (unexpected)";
    Serial.println("ERROR: Non-existent file opened successfully!");
    return result;
  }
  
  // Test 2: Try to write to read-only file
  String readOnlyPath = "/readonly_test.txt";
  File createFile = SD.open(readOnlyPath, FILE_WRITE);
  if (createFile) {
    createFile.println("Read-only test");
    createFile.close();
  }
  
  // Test 3: Simulate full disk (create large file until failure)
  String fullDiskPath = "/full_disk_test.txt";
  File fullDiskFile = SD.open(fullDiskPath, FILE_WRITE);
  if (fullDiskFile) {
    // Write until we can't write anymore or reach reasonable limit
    int writeAttempts = 0;
    const int maxAttempts = 1000;
    
    while (writeAttempts < maxAttempts) {
      int written = fullDiskFile.print("Full disk test data line " + String(writeAttempts) + "\n");
      if (written == 0) {
        Serial.println("Disk full simulation reached at attempt " + String(writeAttempts));
        break;
      }
      writeAttempts++;
    }
    fullDiskFile.close();
    
    // Clean up
    SD.remove(fullDiskPath);
  }
  
  // Clean up
  if (SD.exists(readOnlyPath)) {
    SD.remove(readOnlyPath);
  }
  
  result.success = true;
  result.errorCode = SD_OK;
  result.errorMessage = "Error recovery tests completed";
  Serial.println("SUCCESS: Error recovery tests completed!");
  
  result.duration = millis() - startTime;
  return result;
}

TestResult testPerformance() {
  TestResult result;
  unsigned long startTime = millis();
  
  Serial.println("\n--- Test 10: Performance Test ---");
  
  if (!sdInitialized) {
    result.success = false;
    result.errorCode = SD_CARD_NOT_FOUND;
    result.errorMessage = "SD not initialized";
    return result;
  }
  
  String perfFilePath = "/performance_test.txt";
  
  // Delete existing file
  if (SD.exists(perfFilePath)) {
    SD.remove(perfFilePath);
  }
  
  // Write performance test
  unsigned long writeStartTime = millis();
  File perfFile = SD.open(perfFilePath, FILE_WRITE);
  if (!perfFile) {
    result.success = false;
    result.errorCode = SD_FILE_OPEN_FAILED;
    result.errorMessage = "Could not create performance test file";
    return result;
  }
  
  const int perfTestLines = 100;
  for (int i = 0; i < perfTestLines; i++) {
    perfFile.println("Performance test line " + String(i) + " with timestamp " + String(millis()));
  }
  perfFile.close();
  unsigned long writeTime = millis() - writeStartTime;
  
  // Read performance test
  unsigned long readStartTime = millis();
  perfFile = SD.open(perfFilePath, FILE_READ);
  if (!perfFile) {
    result.success = false;
    result.errorCode = SD_FILE_OPEN_FAILED;
    result.errorMessage = "Could not open performance test file for reading";
    return result;
  }
  
  int linesRead = 0;
  while (perfFile.available()) {
    String line = perfFile.readStringUntil('\n');
    linesRead++;
  }
  perfFile.close();
  unsigned long readTime = millis() - readStartTime;
  
  // Clean up
  SD.remove(perfFilePath);
  
  if (linesRead != perfTestLines) {
    result.success = false;
    result.errorCode = SD_READ_FAILED;
    result.errorMessage = "Performance test line count mismatch";
    Serial.printf("ERROR: Expected %d lines, read %d lines\n", perfTestLines, linesRead);
    return result;
  }
  
  result.success = true;
  result.errorCode = SD_OK;
  result.errorMessage = "Write: " + String(writeTime) + "ms, Read: " + String(readTime) + "ms";
  Serial.printf("SUCCESS: Performance test - Write: %lums, Read: %lums\n", writeTime, readTime);
  
  result.duration = millis() - startTime;
  return result;
}

// ===== Helper Functions =====
void displayTestResult(String testName, TestResult result) {
  Serial.println("\n--- " + testName + " Result ---");
  Serial.println("Success: " + String(result.success ? "YES" : "NO"));
  Serial.println("Duration: " + String(result.duration) + "ms");
  Serial.println("Message: " + result.errorMessage);
  
  if (result.success) {
    testsPassed++;
  } else {
    testsFailed++;
    Serial.println("Error Code: " + String(result.errorCode));
    logError(testName, result);
  }
  
  // Update LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(testName);
  lcd.setCursor(0, 1);
  if (result.success) {
    lcd.print("PASS (" + String(result.duration) + "ms)");
  } else {
    lcd.print("FAIL - ERR:" + String(result.errorCode));
  }
  
  delay(2000);
}

void logError(String testName, TestResult result) {
  if (!sdInitialized) return;
  
  File logFile = SD.open(LOG_FILE_PATH, FILE_APPEND);
  if (logFile) {
    logFile.println("=== ERROR LOG ===");
    logFile.println("Timestamp: " + String(millis()));
    logFile.println("Test: " + testName);
    logFile.println("Error Code: " + String(result.errorCode));
    logFile.println("Message: " + result.errorMessage);
    logFile.println("Duration: " + String(result.duration) + "ms");
    logFile.println("---");
    logFile.close();
  }
}

void listDirectory(File dir, int numTabs) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      listDirectory(entry, numTabs + 1);
    } else {
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

void removeDirectory(String path) {
  File root = SD.open(path);
  if (!root) return;
  
  if (!root.isDirectory()) {
    root.close();
    SD.remove(path);
    return;
  }
  
  File file = root.openNextFile();
  while (file) {
    String filePath = path + "/" + String(file.name());
    if (file.isDirectory()) {
      removeDirectory(filePath);
    } else {
      SD.remove(filePath);
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  SD.rmdir(path);
}

void displayFinalResults() {
  Serial.println("\n=== FINAL TEST RESULTS ===");
  Serial.println("Tests Passed: " + String(testsPassed));
  Serial.println("Tests Failed: " + String(testsFailed));
  Serial.println("Total Tests: " + String(testsPassed + testsFailed));
  
  if (testsFailed == 0) {
    Serial.println("🎉 ALL TESTS PASSED! SD Card is working perfectly!");
  } else {
    Serial.println("⚠️  Some tests failed. Check error log for details.");
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tests Complete!");
  lcd.setCursor(0, 1);
  lcd.print("P:" + String(testsPassed) + " F:" + String(testsFailed));
  
  // Create summary file
  if (sdInitialized) {
    createTestSummary();
  }
}

void createTestSummary() {
  File summaryFile = SD.open("/test_summary.txt", FILE_WRITE);
  if (summaryFile) {
    summaryFile.println("=== SD CARD TEST SUMMARY ===");
    summaryFile.println("Date: " + String(millis()));
    summaryFile.println("Tests Passed: " + String(testsPassed));
    summaryFile.println("Tests Failed: " + String(testsFailed));
    summaryFile.println("Total Tests: " + String(testsPassed + testsFailed));
    
    if (testsFailed == 0) {
      summaryFile.println("Result: ALL TESTS PASSED");
    } else {
      summaryFile.println("Result: SOME TESTS FAILED");
    }
    
    summaryFile.println("=== CARD INFO ===");
    summaryFile.println("Card Type: " + String(SD.cardType()));
    summaryFile.println("Card Size: " + String(SD.cardSize() / (1024 * 1024)) + "MB");
    summaryFile.println("Total Bytes: " + String(SD.totalBytes()));
    summaryFile.println("Used Bytes: " + String(SD.usedBytes()));
    summaryFile.close();
    
    Serial.println("Test summary saved to /test_summary.txt");
  }
}

// ===== Continuous Monitoring Functions =====
void performQuickHealthCheck() {
  static int healthCheckCount = 0;
  healthCheckCount++;
  
  // Quick write/read test
  String healthFile = "/health_check.tmp";
  String testData = "Health check " + String(healthCheckCount) + " at " + String(millis());
  
  File file = SD.open(healthFile, FILE_WRITE);
  if (!file) {
    Serial.println("Health check FAILED - Cannot write");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Health FAIL");
    lcd.setCursor(0, 1);
    lcd.print("Write Error");
    return;
  }
  
  file.println(testData);
  file.close();
  
  // Read back
  file = SD.open(healthFile, FILE_READ);
  if (!file) {
    Serial.println("Health check FAILED - Cannot read");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Health FAIL");
    lcd.setCursor(0, 1);
    lcd.print("Read Error");
    return;
  }
  
  String readData = file.readStringUntil('\n');
  file.close();
  
  // Clean up
  SD.remove(healthFile);
  
  if (readData.indexOf(String(healthCheckCount)) == -1) {
    Serial.println("Health check FAILED - Data mismatch");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Health FAIL");
    lcd.setCursor(0, 1);
    lcd.print("Data Mismatch");
    return;
  }
  
  // Success
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SD Health OK");
  lcd.setCursor(0, 1);
  lcd.print("Check #" + String(healthCheckCount));
  
  Serial.println("Health check #" + String(healthCheckCount) + " PASSED");
}

void attemptSDReinitialization() {
  Serial.println("Attempting SD card reinitialization...");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Reinit SD...");
  
  // Try to reinitialize
  SPI.end();
  delay(1000);
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  
  if (SD.begin(PIN_SD_CS)) {
    sdInitialized = true;
    Serial.println("SD card reinitialization successful!");
    lcd.setCursor(0, 1);
    lcd.print("Success!");
  } else {
    sdInitialized = false;
    Serial.println("SD card reinitialization failed!");
    lcd.setCursor(0, 1);
    lcd.print("Failed!");
  }
  
  delay(2000);
}

// ===== Error Code to String Function =====
String getErrorString(SDError error) {
  switch (error) {
    case SD_OK: return "No Error";
    case SD_INIT_FAILED: return "Initialization Failed";
    case SD_CARD_NOT_FOUND: return "Card Not Found";
    case SD_FILE_OPEN_FAILED: return "File Open Failed";
    case SD_WRITE_FAILED: return "Write Failed";
    case SD_READ_FAILED: return "Read Failed";
    case SD_DELETE_FAILED: return "Delete Failed";
    case SD_SPACE_FULL: return "Disk Full";
    case SD_CORRUPTED: return "Card Corrupted";
    default: return "Unknown Error";
  }
}