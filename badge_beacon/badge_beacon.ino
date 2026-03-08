/*
 * Faculty BLE Badge — DIY iBeacon Broadcaster
 * =============================================
 * 
 * Hardware: ESP32-C6 Mini (~$3-5 on AliExpress/Amazon)
 * Battery:  3.7V LiPo (300-600mAh) via TP4056 charger module
 * 
 * What it does:
 *   Wakes up → broadcasts iBeacon advertisement for ~100ms → deep sleeps → repeat
 *   That's it. No WiFi, no scanning, no complexity.
 * 
 * The room scanner (separate ESP32) picks up these broadcasts.
 * 
 * Power budget (deep sleep cycle):
 *   Active BLE advertising: ~130mA for ~100ms
 *   Deep sleep: ~5µA
 *   With 1-second wake cycle and 500mAh battery: ~30-45 days
 *   With 2-second wake cycle and 500mAh battery: ~60-80 days
 * 
 * ============================================================
 * STUDENT CONFIGURATION - Change these per badge
 * ============================================================
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEBeacon.h>
#include <esp_sleep.h>
#include <esp_bt.h>

// ---- BADGE IDENTITY (unique per faculty member) ----

// Organization UUID — same across ALL badges in your deployment
// Generate your own at https://www.uuidgenerator.net/
#define BEACON_UUID "FDA50693-A4E2-4FB1-AFCF-C6EB07647825"
// The ESP32 BLE iBeacon helper expects the UUID bytes in reverse order.
#define BEACON_UUID_REV "25786407-EBC6-CFAF-B14F-E2A49306A5FD"

// Major: group identifier (1 = faculty, 2 = staff, 3 = student worker, etc.)
#define BEACON_MAJOR 1

// Minor: unique per person — THIS IS WHAT CHANGES PER BADGE
// Badge 1 = Dr. Smith, Badge 2 = Dr. Jones, etc.
#define BEACON_MINOR 1

// ---- POWER & TIMING ----

// Temporary debug mode for phone-based verification.
// Set to false after testing so the badge returns to low-power duty cycling.
#define PHONE_DEBUG_MODE true

// How long to advertise before going back to sleep (milliseconds)
// 100ms is enough for a scanner running 5-second scan windows
#define ADVERTISE_DURATION_MS 150

// Deep sleep duration between advertisements (microseconds)
// 1 second = good detection speed, shorter battery life
// 2 seconds = still fast enough, better battery life
#define SLEEP_DURATION_US 1000000  // 1 second (1,000,000 µs)

// TX Power level — controls range and battery consumption
// ESP_PWR_LVL_N12 = -12dBm (very short range, best battery)
// ESP_PWR_LVL_N6  = -6dBm  (short range)
// ESP_PWR_LVL_N0  =  0dBm  (medium range, good default for classroom)
// ESP_PWR_LVL_P6  = +6dBm  (long range, worst battery)
// ESP_PWR_LVL_P9  = +9dBm  (maximum range)
#define TX_POWER ESP_PWR_LVL_N0
// ---- LED FEEDBACK ----
// Many ESP32 dev boards either use a different LED pin or have no usable built-in LED.
#define LED_PIN 8
#define BLINK_ON_ADVERTISE false


// ============================================================
// iBeacon Setup — Students don't need to modify below this line
// (but reading it is part of the lab!)
// ============================================================

BLEServer *pServer;
BLEAdvertising *pAdvertising;

void setBeacon() {
  BLEBeacon oBeacon = BLEBeacon();
  
  // Set the iBeacon manufacturer ID (Apple's BLE company ID: 0x004C)
  oBeacon.setManufacturerId(0x4C00);  // Note: byte-swapped for little-endian
  
  // Set the proximity UUID — identifies our organization/deployment
  oBeacon.setProximityUUID(BLEUUID(BEACON_UUID_REV));
  
  // Major and Minor — identify the group and individual
  oBeacon.setMajor(BEACON_MAJOR);
  oBeacon.setMinor(BEACON_MINOR);
  
  // Signal power at 1 meter — helps receivers estimate distance
  // Set to -59 dBm (typical calibrated value for BLE at 1m)
  oBeacon.setSignalPower(-59);

  // Build the advertisement data
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  
  // Match the official ESP32 iBeacon example format.
  oAdvertisementData.setFlags(0x1A);
  oAdvertisementData.setManufacturerData(oBeacon.getData());
  
  pAdvertising->setAdvertisementData(oAdvertisementData);

  // Expose a simple name as a fallback identifier for scanners that do not
  // parse the iBeacon payload correctly on every board family.
  BLEAdvertisementData oScanResponseData = BLEAdvertisementData();
  char deviceName[32];
  snprintf(deviceName, sizeof(deviceName), "BADGE-%d-%d", BEACON_MAJOR, BEACON_MINOR);
  oScanResponseData.setName(deviceName);
  pAdvertising->setScanResponseData(oScanResponseData);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Booting badge beacon...");

  // LED setup
  if (BLINK_ON_ADVERTISE) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // LED on (active low on C3 Super Mini)
  }

  char deviceName[32];
  snprintf(deviceName, sizeof(deviceName), "BADGE-%d-%d", BEACON_MAJOR, BEACON_MINOR);
  BLEDevice::init(deviceName);
  pServer = BLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();
  
  // Set transmit power
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, TX_POWER);
  
  // Configure advertising
  setBeacon();

  // Start advertising
  pAdvertising->start();
  Serial.printf("Advertising as iBeacon: major=%d minor=%d\n", BEACON_MAJOR, BEACON_MINOR);

  // Brief LED flash to show we're alive
  if (BLINK_ON_ADVERTISE) {
    delay(20);
    digitalWrite(LED_PIN, HIGH);  // LED off
  }

  // For phone testing, keep advertising continuously so scanning apps can see it easily.
  if (PHONE_DEBUG_MODE) {
    return;
  }

  // Stay awake long enough for the scanner to pick us up
  delay(ADVERTISE_DURATION_MS);
  
  // Stop advertising before sleep (clean shutdown)
  pAdvertising->stop();
  
  // Fully tear down BLE before deep sleep to minimize current draw.
  // `true` also releases controller memory; that's fine because deep sleep reboots the MCU.
  BLEDevice::deinit(true);

  // Enter deep sleep
  // On wake, the ESP32 will restart from setup() — loop() is never reached
  esp_deep_sleep(SLEEP_DURATION_US);
}

void loop() {
  if (PHONE_DEBUG_MODE) {
    delay(1000);
    return;
  }

  // This never executes — deep sleep restarts from setup()
  // But if deep sleep fails for some reason, fall back to a delay loop
  delay(SLEEP_DURATION_US / 1000);
  ESP.restart();
}
