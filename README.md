# Plant Weight Monitoring System

A complete IoT solution for monitoring plant weights using ESP32, load cells, keypad input, and Supabase database.

## System Components

- **ESP32 microcontroller** with WiFi connectivity
- **HX711 Load Cell** for precise weight measurements
- **16x2 I2C LCD** for display
- **3x4 Keypad** for plant selection (0-9)
- **Supabase database** for data storage
- **Web frontend** for data visualization

## Features

- Place item on scale and press keypad key (0-9) to select plant
- Automatically takes 10 weight readings and calculates average
- Sends data to Supabase database
- Real-time web interface to view all plant weights
- Tare functionality for zeroing the scale

## Hardware Wiring

### HX711 Load Cell
- HX711 DT → ESP32 GPIO 23
- HX711 SCK → ESP32 GPIO 22
- HX711 VCC → ESP32 3.3V
- HX711 GND → ESP32 GND

### I2C LCD (16x2)
- LCD SDA → ESP32 GPIO 18
- LCD SCL → ESP32 GPIO 19
- LCD VCC → ESP32 3.3V
- LCD GND → ESP32 GND

### 3x4 Keypad
- Row 1 → ESP32 GPIO 13
- Row 2 → ESP32 GPIO 12
- Row 3 → ESP32 GPIO 14
- Row 4 → ESP32 GPIO 27
- Col 1 → ESP32 GPIO 26
- Col 2 → ESP32 GPIO 33
- Col 3 → ESP32 GPIO 32

### Tare Button
- Button → ESP32 GPIO 25 (with pull-up resistor)
- Other side → GND

## Setup Instructions

### 1. Supabase Database Setup

1. Create a new Supabase project
2. Run the SQL commands from `supabase_setup.sql` in your Supabase SQL editor
3. Get your Project URL and anon key from Settings → API

### 2. Configuration

**ESP32 Code:**
Update the ESP32 code (`INTEGRATED_KEYPAD_LOADCELL_SUPABASE.ino`) with:
- Your WiFi credentials
- Your Supabase URL and API key

**Frontend:**
No configuration needed! Just enter your Supabase credentials directly in the web interface.

### 3. ESP32 Code Setup

1. Install required Arduino libraries:
   - HX711_ADC
   - LiquidCrystal_I2C
   - Keypad
   - ArduinoJson

2. Update the ESP32 code (`INTEGRATED_KEYPAD_LOADCELL_SUPABASE.ino`) with:
   - Your WiFi credentials
   - Your Supabase URL and API key

3. Upload the code to your ESP32

### 4. Frontend Setup

Simply open `frontend/index.html` in your web browser - no installation needed!

1. Open `frontend/index.html` in any modern web browser
2. Enter your Supabase URL and API key in the form
3. Click "Save Config" 
4. Click "Refresh Data" to load plant weights

## Usage

1. **Power on the ESP32** - Wait for "SYSTEM READY" message
2. **Place item on scale** - Weight will be displayed
3. **Press keypad key 0-9** - Selects plant number and starts measurement
4. **Wait for readings** - System takes 10 readings and calculates average
5. **Data sent to database** - Confirmation shown on LCD
6. **View data** - Open the web frontend to see all measurements

## Database Schema

The `weights` table contains:
- `id`: Primary key
- `plant_0` to `plant_9`: Weight columns for each plant (DECIMAL)
- `created_at`: Timestamp when record was created
- `updated_at`: Timestamp when record was last updated

## Keypad Functions

- **Keys 0-9**: Select plant number and start weight measurement
- **Key ***: Cancel current operation
- **Key #**: Clear display
- **Tare Button**: Zero the scale

## Troubleshooting

- **WiFi connection issues**: Check SSID and password in code
- **Database errors**: Verify Supabase URL and API key
- **Weight readings unstable**: Ensure stable platform and proper HX711 connections
- **LCD not displaying**: Check I2C address (common addresses: 0x27, 0x3F)

## Calibration

The default calibration factor is set to 375.0. To calibrate:
1. Place a known weight on the scale
2. Adjust the `calFactor` variable in the code
3. Re-upload the code to ESP32