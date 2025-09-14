// Very small HX711 connection checker for ESP32
// Change DOUT_PIN / SCK_PIN if you wired different pins.

#include "HX711.h"

#define DOUT_PIN 19  // HX711 DOUT -> ESP32 (change if needed)
#define SCK_PIN  18  // HX711 SCK  -> ESP32 (change if needed)

HX711 scale;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n--- HX711 CONNECTION CHECK ---");
  Serial.print("DT=");
  Serial.print(DOUT_PIN);
  Serial.print("  SCK=");
  Serial.println(SCK_PIN);

  // Start HX711
  scale.begin(DOUT_PIN, SCK_PIN);
  delay(200); // allow module to initialize
}

void loop() {
  bool ready = scale.is_ready();
  if (ready) {
    Serial.println("YES — HX711 connected");
  } else {
    Serial.println("NO — HX711 not ready");
  }
  delay(1000);
}


/*
What to expect

If wiring/power/board are OK you should see: YES — HX711 connected every second.

If you see NO — HX711 not ready, something is wrong (power, wiring, pins, or module).
*/