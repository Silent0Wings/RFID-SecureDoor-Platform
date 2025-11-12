// ===== ESP32 BLE (bonded) + Robust HTTPS GET (Render) =====
// One service, one WRITE command characteristic. Commands: WHO?, BONDS?, GET <name>
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "esp_gap_ble_api.h"

// ---------- WIFI ----------
const char* WIFI_SSID = "Chimera";
const char* WIFI_PASS = "Sranklord1";

// ---------- API ----------
static const char* API_BASE = "https://iotjukebox.onrender.com";

// Toggle insecure TLS for quick recovery. Set to false and provide CA when stable.
#define USE_INSECURE_TLS 1
// If you disable insecure mode, paste the correct root CA below and set USE_INSECURE_TLS to 0.
// static const char* ROOT_CA = R"(-----BEGIN CERTIFICATE-----
// <ISRG Root X1 or provider root for your host>
// -----END CERTIFICATE-----)";

// ---------- BLE IDs ----------
static const char* DEVICE_NAME = "ESP32_Bonded_Server";
static const char* SERVICE_UUID = "7e6a1000-0000-0000-0000-000000000001";
static const char* CMD_UUID = "7e6a2000-0000-0000-0000-000000000002";

// ---------- Globals ----------
BLECharacteristic* cmdChar = nullptr;
esp_bd_addr_t lastRemoteAddr = { 0 };
char lastRemoteMac[18] = { 0 };

// ---------- Utils ----------
static void formatMac(const uint8_t* bda, char out[18]) {
  sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}
static void listBondedDevices() {
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) {
    Serial.println("Bonded devices: 0");
    return;
  }
  esp_ble_bond_dev_t list[n];
  esp_ble_get_bond_device_list(&n, list);
  Serial.printf("Bonded devices: %d\n", n);
  for (int i = 0; i < n; ++i) {
    char mac[18];
    formatMac(list[i].bd_addr, mac);
    Serial.printf("  %d) %s\n", i + 1, mac);
  }
}

// Accurate time is required for TLS
static void setClock() {
  Serial.print("Sync time...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  uint32_t tries = 0;
  while (now < 8 * 3600 * 2 && tries++ < 50) {
    delay(200);
    now = time(nullptr);
  }
  Serial.printf(" done (epoch=%ld)\n", now);
}

// ---------- HTTPS GET with full diagnostics ----------
static bool httpsGetJson(const String& url, String& payloadOut) {
  payloadOut = "";

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(" WiFi not connected");
    return false;
  }

  WiFiClientSecure client;
#if USE_INSECURE_TLS
  client.setInsecure();  // quick fix for cert/clock issues
#else
  client.setCACert(ROOT_CA);
#endif
  client.setTimeout(12000);

  HTTPClient http;
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);  // avoid stale keep-alives
  http.useHTTP10(true);  // simpler transfer (no chunking)
  http.addHeader("Accept", "application/json");

  Serial.printf("GET %s\n", url.c_str());
  if (!http.begin(client, url)) {
    Serial.println(" http.begin() failed");
    return false;
  }

  int code = http.GET();
  Serial.printf("HTTP status: %d\n", code);

  if (code <= 0) {
    Serial.printf(" GET failed: %s\n", http.errorToString(code).c_str());
    http.end();
    return false;
  }

  payloadOut = http.getString();
  Serial.printf("Body length: %d\n", payloadOut.length());
  if (payloadOut.length() > 0) {
    unsigned int headLen = (payloadOut.length() < 160u) ? payloadOut.length() : 160u;
    String head = payloadOut.substring(0, headLen);
    head.replace("\n", "\\n");
    head.replace("\n", "\\n");
    Serial.printf("Body head: %s\n", head.c_str());
  }

  http.end();
  return code == 200;
}

// High-level song fetch with JSON parse and errors
static bool loadSongByName(const String& name) {
  String url = String(API_BASE) + "/song?name=" + name;
  String body;
  if (!httpsGetJson(url, body)) {
    Serial.println(" Failed to GET");
    return false;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf(" JSON parse error: %s\n", err.c_str());
    return false;
  }

  // Minimal validation: require a "name" field
  if (!doc.containsKey("name")) {
    Serial.println(" JSON missing 'name'");
    return false;
  }

  Serial.printf(" Loaded song: %s\n", doc["name"].as<const char*>());
  // TODO: parse the rest of the fields your player needs
  return true;
}

// ---------- Security ----------
class MySecurity : public BLESecurityCallbacks {
public:
  uint32_t onPassKeyRequest() override {
    Serial.println("Passkey requested. Use 123456.");
    return 123456;
  }
  void onPassKeyNotify(uint32_t pass_key) override {
    Serial.printf("Display passkey: %06u\n", pass_key);
  }
  bool onConfirmPIN(uint32_t) override {
    Serial.println("PIN confirmed");
    return true;
  }
  bool onSecurityRequest() override {
    Serial.println("Security request accepted");
    return true;
  }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t auth) override {
    if (auth.success) {
      Serial.println("Bonded and authenticated");
      listBondedDevices();
    } else {
      Serial.printf("Auth failed: 0x%02X\n", auth.fail_reason);
    }
  }
};

// ---------- BLE ----------
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* p) override {
    memcpy(lastRemoteAddr, p->connect.remote_bda, sizeof(esp_bd_addr_t));
    formatMac(p->connect.remote_bda, lastRemoteMac);
    Serial.printf("Device connected: %s\n", lastRemoteMac);
    esp_ble_set_encryption(p->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
  }
  void onDisconnect(BLEServer* s) override {
    Serial.println("Device disconnected. Advertising...");
    s->getAdvertising()->start();
  }
};

class CmdCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    auto v = c->getValue();
    String s = String(v.c_str());
    s.trim();
    if (!s.length()) return;
    Serial.printf("CMD %s: %s\n", lastRemoteMac[0] ? lastRemoteMac : "UNKNOWN", s.c_str());

    if (s.equalsIgnoreCase("WHO?")) {
      Serial.printf("Peer: %s\n", lastRemoteMac[0] ? lastRemoteMac : "NONE");
    } else if (s.equalsIgnoreCase("BONDS?")) {
      listBondedDevices();
    } else if (s.startsWith("GET ")) {
      String name = s.substring(4);
      name.trim();
      if (!name.length()) {
        Serial.println(" Missing song name");
        return;
      }
      Serial.printf("Loading song: %s\n", name.c_str());
      if (!loadSongByName(name)) Serial.println("Failed to load song");
    } else {
      Serial.println("OK");
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nBoot");

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi connect to %s\n", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) Serial.println(" WiFi connect failed");
  else {
    Serial.printf(" WiFi OK, IP=%s RSSI=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    setClock();  // important for TLS
  }

  // BLE init + security
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setSecurityCallbacks(new MySecurity());
  BLESecurity* sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec->setCapability(ESP_IO_CAP_OUT);
  sec->setKeySize(16);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  uint32_t passkey = 123456;
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(passkey));

  // GATT
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());
  BLEService* service = server->createService(SERVICE_UUID);

  cmdChar = service->createCharacteristic(CMD_UUID,
                                          BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  cmdChar->setCallbacks(new CmdCallbacks());

  service->start();

  BLEAdvertising* adv = server->getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  Serial.println("Advertising. Passkey=123456");
  listBondedDevices();
}

void loop() {
  delay(1000);
}
