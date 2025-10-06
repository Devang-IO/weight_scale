// For two-wire buttons: Connect one wire to GPIO pin, other wire to GND

void setup() {
  Serial.begin(115200);
  
  // Set pins as INPUT_PULLUP so they read HIGH normally
  // When wires touch (complete circuit to GND), pin reads LOW
  pinMode(14, INPUT_PULLUP);
  pinMode(25, INPUT_PULLUP);
  pinMode(26, INPUT_PULLUP);
  pinMode(27, INPUT_PULLUP);
  pinMode(33, INPUT_PULLUP);
}

void loop() {
  // When wires touch, pin goes LOW (connected to GND)
  if (digitalRead(14) == LOW) {
    Serial.println("1");
    delay(200);
  }
  if (digitalRead(25) == LOW) {
    Serial.println("2");
    delay(200);
  }
  if (digitalRead(26) == LOW) {
    Serial.println("3");
    delay(200);
  }
  if (digitalRead(27) == LOW) {
    Serial.println("4");
    delay(200);
  }
  if (digitalRead(33) == LOW) {
    Serial.println("5");
    delay(200);
  }
}