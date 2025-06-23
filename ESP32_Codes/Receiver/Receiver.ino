/*
  ----------------------------------------------
  Project: Wireless Environment Monitoring System with 3D Simulation
  Node Type: Receiver 
  File Name: ReceiverNode.ino
  
  Developer: Yogesh Kumar
  Team Members: Yogesh Kumar, Abhishek, Vishnu Kumar, Dhananjay
  GitHub Repo: https://github.com/binhex0x/wireless_emsw3s
  Date : June 2025

  Description:
  This node receives ESP-NOW packets from the Relay Node and transmits the structured
  JSON data to a laptop via Serial communication. The Unity 3D simulation reads this
  data in real-time to render environmental conditions like air quality, water level,
  and more.

  Dependencies:
  - ESP-NOW
  - ArduinoJson

  License: MIT License
  ----------------------------------------------
*/


   ============================================================ */
#include <WiFi.h>
#include <esp_now.h>
#include <FastLED.h>

/* -------- LED setup (FastLED) -------- */
#define LED_PIN     4          // GPIO for the single WS2812B
#define NUM_LEDS    1
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

/* -------- timeouts & intervals -------- */
#define RELAY_TIMEOUT  5000    // ms without packet ➜ relay offline
#define JSON_INTERVAL   500    // serial update rate

/* -------- data packet (must match sender) -------- */
typedef struct {
  char  nodeType[8];
  char  s1Name[8], s2Name[8], s3Name[8], s4Name[8], s5Name[8];
  float s1, s2, s3, s4, s5;
  unsigned long ts;
} NodeData;

/* -------- track latest data -------- */
NodeData green{}, aqua{};
unsigned long lastRelayPacket = 0;

/* helper: returns slot for nodeType */
NodeData* slot(const char* type) {
  if (strcmp(type, "Green") == 0) return &green;
  if (strcmp(type, "Aqua")  == 0) return &aqua;
  return nullptr;
}

/* -------- ESP‑NOW receive callback -------- */
void onReceive(const esp_now_recv_info_t*,
               const uint8_t* data, int len)
{
  if (len != sizeof(NodeData)) return;      // size mismatch → ignore
  NodeData pkt;  memcpy(&pkt, data, sizeof(pkt));

  NodeData* dst = slot(pkt.nodeType);
  if (dst) *dst = pkt;                      // store latest for that node

  lastRelayPacket = millis();               // mark relay alive
}

/* -------- JSON out -------- */
void sendJSON()
{
  bool relayOnline = (millis() - lastRelayPacket) < RELAY_TIMEOUT;

  Serial.print('{');
  Serial.print("\"relay\":"); Serial.print(relayOnline ? "1" : "0");

  Serial.print(",\"green\":{");
  Serial.printf("\"%s\":%.1f,\"%s\":%.1f,\"%s\":%.1f,\"%s\":%.1f,\"%s\":%.1f}",
    green.s1Name, green.s1, green.s2Name, green.s2,
    green.s3Name, green.s3, green.s4Name, green.s4,
    green.s5Name, green.s5);

  Serial.print(",\"aqua\":{");
  Serial.printf("\"%s\":%.1f,\"%s\":%.1f,\"%s\":%.1f}",
    aqua.s1Name, aqua.s1, aqua.s2Name, aqua.s2, aqua.s3Name, aqua.s3);
  Serial.println('}');
}

/* -------- setup -------- */
void setup()
{
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  /* FastLED init */
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(50);
  leds[0] = CRGB::Red;
  FastLED.show();

  /* ESP‑NOW */
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP‑NOW init failed"); while (true);
  }
  esp_now_register_recv_cb(onReceive);

  Serial.println("Receiver ready");
}

/* -------- loop -------- */
void loop()
{
  static unsigned long lastJson = 0;
  bool relayOnline = (millis() - lastRelayPacket) < RELAY_TIMEOUT;

  /* LED status */
  leds[0] = relayOnline ? CRGB::Green : CRGB::Red;
  FastLED.show();

  /* periodic JSON output */
  if (millis() - lastJson >= JSON_INTERVAL) {
    sendJSON();
    lastJson = millis();
  }
}
