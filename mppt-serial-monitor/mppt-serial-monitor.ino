#include <W5500lwIP.h>
#include <SerialPIO.h>
#include "secrets.h"

#pragma region structs and variables

const int VEDIRECT_RX_PIN_1 = 6;
const int VEDIRECT_RX_PIN_2 = 10;
const int VEDIRECT_RX_PIN_3 = 14;
SerialPIO veSerial1(NOPIN, VEDIRECT_RX_PIN_1, 384);
SerialPIO veSerial2(NOPIN, VEDIRECT_RX_PIN_2, 384);
SerialPIO veSerial3(NOPIN, VEDIRECT_RX_PIN_3, 384);

WiFiClient client;
Wiznet5500lwIP eth(17, SPI, 21);

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
bool is_charger_data_received = false;

#pragma endregion
#pragma region setup

void setup() {
  Serial1.begin(115200);

  delay(5000);
  println("");
  println("");
  println("Starting Ethernet port");

  // Start the Ethernet port
  if (!eth.begin()) {
    println("No wired Ethernet hardware detected. Check pinouts, wiring.");
    while (1) {
      delay(1000);
    }
  }

  while (!eth.isLinked()) {
    print(".");
    delay(500);
  }

  while (!eth.localIP()) {
    print(",");
    delay(500);
  }

  println("");
  println("Ethernet connected");
  println("IP address: ");
  println(eth.localIP().toString());

  pinMode(VEDIRECT_RX_PIN_1, INPUT);
  veSerial1.begin(19200);

  pinMode(VEDIRECT_RX_PIN_2, INPUT);
  veSerial2.begin(19200);

  pinMode(VEDIRECT_RX_PIN_3, INPUT);
  veSerial3.begin(19200);

  println("Setup complete");
}

#pragma endregion
#pragma region loop

void loop() {
  println("");
  println("");
  println("Tick 1");
  readAndPush(veSerial1, 257);
  println("Tick 2");
  readAndPush(veSerial2, 258);
  println("Tick 3");
  readAndPush(veSerial3, 259);

  println("Done");

  delay(1000);
}

void readAndPush(SerialPIO& serial, int deviceInstance) {
  readVeDirect(serial);

  if (is_charger_data_received == false) {
    println("No charger data");
    return;
  }

  if (eth.isLinked() == false) {
    println("No ethernet cable");
    return;
  }

  IPAddress ip = eth.localIP();
  if ((ip[0] == 0) && (ip[1] == 0) && (ip[2] == 0) && (ip[3] == 0)) {
    println("No IP address");
    return;
  }

  if (CurrentMpptData.frameValid == false) {
    println("Corrupt data");
    return;
  }

  CurrentMpptData.deviceInstance = deviceInstance;

  println("Push");
  lastPushMs = millis();
  String payload = buildJson();
  println(payload);
  bool is_pushed = sendPut(payload);

  if (is_pushed == false) {
    println("Push failed");
  }

  is_charger_data_received = false;
}

#pragma endregion
#pragma region victron

void readVeDirect(SerialPIO& serial) {
  while (serial.available()) {
    char c = (char)serial.read();
    //print(c);
    if (c == '\n') {
      processLine(lineBuffer);
      lineBuffer = "";
    } else if (c != '\r') {
      lineBuffer += c;
      if (lineBuffer.length() > 120) lineBuffer = "";
    }
  }
}

void addPair(const String& key, const String& value) {
  if (pairCount >= 32) return;
  keys[pairCount] = key;
  values[pairCount] = value;
  pairCount++;
}

void processLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  int tabPos = line.indexOf('\t');
  if (tabPos < 0) return;
  String key = line.substring(0, tabPos);
  String value = line.substring(tabPos + 1);
  addPair(key, value);
  if (key == "Checksum") {
    commitFrame();
    resetFrame();
    is_charger_data_received = true;
  }
}

int toIntSafe(const String& value, int fallback = 0) {
  if (value.length() == 0) return fallback;
  return value.toInt();
}

void commitFrame() {
  CurrentMpptData.deviceInstance = 256;
  CurrentMpptData.productId = getValue("PID");
  CurrentMpptData.firmwareVersion = getValue("FW").substring(0, 1) + "." + getValue("FW").substring(1, 3);
  CurrentMpptData.serialNumber = getValue("SER#");
  CurrentMpptData.stateText = mapState(getValue("CS"));
  CurrentMpptData.errorCode = toIntSafe(getValue("ERR"));
  CurrentMpptData.batteryVoltageV = toScaledFloat(getValue("V"), 1000.0f);
  CurrentMpptData.batteryCurrentA = toScaledFloat(getValue("I"), 1000.0f);
  CurrentMpptData.panelVoltageV = toScaledFloat(getValue("VPV"), 1000.0f);
  CurrentMpptData.panelPowerW = toIntSafe(getValue("PPV"));
  CurrentMpptData.yieldTodayKWh = toScaledFloat(getValue("H20"), 100.0f);
  CurrentMpptData.yieldYesterdayKWh = toScaledFloat(getValue("H22"), 100.0f);
  CurrentMpptData.maxPowerTodayW = toIntSafe(getValue("H21"));
  CurrentMpptData.maxPowerYesterdayW = toIntSafe(getValue("H23"));
  CurrentMpptData.loadCurrentA = toScaledFloat(getValue("IL"), 1000.0f);
  CurrentMpptData.loadOutputState = toIntSafe(getValue("LOAD")) == 1;
  CurrentMpptData.chargerModeId = toIntSafe(getValue("MPPT"));
  CurrentMpptData.frameValid = true;
  CurrentMpptData.lastUpdateMs = millis();
}

String getValue(const String& key) {
  for (int i = 0; i < pairCount; i++) {
    if (keys[i] == key) return values[i];
  }
  return "";
}

void resetFrame() {
  pairCount = 0;
}

String mapState(const String& cs) {
  if (cs == "0") return "Off";
  if (cs == "2") return "Fault";
  if (cs == "3") return "Bulk";
  if (cs == "4") return "Absorption";
  if (cs == "5") return "Float";
  if (cs == "7") return "Equalize";
  return cs;
}



float toScaledFloat(const String& value, float scale) {
  if (value.length() == 0) return 0.0f;
  return value.toFloat() / scale;
}

#pragma endregion
#pragma region http

bool sendPut(const String& payload) {
  client.setNoDelay(true);
  if (client.connected() == false) {
    if (!client.connect(INVERTER_HOST, INVERTER_PORT)) {
      Serial1.println("Connection to inverter failed");
      return false;
    }
  }



  // if (!client.connect(INVERTER_HOST, INVERTER_PORT)) {
  //   Serial.println("Connection to inverter failed");
  //   return false;
  // }

  client.print(String("PUT ") + INVERTER_PATH + " HTTP/1.1\r\n");
  client.print(String("Host: ") + INVERTER_HOST + ":" + INVERTER_PORT + "\r\n");
  client.print("User-Agent: PicoW-VEDirect/1.0\r\n");
  //client.print("Connection: close\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print(String("X-Api-Token: ") + API_TOKEN + "\r\n");
  client.print(String("Content-Length: ") + payload.length() + "\r\n\r\n");
  client.print(payload);
  client.flush(1);

  unsigned long timeout = millis();
  while (client.connected() && !client.available() && millis() - timeout < 2000) {
    delay(10);
  }

  while (client.available()) {
    String line = client.readStringUntil('\n');
    Serial1.println(line);
  }

  client.stop(1);
  return true;
}


// void pushUpdate() {
//   if (WiFi.status() != WL_CONNECTED) { return; }
//   if (CurrentMpptData.frameValid == false) { return; }

//   HTTPClient http;
//   http.begin(INVERTER_URL);
//   http.addHeader("Content-Type", "application/json");
//   http.addHeader("X-Api-Token", API_TOKEN);

//   String payload = buildJson();
//   int code = http.PUT(payload);
//   Serial.print("PUT ");
//   Serial.print(INVERTER_URL);
//   Serial.print(" -> ");
//   Serial.println(code);
//   if (code > 0) {
//     String response = http.getString();
//     Serial.println(response);
//   }
//   http.end();
// }

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

#pragma endregion

void println(const arduino::String& text) {
  Serial1.println(text);
}

void print(const arduino::String& text) {
  Serial1.print(text);
}

void print(const char symbol) {
  Serial1.print(symbol);
}
