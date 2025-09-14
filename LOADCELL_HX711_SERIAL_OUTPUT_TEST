// ESP32 + HX711 + 4-wire load cell example
// Features:
// - prints raw reading and grams
// - press 't' in Serial to tare (zero)
// - press 'k' to print current calibration factor
// - send a number (e.g. "200") to calibrate using a known weight in grams:
//     flow: tare, place known weight, send known weight value -> the sketch computes calibration factor

#include "HX711.h"

const int DOUT_PIN = 23; // connect HX711 DT to ESP32 GPIO 23
const int SCK_PIN  = 22; // connect HX711 SCK to ESP32 GPIO 22

HX711 scale;

// Calibration factor: MUST be found for your load cell + HX711 combination
// start with a placeholder and use the calibration routine below
double calibration_factor = 1000.0; // placeholder - YOUR value will differ!

// reading settings
const int READINGS_TO_AVERAGE = 10;

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\nESP32 + HX711 load cell demo");
  Serial.println("Library version: HX711");

  scale.begin(DOUT_PIN, SCK_PIN);
  // set gain (default 128). HX711 library uses set_gain by begin default.
  if (!scale.is_ready()) {
    Serial.println("Warning: HX711 not found. Check wiring.");
  } else {
    Serial.println("HX711 connected.");
  }

  // apply initial calibration factor
  scale.set_scale(calibration_factor);
  scale.tare(); // reset the scale to 0

  Serial.println("\nSerial Commands:");
  Serial.println("  t   -> tare (zero)");
  Serial.println("  k   -> print current calibration factor");
  Serial.println("  <number> -> send known weight in grams for calibration (after placing known weight)");
  Serial.println();
}

void loop() {
  // check serial input for commands/calibration values
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    s.trim();
    if (s.length() == 0) return;

    if (s.equalsIgnoreCase("t")) {
      scale.tare();
      Serial.println("Tare done. Zero set.");
    } else if (s.equalsIgnoreCase("k")) {
      Serial.print("Calibration factor = ");
      Serial.println(calibration_factor, 6);
    } else {
      // try parse numeric: treat as known weight in grams for auto calibration
      bool isNumber = true;
      for (char c : s) if (!(isDigit(c) || c == '.' || c == '-')) isNumber = false;
      if (isNumber) {
        double knownWeight = s.toDouble();
        if (knownWeight <= 0) {
          Serial.println("Enter a positive number for known weight (grams).");
        } else {
          calibrateUsingKnownWeight(knownWeight);
        }
      } else {
        Serial.println("Unknown command.");
      }
    }
  }

  // regular reading
  double raw = getAverageRaw(READINGS_TO_AVERAGE);
  double grams = scale.get_units(READINGS_TO_AVERAGE); // library already divides by scale factor
  Serial.print("Raw: ");
  Serial.print(raw, 0);
  Serial.print("  |  Weight (g): ");
  Serial.print(grams, 2);
  Serial.print("  |  calib: ");
  Serial.println(calibration_factor, 6);

  delay(500);
}

double getAverageRaw(int times) {
  long sum = 0;
  for (int i = 0; i < times; i++) {
    sum += scale.read();
    delay(5);
  }
  return (double)sum / times;
}

void calibrateUsingKnownWeight(double knownWeight) {
  // recommended flow:
  // 1) Remove all weights and press 't' to tare
  // 2) Place known weight (e.g., 100 g)
  // 3) Send known weight value (e.g., 100) via Serial
  // The code computes new calibration_factor = raw_reading / knownWeight
  Serial.println("Starting calibration...");
  Serial.println("Ensure you already performed tare with no weight on the cell.");
  delay(1000);

  // take an averaged raw reading
  long rawSum = 0;
  const int samples = 15;
  for (int i = 0; i < samples; i++) {
    long r = scale.read(); // raw (signed long)
    rawSum += r;
    delay(100);
  }
  double rawAvg = (double)rawSum / samples;

  // the library's set_scale expects scale such that units = raw / scale
  // so scale = raw / weight
  double new_scale = rawAvg / knownWeight;

  // update calibration
  calibration_factor = new_scale;
  scale.set_scale(calibration_factor);

  Serial.print("Raw average = ");
  Serial.println(rawAvg, 2);
  Serial.print("Known weight = ");
  Serial.print(knownWeight, 2);
  Serial.print(" g -> new calibration factor = ");
  Serial.println(calibration_factor, 6);

  Serial.println("Calibration applied. Remove weight and check zero or press 't' to tare again.");
}


/*
Wiring

Load cell has 4 wires:

Red → VCC (5V or 3.3V; HX711 works with both, but supply the same logic level to ESP32)

Black → GND

White → D- / or output- (depends on cell color convention)

Green → D+ / or output+

On many mini load cells: Red = VCC, Black = GND, White = S-, Green = S+

HX711 module pins:

VCC → 3.3V (recommended)

GND → GND

DT (DOUT) → GPIO 23

SCK → GPIO 22

Note: Use 3.3V for HX711 VCC if possible to match ESP32 I/O logic. If you must use 5V, check the HX711 module's logic-level compatibility.
 */