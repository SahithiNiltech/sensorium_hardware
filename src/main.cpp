/*
  ESP32-S3 BLE JSON Example
  - BLE GATT server with default UUIDs
  - Read, Write, Notify support for a single characteristic
  - Sends/receives JSON (e.g., {"msg": "Hello from ESP32"})
  - Notifies app with JSON every 5 seconds
  - Broadcasts sensor data in advertisement scan response (readable WITHOUT connecting)
  - For use with Sensorium mobile app

  Instructions:
  1. Open this project in VS Code with PlatformIO.
  2. Select the esp32s3 environment.
  3. Upload and open Serial Monitor for debug.
*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <BLEAdvertisedDevice.h>

// Default example UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLECharacteristic *pCharacteristic;
BLEAdvertising *pAdvertising;
bool deviceConnected = false;
String lastWritten = "{\"msg\": \"Hello from ESP32\"}";

// Simulated sensor values — replace with real sensor reads if available
float sensorTemp     = 25.0;
float sensorHumidity = 60.0;

// Build compact JSON payload for advertisement.
// Must stay under 27 bytes (31-byte BLE limit minus 4-byte overhead).
// Uses integer t/h (no decimals) + uptime seconds "s" to stay within limit.
// Example: {"t":24,"h":67,"s":61} = 22 chars — fits fine.
// App converts "s" to "time":"HH:MM:SS" for display.
String buildSensorJson() {
  unsigned long sec = millis() / 1000;
  char json[28];
  snprintf(json, sizeof(json), "{\"t\":%d,\"h\":%d,\"s\":%lu}",
           (int)sensorTemp, (int)sensorHumidity, sec);
  return String(json);
}

// Update BLE advertisement + scan response with latest sensor data.
//
// Packet budget (31 bytes max each):
//   Primary adv:   Flags(3) + ManufacturerData(1+1+2+22=26) = 29 bytes
//                  → Web Bluetooth passive scan receives THIS packet
//   Scan response: Name "ESP32-S3-JSON"(2+13=15) = 15 bytes
//                  → Mobile active scan also reads this
//
// macOS CoreBluetooth strips manufacturer data from watchAdvertisements() events.
// Using Service Data (UUID 0xFFF0) instead — macOS exposes this when the web app
// declares optionalServices: ['0000fff0-0000-1000-8000-00805f9b34fb'].
//
// Packet budget (31 bytes max):
//   Primary adv: Flags(3) + ServiceData(1+1+2+22=26) = 29 bytes ✓
//   Scan response: Name "ESP32-S3-JSON"(2+13=15) = 15 bytes ✓
void updateAdvertisement() {
  pAdvertising->stop();

  // Primary advertisement: service data (UUID 0xFFF0) with sensor JSON
  // Web Bluetooth maps this to event.serviceData.get('0000fff0-...')
  BLEAdvertisementData advData;
  String payload = buildSensorJson();
  advData.setServiceData(BLEUUID((uint16_t)0xFFF0), std::string(payload.c_str()));

  // Scan response: device name (for active scanners like mobile)
  BLEAdvertisementData scanResponse;
  scanResponse.setName("ESP32-S3-JSON");

  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(scanResponse);
  pAdvertising->start();
}

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    String value = String(pChar->getValue().c_str());
    Serial.print("[ESP32] Received from app: ");
    Serial.println(value);
    lastWritten = value;
  }
  void onRead(BLECharacteristic *pChar) override {
    Serial.println("[ESP32] App requested read");
    pChar->setValue(lastWritten.c_str());
  }
};

class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("[ESP32] App connected");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("[ESP32] App disconnected");
    delay(500);
    updateAdvertisement();
    Serial.println("[ESP32] Restarted advertising");
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("[ESP32] Starting BLE JSON server...");

  BLEDevice::init("ESP32-S3-JSON");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setValue(lastWritten.c_str());
  pService->start();

  pAdvertising = BLEDevice::getAdvertising();
  updateAdvertisement();
  Serial.println("[ESP32] BLE advertising started with sensor data in scan response");
}

unsigned long lastNotify = 0;

void loop() {
  // Update sensor values and advertisement every 5 seconds
  if (millis() - lastNotify > 5000) {
    // Simulate sensor changes — replace with real sensor reads if available
    sensorTemp     = 22.0 + (float)(millis() / 1000 % 10);
    sensorHumidity = 55.0 + (float)(millis() / 1000 % 20);

    // Always update advertisement so passive scanners see fresh values
    updateAdvertisement();
    unsigned long sec = millis() / 1000;
    char ts[9];
    snprintf(ts, sizeof(ts), "%02lu:%02lu:%02lu", sec/3600, (sec%3600)/60, sec%60);
    // Pretty-print JSON with timestamp to serial
    Serial.println("[ESP32] Updated advertisement:");
    Serial.println("{");
    Serial.print("  \"t\": ");   Serial.print(sensorTemp, 1);    Serial.println(",");
    Serial.print("  \"h\": ");   Serial.print(sensorHumidity, 1); Serial.println(",");
    Serial.print("  \"time\": \""); Serial.print(ts); Serial.println("\"");
    Serial.println("}");

    // Also notify connected app if any
    if (deviceConnected) {
      String notifyMsg = String("{\"msg\":\"ESP32\",\"t\":") + sensorTemp + ",\"h\":" + sensorHumidity + "}";
      pCharacteristic->setValue(notifyMsg.c_str());
      pCharacteristic->notify();
      Serial.print("[ESP32] Notified connected app: ");
      Serial.println(notifyMsg);
    }

    lastNotify = millis();
  }

  // Manual send: check for serial input
  if (deviceConnected && Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      String jsonMsg = "{\"msg\": \"" + input + "\"}";
      pCharacteristic->setValue(jsonMsg.c_str());
      pCharacteristic->notify();
      Serial.print("[ESP32] Manually sent to app: ");
      Serial.println(jsonMsg);
    }
  }

  delay(100);
}
