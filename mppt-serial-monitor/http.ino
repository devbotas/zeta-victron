

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi not connected, continuing anyway");
  }
}

void pushUpdate() {
  if (WiFi.status() != WL_CONNECTED) { return; }
  if (CurrentMpptData.frameValid == false) { return; }

  HTTPClient http;
  http.begin(INVERTER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Api-Token", API_TOKEN);

  String payload = buildJson();
  int code = http.PUT(payload);
  Serial.print("PUT ");
  Serial.print(INVERTER_URL);
  Serial.print(" -> ");
  Serial.println(code);
  if (code > 0) {
    String response = http.getString();
    Serial.println(response);
  }
  http.end();
}

String buildJson() {
  String json = "{";
  json += "\"deviceInstance\":\"" + String(CurrentMpptData.deviceInstance) + "\",";
  json += "\"productId\":\"" + jsonEscape(CurrentMpptData.productId) + "\",";
  json += "\"firmwareVersion\":\"" + jsonEscape(CurrentMpptData.firmwareVersion) + "\",";
  json += "\"serialNumber\":\"" + jsonEscape(CurrentMpptData.serialNumber) + "\",";
  json += "\"stateText\":\"" + jsonEscape(CurrentMpptData.stateText) + "\",";
  json += "\"errorCode\":" + String(CurrentMpptData.errorCode) + ",";
  json += "\"batteryVoltageV\":" + String(CurrentMpptData.batteryVoltageV, 3) + ",";
  json += "\"batteryCurrentA\":" + String(CurrentMpptData.batteryCurrentA, 3) + ",";
  json += "\"panelVoltageV\":" + String(CurrentMpptData.panelVoltageV, 3) + ",";
  json += "\"panelPowerW\":" + String(CurrentMpptData.panelPowerW) + ",";
  json += "\"yieldTodayKWh\":" + String(CurrentMpptData.yieldTodayKWh, 2) + ",";
  json += "\"yieldYesterdayKWh\":" + String(CurrentMpptData.yieldYesterdayKWh, 2) + ",";
  json += "\"maxPowerTodayW\":" + String(CurrentMpptData.maxPowerTodayW) + ",";
  json += "\"maxPowerYesterdayW\":" + String(CurrentMpptData.maxPowerYesterdayW) + ",";
  json += "\"loadCurrentA\":" + String(CurrentMpptData.loadCurrentA, 3) + ",";
  json += "\"loadOutputState\":" + String(CurrentMpptData.loadOutputState ? "true" : "false") + ",";
  json += "\"chargerModeId\":" + String(CurrentMpptData.chargerModeId) + ",";
  json += "\"frameValid\":" + String(CurrentMpptData.frameValid ? "true" : "false") + ",";
  json += "\"lastUpdateMs\":" + String(CurrentMpptData.lastUpdateMs) + ",";
  json += "\"source\":\"arduino-nano-esp32\"";
  json += "}";
  return json;
}

String jsonEscape(const String& s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }
  return out;
}