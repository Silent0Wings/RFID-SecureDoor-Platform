#include <BLEDevice.h>
#include <BLEServer.h>
#include "esp_gatts_api.h"  // for esp_ble_gatts_cb_param_t

// Wait for a single BLE client; print peer/local MAC on connect
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
    char peer[18];
    sprintf(peer, "%02X:%02X:%02X:%02X:%02X:%02X",
            param->connect.remote_bda[0],
            param->connect.remote_bda[1],
            param->connect.remote_bda[2],
            param->connect.remote_bda[3],
            param->connect.remote_bda[4],
            param->connect.remote_bda[5]);

    Serial.printf("Client connected\n  Peer MAC : %s\n  Local MAC: %s\n",
                  peer, BLEDevice::getAddress().toString().c_str());

    // Keep one connection
    server->getAdvertising()->stop();
  }

  void onDisconnect(BLEServer* server) override {
    Serial.println("Client disconnected");
    server->getAdvertising()->start();
  }
};


void setup() {
  Serial.begin(115200);
  BLEDevice::init("ESP32_Server");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());

  BLEAdvertising* adv = server->getAdvertising();
  adv->setScanResponse(true);
  adv->start();

  Serial.printf("Waiting for BLE client...\nLocal MAC: %s\n",
                BLEDevice::getAddress().toString().c_str());
}

void loop() {}
