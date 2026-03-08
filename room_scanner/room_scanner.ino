#include <WiFi.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEBeacon.h>

#include <string.h>

#define ENDIAN_CHANGE_U16(x) ((((x) & 0xFF00) >> 8) + (((x) & 0xFF) << 8))

// ---------------------------------------------------------------------------
// Network configuration
// ---------------------------------------------------------------------------
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char *MQTT_BROKER = "YOUR_MQTT_BROKER_IP";
const uint16_t MQTT_PORT = 1883;

// ---------------------------------------------------------------------------
// Room configuration
// ---------------------------------------------------------------------------
const char *ROOM_ID = "CI-214";
const char *ROOM_NAME = "CI 214 - Faculty Demo Room";

// ---------------------------------------------------------------------------
// BLE badge configuration
// ---------------------------------------------------------------------------
const char *ORG_UUID = "FDA50693-A4E2-4FB1-AFCF-C6EB07647825";
const int SCAN_TIME_SECONDS = 2;
const int RSSI_THRESHOLD = -78;
const unsigned long ABSENCE_TIMEOUT_MS = 15000;
const unsigned long STATUS_INTERVAL_MS = 30000;
const size_t MAX_TRACKED_BADGES = 32;

struct BadgeState {
  bool inUse;
  bool present;
  uint16_t major;
  uint16_t minor;
  int lastRssi;
  unsigned long lastSeenMs;
};

BadgeState badgeStates[MAX_TRACKED_BADGES];

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
BLEScan *pBLEScan;
unsigned long lastStatusPublishMs = 0;

String jsonEscape(const char *value) {
  String escaped = "";
  for (size_t i = 0; value[i] != '\0'; i++) {
    char c = value[i];
    if (c == '\"' || c == '\\') {
      escaped += '\\';
    }
    escaped += c;
  }
  return escaped;
}

int findBadgeSlot(uint16_t major, uint16_t minor) {
  for (size_t i = 0; i < MAX_TRACKED_BADGES; i++) {
    if (badgeStates[i].inUse && badgeStates[i].major == major && badgeStates[i].minor == minor) {
      return (int)i;
    }
  }
  return -1;
}

int allocateBadgeSlot(uint16_t major, uint16_t minor) {
  int existingSlot = findBadgeSlot(major, minor);
  if (existingSlot >= 0) {
    return existingSlot;
  }

  for (size_t i = 0; i < MAX_TRACKED_BADGES; i++) {
    if (!badgeStates[i].inUse) {
      badgeStates[i].inUse = true;
      badgeStates[i].present = false;
      badgeStates[i].major = major;
      badgeStates[i].minor = minor;
      badgeStates[i].lastRssi = 0;
      badgeStates[i].lastSeenMs = 0;
      return (int)i;
    }
  }

  return -1;
}

bool uuidMatches(const String &uuid) {
  return uuid.equalsIgnoreCase(ORG_UUID);
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

void connectMqtt() {
  if (mqttClient.connected()) {
    return;
  }

  while (!mqttClient.connected()) {
    String clientId = String("room-scanner-") + ROOM_ID;
    Serial.printf("Connecting to MQTT broker %s:%d...\n", MQTT_BROKER, MQTT_PORT);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("MQTT connected.");
      return;
    }

    Serial.printf("MQTT connect failed rc=%d, retrying in 2s\n", mqttClient.state());
    delay(2000);
  }
}

void publishSystemStatus() {
  String topic = String("faculty/system/") + ROOM_ID;
  String payload = "{";
  payload += "\"room_id\":\"" + jsonEscape(ROOM_ID) + "\",";
  payload += "\"room_name\":\"" + jsonEscape(ROOM_NAME) + "\",";
  payload += "\"status\":\"online\",";
  payload += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  payload += "}";

  mqttClient.publish(topic.c_str(), payload.c_str(), true);
  lastStatusPublishMs = millis();
}

void publishPresenceEvent(uint16_t major, uint16_t minor, bool present, int rssi) {
  String topic = String("faculty/presence/") + ROOM_ID;
  String payload = "{";
  payload += "\"room_id\":\"" + jsonEscape(ROOM_ID) + "\",";
  payload += "\"room_name\":\"" + jsonEscape(ROOM_NAME) + "\",";
  payload += "\"major\":" + String(major) + ",";
  payload += "\"minor\":" + String(minor) + ",";
  payload += "\"present\":" + String(present ? "true" : "false") + ",";
  payload += "\"rssi\":" + String(present ? rssi : 0) + ",";
  payload += "\"timestamp\":" + String(millis() / 1000);
  payload += "}";

  mqttClient.publish(topic.c_str(), payload.c_str(), true);
  Serial.printf(
    "[%s] badge_%d_%d in %s (major=%d minor=%d rssi=%d)\n",
    present ? "ENTER" : "EXIT",
    major,
    minor,
    ROOM_ID,
    major,
    minor,
    rssi
  );
}

void handleBadgeSeen(uint16_t major, uint16_t minor, int rssi) {
  if (rssi < RSSI_THRESHOLD) {
    return;
  }

  int badgeIndex = allocateBadgeSlot(major, minor);
  if (badgeIndex < 0) {
    Serial.printf("No free badge slots for major=%d minor=%d rssi=%d\n", major, minor, rssi);
    return;
  }

  BadgeState &state = badgeStates[badgeIndex];
  state.lastSeenMs = millis();
  state.lastRssi = rssi;

  if (!state.present) {
    state.present = true;
    publishPresenceEvent(state.major, state.minor, true, rssi);
  }
}

void expireAbsentBadges() {
  unsigned long now = millis();
  for (size_t i = 0; i < MAX_TRACKED_BADGES; i++) {
    BadgeState &state = badgeStates[i];
    if (!state.inUse || !state.present) {
      continue;
    }

    if (now - state.lastSeenMs > ABSENCE_TIMEOUT_MS) {
      state.present = false;
      publishPresenceEvent(state.major, state.minor, false, state.lastRssi);
    }
  }
}

class BadgeCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!advertisedDevice.haveManufacturerData()) {
      return;
    }

    String manufacturerData = advertisedDevice.getManufacturerData();
    if (manufacturerData.length() != 25) {
      return;
    }

    const uint8_t *raw = (const uint8_t *)manufacturerData.c_str();
    if (raw[0] != 0x4C || raw[1] != 0x00) {
      return;
    }

    BLEBeacon beacon;
    beacon.setData(manufacturerData);

    String uuid = beacon.getProximityUUID().toString().c_str();
    if (!uuidMatches(uuid)) {
      return;
    }

    uint16_t major = ENDIAN_CHANGE_U16(beacon.getMajor());
    uint16_t minor = ENDIAN_CHANGE_U16(beacon.getMinor());
    int rssi = advertisedDevice.getRSSI();

    handleBadgeSeen(major, minor, rssi);
  }
};

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Starting room scanner...");

  connectWiFi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(512);
  connectMqtt();
  publishSystemStatus();

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new BadgeCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  Serial.printf("Scanning for UUID %s in room %s\n", ORG_UUID, ROOM_ID);
}

void loop() {
  connectWiFi();
  connectMqtt();
  mqttClient.loop();

  BLEScanResults *scanResults = pBLEScan->start(SCAN_TIME_SECONDS, false);
  Serial.printf("Scan complete, %d devices seen\n", scanResults->getCount());
  pBLEScan->clearResults();

  expireAbsentBadges();

  if (millis() - lastStatusPublishMs > STATUS_INTERVAL_MS) {
    publishSystemStatus();
  }

  delay(250);
}
