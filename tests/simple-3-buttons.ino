//Simple 3 Buttons + Buzzer
//Buttons: 4, 15, 32  |  Buzzer: Pin 0

void setup() {
  Serial.begin(115200);
  
  pinMode(4, INPUT_PULLUP);   // Button 1
  pinMode(15, INPUT_PULLUP);  // Button 2
  pinMode(32, INPUT_PULLUP);  // Button 3
  pinMode(2, OUTPUT);         // Buzzer (Pin 2 - built-in LED)
  
  Serial.println("3 Buttons + Buzzer Ready!");
}

void loop() {
  if (digitalRead(4) == LOW) {
    Serial.println("1");
    beep();
    delay(200);
  }
  
  if (digitalRead(15) == LOW) {
    Serial.println("2");
    beep();
    delay(200);
  }
  
  if (digitalRead(32) == LOW) {
    Serial.println("3");
    beep();
    delay(200);
  }
  
  delay(50);
}

void beep() {
  digitalWrite(2, HIGH);  // Pin 2 - built-in LED + buzzer
  delay(100);
  digitalWrite(2, LOW);
}