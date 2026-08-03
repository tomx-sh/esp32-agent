#include "ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

namespace {
constexpr char kDeviceName[] = "ESP32 AMOLED";
constexpr char kServiceUuid[] = "12345678-1234-1234-1234-1234567890ab";
constexpr char kCharacteristicUuid[] = "12345678-1234-1234-1234-1234567890ac";

NimBLECharacteristic *statusCharacteristic = nullptr;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
    Serial.printf(
        "BLE client connected: %s\n",
        connInfo.getAddress().toString().c_str());
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override {
    Serial.printf(
        "BLE client disconnected: %s, reason=%d\n",
        connInfo.getAddress().toString().c_str(),
        reason);
    NimBLEDevice::startAdvertising();
    Serial.println("BLE advertising restarted");
  }
};

class StatusCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
    Serial.printf(
        "BLE characteristic read by %s\n",
        connInfo.getAddress().toString().c_str());
  }

  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
    const std::string value = characteristic->getValue();
    Serial.printf(
        "BLE characteristic write from %s: %s\n",
        connInfo.getAddress().toString().c_str(),
        value.c_str());
  }
};
}  // namespace

void ble_init() {
  NimBLEDevice::init(kDeviceName);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService *service = server->createService(kServiceUuid);
  statusCharacteristic = service->createCharacteristic(
      kCharacteristicUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  statusCharacteristic->setValue("hello");
  statusCharacteristic->setCallbacks(new StatusCallbacks());

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setName(kDeviceName);
  advertising->enableScanResponse(true);
  advertising->start();

  Serial.println("BLE advertising started");
}
