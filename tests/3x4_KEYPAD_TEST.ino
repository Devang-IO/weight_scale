#include <Keypad.h>

// --- Keypad Layout (4x3) ---
const byte ROWS = 4; // Four rows
const byte COLS = 3; // Three columns

// Keymap (4x3 standard keypad)
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

// Pin connections (swap rows 3 & 4 compared to your previous code)
byte rowPins[ROWS] = {13, 12, 27, 14}; // <-- swapped index 2 & 3
byte colPins[COLS] = {26, 33, 25};     // C1, C2, C3 (changed 32 to 25)

// Create Keypad object
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Buffer to store input
String inputBuffer = "";

void setup() {
  Serial.begin(115200);
  
  // Enable internal pull-ups for all keypad pins
  for (int i = 0; i < ROWS; i++) {
    pinMode(rowPins[i], INPUT_PULLUP);
  }
  for (int i = 0; i < COLS; i++) {
    pinMode(colPins[i], INPUT_PULLUP);
  }
  
  Serial.println("Keypad Test Ready!");
  Serial.println("Press '*' to Enter, '#' to Clear");
}

void loop() {
  char key = keypad.getKey();

  if (key) {
    if (key == '*') {
      Serial.print("ENTER pressed. Final Input: ");
      Serial.println(inputBuffer);
      inputBuffer = ""; // reset after enter
    }
    else if (key == '#') {
      Serial.println("CLEAR pressed. Buffer cleared.");
      inputBuffer = "";
    }
    else {
      inputBuffer += key;
      Serial.print("Key pressed: ");
      Serial.println(key);
    }
  }
}


/*
Suggested Wiring (4×3 Keypad → ESP32)
Keypad Pin	Function	ESP32 GPIO
Row 1	R1	GPIO 13
Row 2	R2	GPIO 12
Row 3	R3	GPIO 14
Row 4	R4	GPIO 27
Col 1	C1	GPIO 26
Col 2	C2	GPIO 33
Col 3	C3	GPIO 25
*/