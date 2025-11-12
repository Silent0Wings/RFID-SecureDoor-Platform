#include <WiFi.h>
#include <HTTPClient.h>
#include <BluetoothSerial.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include <WiFiClientSecure.h>

#define BUZZER_PIN 21
const char* ssid = "Chimera";
const char* password = "Sranklord1";
const char* bNames = "IoT_Jukebox";
static const char* API_BASE = "https://iotjukebox.onrender.com";

BluetoothSerial SerialBT;
volatile char btCmd = 0;
String bleData = "";

// ===== Utility =====
String encodeMac(String mac) {
  mac.replace(":", "%3A");
  return mac;
}

// ===== HTTP GET with Retry =====
void sendGet(String mac, String key, String value) {
  String url = String(API_BASE) + "/preference?id=" + encodeMac(mac) + "&key=" + key + "&value=" + value;

  Serial.println("GET URL: " + url);
  int code = -1;
  HTTPClient http;

  for (int i = 1; i <= 5; i++) {  // up to 5 tries
    // Use WiFiClientSecure for HTTPS GET
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate validation
    http.begin(client, url);
    http.setTimeout(15000);  // 15s timeout per try
    code = http.GET();

    Serial.printf("GET (try %d) -> HTTP %d\n", i, code);
    if (code == 200) {
      Serial.println(http.getString());
      http.end();
      return;
    }
    http.end();
    delay(1000);  // short pause before retry
  }
  Serial.println("GET failed after retries.");
}

// ===================================================================
// ===== MODIFIED HTTP POST FUNCTION =====
// ===================================================================
// This version matches your working PowerShell example:
// It sends a POST request with all parameters in the URL query string,
// and an empty request body.
void sendPost(String mac, String key, String value) {

  // Build the full URL with parameters, just like the GET request
  // Note: We use encodeMac(mac) for the 'id' parameter.
  String url = String(API_BASE) + "/preference?id=" + encodeMac(mac) + "&key=" + key + "&value=" + value;

  Serial.println("== PowerShell-style POST ==");
  Serial.println("Invoke-WebRequest -Uri \"" + url + "\" -Method POST");

  WiFiClientSecure client;
  client.setInsecure();  // Required for HTTPS
  HTTPClient http;

  // Begin with the *full* URL
  http.begin(client, url);
  // No "Content-Type" header is needed since the body is empty

  // Send the POST request with an EMPTY body
  int code = http.POST("");

  Serial.printf("POST -> HTTP %d\n", code);
  if (code > 0)
    Serial.println(http.getString());
  else
    Serial.println("POST failed");

  http.end();
}
// ===================================================================
// ===== END OF MODIFIED SECTION =====
// ===================================================================


// ===== Security Callbacks =====
class MySecurity : public BLESecurityCallbacks {
public:
  uint32_t onPassKeyRequest() override {
    Serial.println("Passkey requested. Use 123456.");
    return 123456;
  }
  void onPassKeyNotify(uint32_t pass_key) override {
    Serial.printf("Passkey displayed: %06u\n", pass_key);
  }
  bool onConfirmPIN(uint32_t) override {
    Serial.println("PIN auto-confirmed");
    return true;
  }
  bool onSecurityRequest() override {
    Serial.println("Security request accepted");
    return true;
  }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t auth) override {
    if (auth.success) Serial.println("Bonded and authenticated");
    else Serial.println("Authentication failed");
  }
};

// ===== BLE Callbacks =====
class ConnCB : public BLEServerCallbacks {
  void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* p) override {
    const uint8_t* a = p->connect.remote_bda;
    char mac[18];
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
    Serial.printf("BLE connected: %s\n", mac);
    esp_ble_gap_security_rsp(p->connect.remote_bda, true);
    esp_ble_set_encryption(p->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
  }
  void onDisconnect(BLEServer* s) override {
    Serial.println("BLE disconnected. Advertising...");
    s->getAdvertising()->start();
  }
};

// ===== CMD characteristic =====
class CmdCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String s = String(c->getValue().c_str());
    if (s.length() > 0) {
      btCmd = s[0];
      Serial.printf("Received CMD: %c\n", btCmd);
    }
  }
};

// ===== DATA characteristic =====
class DataCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String s = String(c->getValue().c_str());
    s.toLowerCase();
    int len = s.length();
    if (len == 0) return;

    if (len == 1) {
      btCmd = s[0];
      Serial.printf("CMD received: %c\n", btCmd);
    } else {
      bleData = s;
      Serial.printf("DATA stored: %s (%d bytes)\n", bleData.c_str(), len);

      // Perform HTTP GET and POST with retries
      String mac = WiFi.macAddress();
      String key = "IoT_Jukebox";

      // The 'value' parameter for both functions will be the data from BLE
      sendGet(mac, key, bleData);
      delay(500);
      sendPost(mac, key, bleData);
    }
  }
};

// ===== BLE Setup =====
void setupBLE() {
  BLEDevice::init("TTGO_Jukebox");
  BLEDevice::setSecurityCallbacks(new MySecurity());
  BLESecurity* sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec->setCapability(ESP_IO_CAP_OUT);
  sec->setKeySize(16);
  uint32_t passkey = 123456;
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(uint32_t));

  BLEServer* srv = BLEDevice::createServer();
  srv->setCallbacks(new ConnCB());

  // ===== CMD SERVICE =====
  BLEService* cmd = srv->createService("7e6a3000-0000-0000-0000-000000000001");
  BLECharacteristic* cmdChar = cmd->createCharacteristic(
    "7e6a3001-0000-0000-0000-000000000001",
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  cmdChar->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  cmdChar->setCallbacks(new CmdCB());
  cmdChar->setValue("P|N|B");
  cmd->start();

  // ===== DATA CHANNEL =====
  BLEService* data = srv->createService("7e6a5000-0000-0000-0000-000000000001");
  BLECharacteristic* dataChar = data->createCharacteristic(
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

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  SerialBT.begin(bNames, true);
  Serial.println("Classic Bluetooth active");
  setupBLE();
}

// ===== Loop =====
void loop() {
  if (btCmd) {
    Serial.printf("Command: %c\n", btCmd);
    btCmd = 0;
  }
  delay(100);
}