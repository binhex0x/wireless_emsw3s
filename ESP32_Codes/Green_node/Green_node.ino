
/*
  ----------------------------------------------
  Project: Wireless Environment Monitoring System with 3D Simulation
  Node Type: Green Node
  File Name: GreenNode.ino
  
  Developer: Yogesh Kumar
  Team Members: Yogesh Kumar, Abhishek, Vishnu Kumar, Dhananjay
  GitHub Repo: https://github.com/binhex0x/wireless_emsw3s
  Date : June 2025

  Description:
  This code collects air quality data from MQ2, MQ135, Dust sensor, DHT11, and LDR.
  It sends sensor data to the Relay Node via ESP-NOW and displays values on OLED.
  Also controls WS2812B LED based on connection status and triggers buzzer alerts
  when gas levels exceed thresholds.

  Dependencies:
  - Adafruit_GFX
  - Adafruit_SSD1306
  - ArduinoJson
  - ESP-NOW
  - FastLED

  License: MIT License
  ----------------------------------------------
*/


#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// === Pins ===
#define MQ2_PIN     35
#define MQ135_PIN   32

#define DUST_PIN    33
#define DUST_LED    27

#define LDR_PIN     34
#define DHT_PIN     14

#define BUZZER_PIN  25

#include <FastLED.h>

// === WS2812B ===
#define LED_PIN     13      // any free GPIO that supports RMT (e.g. 4, 12, 13, 18…)
#define NUM_LEDS    11     // strip length (use 1 if you just have a single NeoPixel)
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];


#define SCREEN_W    128
#define SCREEN_H    64
#define OLED_RESET  -1


Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);
DHT dht(DHT_PIN, DHT11);

// === Relay MAC ===
uint8_t relayMAC[] = {0xf4,0x65,0x0b,0x54,0x86,0x6c};// relay};

// === Packet ===
typedef struct {
  char nodeType[8];  // "Green"
  char s1Name[8];    // "MQ2"
  char s2Name[8];    // "MQ135"
  char s3Name[8];    // "Dust"
  char s4Name[8];    // "LDR"
  char s5Name[8];    // "Temp"
  float s1;
  float s2;
  float s3;
  float s4;
  float s5;
  unsigned long ts;
} NodeData;

NodeData pkt;

// === Status Flags ===
bool relayOnline = false;
unsigned long lastRelayOK = 0;
const unsigned long SEND_INTERVAL = 2000;
unsigned long lastSend = 0;

// === Callbacks ===
void onSent(const uint8_t *, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    relayOnline = true;
    lastRelayOK = millis();
  } else {
    relayOnline = false;
  }
}

// === Read Sensors ===
void readSensors() {
  pkt.s1 = analogRead(MQ2_PIN);
  pkt.s2 = analogRead(MQ135_PIN);

  digitalWrite(DUST_LED, LOW);
  delayMicroseconds(280);
  pkt.s3 = analogRead(DUST_PIN);
  delayMicroseconds(40);
  digitalWrite(DUST_LED, HIGH);

  pkt.s4 = analogRead(LDR_PIN);
  pkt.s5 = dht.readTemperature();
  pkt.ts = millis();

  // Buzzer alert for gas
  digitalWrite(BUZZER_PIN, (pkt.s1 > 3000 || pkt.s2 > 3000) ? HIGH : LOW);
}

// === Show OLED ===
void showDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.println(relayOnline ? "Relay: Online" : "Relay: Offline");
  display.printf("%s : %.0f\n", pkt.s1Name, pkt.s1);
  display.printf("%s : %.0f\n", pkt.s2Name, pkt.s2);
  display.printf("%s : %.0f\n", pkt.s3Name, pkt.s3);
  display.printf("%s : %.0f\n", pkt.s4Name, pkt.s4);
  display.printf("%s : %.1fC\n", pkt.s5Name, pkt.s5);
  display.display();
}


// --- Simple connect / disconnect beeps ----------------------------
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

void updateStatusLED() {
  // --- User‑tweakable fill speeds (ms per LED) --------------------
  const unsigned long STEP_MS_ONLINE  =  70;   // faster green fill
  const unsigned long STEP_MS_OFFLINE = 140;   // slower red  fill
  // ----------------------------------------------------------------

  static bool lastState   = false;          // previous relayOnline value
  static uint8_t litCount = 0;              // LEDs already lit
  static unsigned long lastStep = 0;

  bool state = relayOnline;
  unsigned long stepMs = state ? STEP_MS_ONLINE : STEP_MS_OFFLINE;

  // Detect ONLINE ↔ OFFLINE transition
  if (state != lastState) {
    litCount = 0;
    FastLED.clear(true);

    if (state)
      connectBeep();        // simple “online” beep
    else
      disconnectBeep();     // simple “offline” beep

    lastStep  = millis();
    lastState = state;
  }

  // Progressive fill until the strip is full
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

// --- Stub for your thresholds -----------------------------------------
void checkSensorAlerts() {
  /* Example — fill in your own limits */
  if (pkt.s1 > 1000 || pkt.s2 > 1000)  alertBeep(2500, 180); // gas
  if (pkt.s3 > 1000)                   alertBeep(1800, 120); // dust
  // add more…
}


// --- Quick rising/falling chirps ------------------------------------------
// -------- Universal "laser" chirp  -------------------------------------



// === Setup ===
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  pinMode(DUST_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  Wire.begin();

    // --- FastLED init ---
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.clear(true);          // strip starts off


  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED failed")); while (true);
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed"); while (true);
  }
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, relayMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  // Init struct
  strcpy(pkt.nodeType, "Green");
  strcpy(pkt.s1Name, "MQ2");
  strcpy(pkt.s2Name, "MQ135");
  strcpy(pkt.s3Name, "Dust");
  strcpy(pkt.s4Name, "LDR");
  strcpy(pkt.s5Name, "Temp");

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 10);
  display.println("Online");
  display.display();
  delay(1000);
}

// === Loop ===
void loop() {

  
  if (millis() - lastRelayOK > 5000) {
    relayOnline = false;
  }
  updateStatusLED();    // keeps NeoPixel breathing or solid red
  checkSensorAlerts();  // your custom per‑sensor beeps

  if (millis() - lastSend > SEND_INTERVAL) {
    readSensors();
    esp_now_send(relayMAC, (uint8_t *)&pkt, sizeof(pkt));
    showDisplay();
    lastSend = millis();
  }
}
