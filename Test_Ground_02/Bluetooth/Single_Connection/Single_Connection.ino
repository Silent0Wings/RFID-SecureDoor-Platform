#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>
#include "esp_gap_ble_api.h"

/*
so this will force a pairing threw the nrf connect app
which will promp a pass key 123456 in this case
to force a bounded bluetooth connection allowing for a stable unique identifier

*/

// ===== Security Callbacks =====
class MySecurity : public BLESecurityCallbacks {
 public:
  uint32_t onPassKeyRequest() override {
    Serial.println("Passkey requested (enter 123456 on client)");
    return 123456;
  }

  void onPassKeyNotify(uint32_t pass_key) override {
    Serial.printf("Passkey displayed: %06u\n", pass_key);
  }

  bool onConfirmPIN(uint32_t) override {
    Serial.println("PIN confirmed automatically");
    return true;
  }

  bool onSecurityRequest() override {
    Serial.println("Security request accepted");
    return true;
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t auth) override {
    if (auth.success)
      Serial.println("Bonded and authenticated");
    else
      Serial.println("Authentication failed");
  }
};

// ===== Server Callbacks =====
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override {
    char mac[18];
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
            param->connect.remote_bda[0],
            param->connect.remote_bda[1],
            param->connect.remote_bda[2],
            param->connect.remote_bda[3],
            param->connect.remote_bda[4],
            param->connect.remote_bda[5]);
    Serial.printf("Device connected: %s\n", mac);

    // Force encryption with MITM (passkey)
    esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
  }

  void onDisconnect(BLEServer* pServer) override {
    Serial.println("Device disconnected, restarting advertising");
    pServer->getAdvertising()->start();
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting BLE Bonded Server...");

  // ===== Init BLE device =====
  BLEDevice::init("ESP32_Bonded_Server");

  // ===== Configure Security =====
  BLESecurity* sec = new BLESecurity();
  BLEDevice::setSecurityCallbacks(new MySecurity());
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec->setCapability(ESP_IO_CAP_OUT);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  // Static 6-digit passkey
  uint32_t passkey = 123456;
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(passkey));

  // ===== Server + Advertising =====
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());

  BLEService* service = server->createService("7e6a1000-0000-0000-0000-000000000001");
  service->start();

  server->getAdvertising()->start();
  Serial.println("Advertising started (Bonded, Passkey = 123456)");
}

void loop() {
  delay(1000);
}
