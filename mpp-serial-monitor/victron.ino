
void readVeDirect() {
  while (veSerial.available()) {
    char c = (char)veSerial.read();
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
  }
}

int toIntSafe(const String& value, int fallback = 0) {
  if (value.length() == 0) return fallback;
  return value.toInt();
}

void commitFrame() {
  CurrentMpptData.deviceInstance = 257;
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