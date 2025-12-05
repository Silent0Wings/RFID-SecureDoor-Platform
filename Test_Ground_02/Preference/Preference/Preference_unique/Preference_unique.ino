#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>
#include "esp_gatts_api.h"

static const char* SVC_UUID = "91bad492-b950-4226-aa2b-4ede9fa42f59";
static const char* ID_UUID  = "7e6a0001-0000-0000-0000-000000000001"; // optional login
static const char* CMD_UUID = "7e6a0002-0000-0000-0000-000000000001"; // commands P,N,B,S

class ConnCB : public BLEServerCallbacks {
  void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* p) override {
    const uint8_t* a = p->connect.remote_bda;
    char mac[18]; sprintf(mac,"%02X:%02X:%02X:%02X:%02X:%02X",a[0],a[1],a[2],a[3],a[4],a[5]);
    Serial.printf("Connected. Addr may rotate: %s\n", mac);
  }
  void onDisconnect(BLEServer* s) override {
    Serial.println("Disconnected. Advertising...");
    s->getAdvertising()->start();
  }
};

class IdCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();                      // Arduino String
    if (v.length()) Serial.printf("Stable ClientID: %s\n", v.c_str());
  }
};

class CmdCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();                      // expect 1 char or short text
    if (!v.length()) return;
    char cmd = v[0];
    switch (cmd) {
      case 'P': Serial.println("CMD: PLAY/TOGGLE"); break;
      case 'N': Serial.println("CMD: NEXT"); break;
      case 'B': Serial.println("CMD: PREV"); break;
      case 'S': Serial.println("CMD: STOP/PAUSE"); break;
      default:  Serial.printf("CMD: '%c' (unknown)\n", cmd); break;
    }
  }
};

void setup() {
  Serial.begin(115200);
  BLEDevice::init("ESP32_Server");
  BLEServer* srv = BLEDevice::createServer();
  srv->setCallbacks(new ConnCB());

  BLEService* svc = srv->createService(SVC_UUID);

  // Optional login ID characteristic
  BLECharacteristic* idChar = svc->createCharacteristic(
    ID_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ
  );
  idChar->setCallbacks(new IdCB());
  idChar->setValue("ID_READY");

  // Command characteristic (write or write without response)
  BLECharacteristic* cmdChar = svc->createCharacteristic(
    CMD_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  cmdChar->setCallbacks(new CmdCB());

  svc->start();
  srv->getAdvertising()->start();
  Serial.println("Advertising. Use nRF Connect to write P, N, B, S.");
}

void loop() {}
