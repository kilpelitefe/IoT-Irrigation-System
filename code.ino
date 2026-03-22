#define BLYNK_TEMPLATE_ID   "TMPL6vWtDF-GN"
#define BLYNK_TEMPLATE_NAME "Farm Irrigation System"
#define BLYNK_AUTH_TOKEN "ENTER_YOUR_TOKEN_HERE"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <DHT.h>

// ---------- WiFi Credentials ----------
char wifiSSID[]     = "WIFI_NAME";
char wifiPassword[] = "WIFI_PASSWORD";

// ---------- Pin Definitions ----------
#define SOIL_SENSOR_PIN   34
#define WATER_PUMP_PIN    23
#define TEMPERATURE_PIN   25

#define I2C_SDA 21
#define I2C_SCL 22

// ---------- Relay Logic ----------
const bool RELAY_ACTIVE_HIGH = true;   

// ---------- DHT Sensor ----------
#define DHT_TYPE DHT22
DHT dht(TEMPERATURE_PIN, DHT_TYPE);

// Last valid DHT readings
float lastValidTemperature = NAN;
float lastValidHumidity    = NAN;

// ---------- BME280 ----------
Adafruit_BME280 bme;
bool bmeAvailable = false;

// ---------- Blynk Virtual Pin Map ----------
#define V_MANUAL_BUTTON   V1
#define V_MOISTURE_GAUGE  V2
#define V_STATUS_TEXT     V3
#define V_TEMPERATURE     V4
#define V_AIR_HUMIDITY    V5
#define V_AUTO_MODE       V6
#define V_THRESHOLD_SLIDER V7
#define V_PRESSURE        V8

BlynkTimer timer;

// ---------- State Variables ----------
int  manualButtonState = 0;
int  autoModeEnabled   = 0;
int  moistureThreshold = 30;
bool pumpRunning       = false;

const int HYSTERESIS_MARGIN = 5;

// Sensor raw ADC calibration values
const int DRY_VALUE = 4095;
const int WET_VALUE = 0;

// Blynk connection timeout (ms)
const unsigned long BLYNK_CONNECT_TIMEOUT = 10000UL;

// ============================================================
//  Helper Functions
// ============================================================

void controlPump(bool turnOn) {
  int signal = RELAY_ACTIVE_HIGH
               ? (turnOn ? HIGH : LOW)
               : (turnOn ? LOW  : HIGH);

  digitalWrite(WATER_PUMP_PIN, signal);
  pumpRunning = turnOn;
}

int readSoilMoisture() {
  int  rawValue = analogRead(SOIL_SENSOR_PIN);
  long percent  = map(rawValue, DRY_VALUE, WET_VALUE, 0, 100);
  return (int)constrain(percent, 0, 100);
}

// ============================================================
//  Blynk Callback Functions
// ============================================================

BLYNK_CONNECTED() {
  // Restore last settings from Blynk after ESP32 reset
  Blynk.syncVirtual(V_MANUAL_BUTTON);
  Blynk.syncVirtual(V_AUTO_MODE);
  Blynk.syncVirtual(V_THRESHOLD_SLIDER);
}

BLYNK_WRITE(V_MANUAL_BUTTON) {
  manualButtonState = param.asInt();
  if (autoModeEnabled == 0)
    controlPump(manualButtonState == 1);
}

BLYNK_WRITE(V_AUTO_MODE) {
  autoModeEnabled = param.asInt();
}

BLYNK_WRITE(V_THRESHOLD_SLIDER) {
  moistureThreshold = constrain(param.asInt(), 0, 100);
}

// ============================================================
//  Main Control Loop (runs every 2 seconds)
// ============================================================

void runSystemCheck() {

  // --- Soil Moisture ---
  int soilMoisture = readSoilMoisture();
  Blynk.virtualWrite(V_MOISTURE_GAUGE, soilMoisture);

  // --- DHT22: Temperature & Humidity ---
  float temperature = dht.readTemperature();
  float airHumidity = dht.readHumidity();

  if (!isnan(temperature)) {
    lastValidTemperature = temperature;
    Blynk.virtualWrite(V_TEMPERATURE, temperature);
  } else if (!isnan(lastValidTemperature)) {
    Blynk.virtualWrite(V_TEMPERATURE, lastValidTemperature);
    Serial.println("DHT: Temperature read failed, using last valid value.");
  }

  if (!isnan(airHumidity)) {
    lastValidHumidity = airHumidity;
    Blynk.virtualWrite(V_AIR_HUMIDITY, airHumidity);
  } else if (!isnan(lastValidHumidity)) {
    Blynk.virtualWrite(V_AIR_HUMIDITY, lastValidHumidity);
    Serial.println("DHT: Humidity read failed, using last valid value.");
  }

  // --- BME280: Pressure ---
  if (bmeAvailable) {
    float pressure = bme.readPressure() / 100.0F;
    if (!isnan(pressure))
      Blynk.virtualWrite(V_PRESSURE, pressure);
  }

  // --- Auto / Manual Mode Logic ---
  if (autoModeEnabled == 1) {

    if (!pumpRunning && soilMoisture < moistureThreshold) {
      controlPump(true);
      Blynk.virtualWrite(V_STATUS_TEXT, "AUTO: Irrigating...");
    }
    else if (pumpRunning && soilMoisture > (moistureThreshold + HYSTERESIS_MARGIN)) {
      controlPump(false);
      Blynk.virtualWrite(V_STATUS_TEXT, "AUTO: Moisture OK");
    }
    else {
      String statusMsg = pumpRunning ? "AUTO: ON" : "AUTO: STANDBY";
      Blynk.virtualWrite(V_STATUS_TEXT, statusMsg);
    }

    // Sync button UI with actual pump state
    Blynk.virtualWrite(V_MANUAL_BUTTON, pumpRunning ? 1 : 0);
  }
  else {
    bool manualOn = (manualButtonState == 1);
    if (manualOn != pumpRunning)
      controlPump(manualOn);

    Blynk.virtualWrite(V_STATUS_TEXT, pumpRunning ? "MANUAL: ON" : "MANUAL: OFF");
  }
}

// ============================================================
//  Setup
// ============================================================

void setup() {
  Serial.begin(115200);

  // Set pump pin as output and make sure pump is off
  pinMode(WATER_PUMP_PIN, OUTPUT);
  controlPump(false);

  // Apply ADC attenuation only to the soil sensor pin
  analogSetPinAttenuation(SOIL_SENSOR_PIN, ADC_11db);

  // Initialize DHT sensor
  dht.begin();

  // Initialize BME280 (try 0x76 then 0x77)
  Wire.begin(I2C_SDA, I2C_SCL);
  bmeAvailable = bme.begin(0x76);
  if (!bmeAvailable)
    bmeAvailable = bme.begin(0x77);
  Serial.println(bmeAvailable ? "BME280 found." : "BME280 not found!");

  // Connect to WiFi with timeout
  Blynk.config(BLYNK_AUTH_TOKEN);

  Serial.print("Connecting to WiFi");
  WiFi.begin(wifiSSID, wifiPassword);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < BLYNK_CONNECT_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    if (Blynk.connect(BLYNK_CONNECT_TIMEOUT / 1000)) {
      Serial.println("Blynk connected.");
    } else {
      Serial.println("Blynk connection failed, running offline.");
    }
  } else {
    Serial.println("\nWiFi connection failed, running offline.");
  }

  // Push initial values to Blynk dashboard
  Blynk.virtualWrite(V_AUTO_MODE,         autoModeEnabled);
  Blynk.virtualWrite(V_THRESHOLD_SLIDER,  moistureThreshold);

  // Schedule control function every 2 seconds
  timer.setInterval(2000L, runSystemCheck);
}

// ============================================================
//  Loop
// ============================================================

void loop() {
  if (Blynk.connected())
    Blynk.run();
  timer.run();
}
