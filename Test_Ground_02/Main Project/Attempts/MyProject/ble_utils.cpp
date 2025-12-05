#include "ble_utils.h"
#include "security.h"
#include "wifi_utils.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include <WiFi.h>

volatile char btCmd = 0;
String bleData;

class ConnCB : public BLEServerCallbacks
{
    void onConnect(BLEServer *s, esp_ble_gatts_cb_param_t *p) override
    {
        const uint8_t *a = p->connect.remote_bda;
        char mac[18];
        sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
        Serial.printf("BLE connected: %s\n", mac);
        esp_ble_gap_security_rsp(p->connect.remote_bda, true);
        esp_ble_set_encryption(p->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
    }
    void onDisconnect(BLEServer *s) override
    {
        Serial.println("BLE disconnected. Advertising...");
        s->getAdvertising()->start();
    }
};

class CmdCB : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *c) override
    {
        String s = String(c->getValue().c_str());
        if (s.length() > 0)
        {
            btCmd = s[0];
            Serial.printf("Received CMD: %c\n", btCmd);
        }
    }
};

class DataCB : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *c) override
    {
        String s = String(c->getValue().c_str());
        s.toLowerCase();
        int len = s.length();
        if (len == 0)
            return;

        if (len == 1)
        {
            btCmd = s[0];
            Serial.printf("CMD received: %c\n", btCmd);
        }
        else
        {
            bleData = s;
            Serial.printf("DATA stored: %s (%d bytes)\n", bleData.c_str(), len);

            String mac = WiFi.macAddress();
            String key = "IoT_Jukebox";
            sendGet(mac, key, bleData);
            delay(500);
            sendPost(mac, key, bleData);
        }
    }
};

void setupBLE()
{
    BLEDevice::init("TTGO_Jukebox");

    BLEDevice::setSecurityCallbacks(new MySecurity());
    BLESecurity *sec = new BLESecurity();
    sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    sec->setCapability(ESP_IO_CAP_OUT);
    sec->setKeySize(16);
    uint32_t passkey = 123456;
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(uint32_t));

    BLEServer *srv = BLEDevice::createServer();
    srv->setCallbacks(new ConnCB());

    BLEService *cmd = srv->createService("7e6a3000-0000-0000-0000-000000000001");
    BLECharacteristic *cmdChar = cmd->createCharacteristic(
        "7e6a3001-0000-0000-0000-000000000001",
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    cmdChar->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
    cmdChar->setCallbacks(new CmdCB());
    cmdChar->setValue("P|N|B");
    cmd->start();

    BLEService *data = srv->createService("7e6a5000-0000-0000-0000-000000000001");
    BLECharacteristic *dataChar = data->createCharacteristic(
        "7e6a5001-0000-0000-0000-000000000001",
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
    dataChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED | ESP_GATT_PERM_READ_ENCRYPTED);
    dataChar->addDescriptor(new BLE2902());
    dataChar->setCallbacks(new DataCB());
    dataChar->setValue("DATA");
    data->start();

    srv->getAdvertising()->addServiceUUID(cmd->getUUID());
    srv->getAdvertising()->addServiceUUID(data->getUUID());
    srv->getAdvertising()->start();
    Serial.println("BLE advertising started (CMD + DATA)");
}
