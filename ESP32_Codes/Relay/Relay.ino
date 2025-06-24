/*
  ----------------------------------------------
  Project: Wireless Environment Monitoring System with 3D Simulation
  Node Type: Relay Node (Intermediate Node)
  File Name: RelayNode.ino
  
  Developer: Yogesh Kumar
  Team Members: Yogesh Kumar, Abhishek, Vishnu Kumar, Dhananjay
  GitHub Repo: https://github.com/binhex0x/wireless_emsw3s
  Date : June 2025

  Description:
  This node acts as a bridge between the sensor nodes (Green and Aqua) and the final
  receiver. It receives ESP-NOW packets from both sensor nodes and forwards them
  to the receiver node. It also displays node status and last received data
  on a 128x160 TFT LCD.

  Dependencies:
  - TFT_eSPI
  - ESP-NOW
  - ArduinoJson

  License: MIT License
  ----------------------------------------------
*/

#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>
#include <cstring>

TFT_eSPI tft = TFT_eSPI();

// MAC address of Receiver ESP32  
uint8_t receiverMAC[] = {0x38, 0x18, 0x2b, 0x8a, 0xf7, 0xa0}; 
// ---------- Packet from Nodes ----------
typedef struct {
  char  nodeType[8];   // "Green" / "Aqua"
  char  s1Name[8];
  char  s2Name[8];
  char  s3Name[8];
  char  s4Name[8];
  char  s5Name[8];
  float s1, s2, s3, s4, s5;
  unsigned long ts;
} NodeData;

// ---------- Node tracking ----------
struct NodeStatus {
  NodeData      data{};
  unsigned long lastSeen = 0;
  bool          online   = false;
};

#define NODE_TIMEOUT   5000
#define RECEIVER_TIMEOUT 5000
NodeStatus green, aqua;

// ---------- Receiver tracking ----------
bool receiverOnline     = false;
unsigned long lastOK_rx = 0;

// ---------- Helpers ----------
NodeStatus* getNodeSlot(const char* type) {
  if (strcmp(type, "Green") == 0) return &green;
  if (strcmp(type, "Aqua")  == 0) return &aqua;
  return nullptr;
}
void showStudentNames(
        const char* n1 = "* Yogesh Kumar",
        const char* n2 = "* Abhishek",
        const char* n3 = "Vishnu Kumar",
        const char* n4 = "Dhananjay")
{
  // ── Display + font setup ───────────────────────
  tft.fillScreen(TFT_BLACK);               // black background
  tft.setTextFont(2);                      // font 2 ≈ 16 px high
  tft.setTextColor(TFT_RED, TFT_BLACK);    // red text, black fill
  tft.setTextDatum(MC_DATUM);              // MC = middle-centre anchor

  // ── Layout constants ───────────────────────────
  const int padding   = 4;                 // px around each name
  const int gap       = 8;                 // vertical gap between boxes
  const int scrW      = tft.width();       // screen width
  const int scrH      = tft.height();      // screen height
  const int fontH     = tft.fontHeight(2); // text (baseline-to-baseline)

  // Array of pointers makes looping easy
  const char* names[4] = { n1, n2, n3, n4 };

  // Optional: vertically centre the whole block
  const int totalBlockH = 4 * (fontH + 2*padding) + 3*gap;
  int yTop = (scrH - totalBlockH) / 2;     // start Y so block is centred

  // ── Draw each name ─────────────────────────────
  for (int i = 0; i < 4; ++i) {
    const char* name = names[i];

    // Text and box dimensions
    int textW   = tft.textWidth(name, 2);
    int rectW   = textW + 2 * padding;
    int rectH   = fontH + 2 * padding;
    int rectX   = (scrW - rectW) / 2;          // centred horizontally
    int rectY   = yTop + i * (rectH + gap);

    // Draw white border (1-pixel line)
    tft.drawRect(rectX, rectY, rectW, rectH, TFT_WHITE);

    // Draw the text – anchored middle-centre inside the box
    int textX = scrW / 2;                      // centre of screen
    int textY = rectY + rectH / 2;             // centre of box
    tft.drawString(name, textX, textY);
  }
}


// ---------- ESP‑NOW callbacks ----------
void onReceive(const esp_now_recv_info_t*,
               const uint8_t* incoming, int len)
{
  if (len != sizeof(NodeData)) return;
  NodeData nd;
  memcpy(&nd, incoming, sizeof(nd));

  NodeStatus* slot = getNodeSlot(nd.nodeType);
  if (!slot) return;                      // unknown nodeType

  slot->data     = nd;
  slot->lastSeen = millis();
  slot->online   = true;

  // forward to receiver
  esp_now_send(receiverMAC, incoming, sizeof(nd));
}

void onSent(const uint8_t* mac, esp_now_send_status_t st)
{
  if (memcmp(mac, receiverMAC, 6) == 0) {
    if (st == ESP_NOW_SEND_SUCCESS) {
      receiverOnline = true;
      lastOK_rx      = millis();
    } else {
      receiverOnline = false;
    }
  }
}

// ---------- TFT drawing ----------
void drawNode(int y, const NodeStatus& ns, const char* label)
{
  tft.setCursor(0, y);
  tft.setTextSize(1);
  tft.setTextColor(ns.online ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.printf("%s: %s\n", label, ns.online ? "Online" : "Offline");

  if (!ns.online) return;

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, y + 10);
  tft.printf("%s %.1f | %s %.1f\n",
             ns.data.s1Name, ns.data.s1,
             ns.data.s2Name, ns.data.s2);

  tft.setCursor(10, y + 22);
  tft.printf("%s %.1f | %s %.1f\n",
             ns.data.s3Name, ns.data.s3,
             ns.data.s4Name, ns.data.s4);

  tft.setCursor(10, y + 34);
  tft.printf("%s %.1f\n", ns.data.s5Name, ns.data.s5);
}

void drawScreen()
{
  // timeout checks
  if (millis() - green.lastSeen > NODE_TIMEOUT) green.online = false;
  if (millis() - aqua.lastSeen  > NODE_TIMEOUT) aqua.online  = false;
  if (millis() - lastOK_rx      > RECEIVER_TIMEOUT) receiverOnline = false;

  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(receiverOnline ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.setCursor(0, 0);
  tft.printf("Receiver: %s\n", receiverOnline ? "ON" : "OFF");

  drawNode(30, green, "Green");
  drawNode(90, aqua , "Aqua");
}

// ---------- Setup ----------
void setup()
{
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  tft.init();
  tft.setRotation(-1);      // your preferred 180° orientation
  tft.fillScreen(TFT_BLACK);
  showStudentNames();
  delay(4000);
    tft.setTextFont(1);                      // font 2 ≈ 16 px high

  delay(2000);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP‑NOW init failed");
    while (true);
  }
  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  tft.setTextColor(TFT_WHITE);
  tft.drawString("Relay Ready", 10, 10, 2);
}

// ---------- Main Loop ----------
void loop()
{
  drawScreen();
  delay(500);
}
