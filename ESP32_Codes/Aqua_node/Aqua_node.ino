/*
  ----------------------------------------------
  Project: Wireless Environment Monitoring System with 3D Simulation
  Node Type: Aqua Node
  File Name: AquaNode.ino
  
  Developer: Yogesh Kumar
  Team Members: Yogesh Kumar, Abhishek, Vishnu Kumar, Dhananjay
  GitHub Repo: https://github.com/binhex0x/wireless_emsw3s
  Date : June 2025
  
  Description:
  This code reads water-related parameters using a TDS sensor, water level sensor,
  and DS18B20 temperature probe. It sends the data to the Relay Node via ESP-NOW.
  Sensor values are also displayed on an I2C OLED screen.
  Triggers buzzer if TDS is too high or water level is too low.

  Dependencies:
  - OneWire
  - DallasTemperature
  - Adafruit_SSD1306
  - ESP-NOW
  - ArduinoJson

  License: MIT License
  ----------------------------------------------
*/


#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/* ---------- Pins ---------- */
#define TdsSensorPin 34
#define PIN_WATER_LEVEL 36
#define oneWireBus 15
#define I2C_SDA 21
#define I2C_SCL 22

/* ---------- OLED ---------- */
#define OLED_W 128
#define OLED_H 64
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);

#include <FastLED.h>

// === WS2812B strip ===
#define LED_PIN 13  // data‑in for NeoPixel strip
#define NUM_LEDS 11  // set to the actual length (1‑∞)
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

// === Buzzer ===
#define BUZZER_PIN 25



/* ---------- Relay MAC ---------- */
uint8_t relayMAC[] = { 0xf4, 0x65, 0x0b, 0x54, 0x86, 0x6c };  // relay};

/* ---------- Packet ---------- */
typedef struct {
  char nodeType[8];
  char s1Name[8], s2Name[8], s3Name[8], s4Name[8], s5Name[8];
  float s1, s2, s3, s4, s5;
  unsigned long ts;
} NodeData;
NodeData pkt;

/* ---------- Original Keyestudio Globals ---------- */
#define VREF 3.3
#define SCOUNT 30

// Calibration Parameters
#define TDS_FACTOR 0.5         // Adjust this based on calibration
#define TEMP_COEFFICIENT 0.02  // 2% per °C

// Global Variables
int analogBuffer[SCOUNT];
int analogBufferIndex = 0;
float temperature = 25.0;  // Default temperature if sensor fails
float tdsValue = 0.0;


/* ---------- DS18B20 ---------- */
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

/* ---------- Median filter (unchanged) ---------- */
int getMedianNum(int bArray[], int iFilterLen) {
  int sorted[SCOUNT];
  memcpy(sorted, bArray, iFilterLen * sizeof(int));

  // Simple bubble sort
  for (int i = 0; i < iFilterLen - 1; i++) {
    for (int j = i + 1; j < iFilterLen; j++) {
      if (sorted[i] > sorted[j]) {
        int temp = sorted[i];
        sorted[i] = sorted[j];
        sorted[j] = temp;
      }
    }
  }

  return sorted[iFilterLen / 2];
}

/* ---------- Relay status ---------- */
bool relayOK = false;
unsigned long lastOK = 0;
void onSent(const uint8_t*, esp_now_send_status_t st) {
  if (st == ESP_NOW_SEND_SUCCESS) {
    relayOK = true;
    lastOK = millis();
  } else relayOK = false;
}

/* ---------- OLED ---------- */
void drawOLED() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.printf("Relay: %s\n", relayOK ? "On" : "Off");
  oled.printf("TDS : %.0f ppm\n", pkt.s1);
  oled.printf("Temp: %.1f C\n", pkt.s2);
  oled.printf("Lvl : %.0f\n", pkt.s3);
  oled.display();
}


// ---------- Simple one‑shot beeps ---------------------------------
void connectBeep(uint16_t freq = 1800, uint16_t durMs = 120) {
  tone(BUZZER_PIN, freq, durMs);
  delay(durMs);
  noTone(BUZZER_PIN);
}

void disconnectBeep(uint16_t freq = 900, uint16_t durMs = 200) {
  tone(BUZZER_PIN, freq, durMs);
  delay(durMs);
  noTone(BUZZER_PIN);
}

// ---------- LED status + beeps ------------------------------------
void updateStatusLED() {
  /* tweak speeds (ms per LED) */
  const unsigned long STEP_MS_ON = 70;    // green fill speed
  const unsigned long STEP_MS_OFF = 140;  // red   fill speed

  static bool lastState = false;
  static uint8_t litCount = 0;
  static unsigned long lastStep = 0;

  bool state = relayOK;  // relay link state
  unsigned long stepMs = state ? STEP_MS_ON : STEP_MS_OFF;

  // — transition detected —
  if (state != lastState) {
    litCount = 0;
    FastLED.clear(true);
    if (state) connectBeep();
    else disconnectBeep();
    lastStep = millis();
    lastState = state;
  }

  // — progressive fill until strip full —
  if (litCount < NUM_LEDS && millis() - lastStep >= stepMs) {
    leds[litCount] = state ? CRGB::Green : CRGB::Red;
    FastLED.show();
    litCount++;
    lastStep = millis();
  }
}
// --- Short fixed‑pitch beep (you can tweak freq/dur) --------------------
void alertBeep(uint16_t freq = 2500, uint16_t durMs = 150) {
  tone(BUZZER_PIN, freq, durMs);
  delay(durMs);
  noTone(BUZZER_PIN);
}

void checkSensorAlerts() {
  /* Example — fill in your own limits */
  if (pkt.s1 > 200 || pkt.s2 > 50)  alertBeep(2500, 180); // tds temp
  if (pkt.s3 > 1000)                   alertBeep(1800, 120); // level
  // add more…
}



/* ---------- setup ---------- */
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);    // NEW
  digitalWrite(BUZZER_PIN, LOW);  // keep silent at boot

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);  // NEW
  FastLED.clear(true);
  // Configure ESP32 ADC
  analogReadResolution(12);        // Set to 12-bit resolution
  analogSetAttenuation(ADC_11db);  // Full 0-3.3V range

  WiFi.mode(WIFI_STA);
  pinMode(TdsSensorPin, INPUT);
  Wire.begin(I2C_SDA, I2C_SCL);
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  sensors.begin();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP‑NOW init fail");
    while (1)
      ;
  }
  esp_now_register_send_cb(onSent);
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, relayMAC, 6);
  esp_now_add_peer(&peer);

  strcpy(pkt.nodeType, "Aqua");
  strcpy(pkt.s1Name, "TDS");
  strcpy(pkt.s2Name, "Temp");
  strcpy(pkt.s3Name, "Level");
  pkt.s4Name[0] = pkt.s5Name[0] = '\0';
}

/* ---------- loop ---------- */
void loop() {

    updateStatusLED();   // NEW – drives LEDs + connect/disconnect beeps
    checkSensorAlerts();  // ← only if you add the alert helper

  // Read temperature every second
  static unsigned long tempLastRead = 0;
  if (millis() - tempLastRead > 1000) {
    tempLastRead = millis();
    sensors.requestTemperatures();
    temperature = sensors.getTempCByIndex(0);
    if (temperature == DEVICE_DISCONNECTED_C) {
      temperature = 25.0;  // Default if sensor fails
    }
  }

  // Read TDS sensor every 40ms
  static unsigned long analogSampleTimepoint = millis();
  if (millis() - analogSampleTimepoint > 40) {
    analogSampleTimepoint = millis();
    analogBuffer[analogBufferIndex] = analogRead(TdsSensorPin);
    analogBufferIndex = (analogBufferIndex + 1) % SCOUNT;
  }

  // Calculate and display every 800ms
  static unsigned long printTimepoint = millis();
  if (millis() - printTimepoint > 800) {
    printTimepoint = millis();

    // Get median value
    int median = getMedianNum(analogBuffer, SCOUNT);

    // Convert to voltage
    float voltage = median * VREF / 4095.0;  // ESP32 has 12-bit ADC (0-4095)

    // Temperature compensation
    float compensation = 1.0 + TEMP_COEFFICIENT * (temperature - 25.0);
    float compVoltage = voltage / compensation;

    // Calculate TDS value (modified cubic formula)
    tdsValue = (133.42 * pow(compVoltage, 3)
                - 255.86 * pow(compVoltage, 2)
                + 857.39 * compVoltage)
               * TDS_FACTOR;

    // Display results
    Serial.print("Voltage: ");
    Serial.print(voltage, 3);
    Serial.print("V | Temp: ");
    Serial.print(temperature, 1);
    Serial.print("°C | TDS: ");
    Serial.print(tdsValue, 0);
    Serial.println("ppm");

    // Serial.print("Raw ADC: ");
    // Serial.println(median);
  }
  /* --- ESP‑NOW packet & OLED --- */
  pkt.s1 = tdsValue;
  pkt.s2 = temperature;
  pkt.s3 = analogRead(PIN_WATER_LEVEL);
  pkt.s4 = pkt.s5 = 0;
  pkt.ts = millis();

  if (millis() - lastOK > 5000) relayOK = false;
  esp_now_send(relayMAC, (uint8_t*)&pkt, sizeof(pkt));
  drawOLED();
}
