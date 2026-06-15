#include <WiFi.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include "secrets.h"

const int VEDIRECT_RX_PIN = D0;
const int VEDIRECT_TX_PIN = D1;
const uint32_t VEDIRECT_BAUD = 19200;
const unsigned long PUSH_INTERVAL_MS = 2000;

HardwareSerial veSerial(1);

struct MpptData {
  String productId;
  String firmwareVersion;
  String serialNumber;
  String stateText;
  int deviceInstance;
  int errorCode = 0;
  float batteryVoltageV = 0.0f;
  float batteryCurrentA = 0.0f;
  float panelVoltageV = 0.0f;
  int panelPowerW = 0;
  float yieldTodayKWh = 0.0f;
  float yieldYesterdayKWh = 0.0f;
  int maxPowerTodayW = 0;
  int maxPowerYesterdayW = 0;
  float loadCurrentA = 0.0f;
  bool loadOutputState = false;
  int chargerModeId = 0;
  bool frameValid = false;
  unsigned long lastUpdateMs = 0;
} CurrentMpptData;

String lineBuffer;
String keys[32];
String values[32];
int pairCount = 0;
unsigned long lastPushMs = 0;


void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting Nano ESP32 VE.Direct push bridge");
  veSerial.begin(VEDIRECT_BAUD, SERIAL_8N1, VEDIRECT_RX_PIN, VEDIRECT_TX_PIN);
  connectWifi();
}

void loop() {
  readVeDirect();

  if (millis() - lastPushMs >= PUSH_INTERVAL_MS) {
    lastPushMs = millis();
    pushUpdate();
  }

  static unsigned long lastWifiRetry = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetry > 10000UL) {
    lastWifiRetry = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}
