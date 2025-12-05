#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ===== WIFI + PREF CONFIG =====
static const char* WIFI_SSID = "REDACTED_SSID";
static const char* WIFI_PASS = "REDACTED_PASSWORD";
static const char* PREF_KEY  = "IoT_Jukebox"; // server-side key
static const char* PREF_URL  = "https://iotjukebox.onrender.com/preference";

// ===== URL helpers =====
static inline String urlEncode(const String& in) {
  String out; out.reserve(in.length() * 3 + 1);
  char buf[4];
  for (size_t i = 0; i < in.length(); ++i) {
    unsigned char c = (unsigned char)in[i];
    bool unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    if (unreserved) out += (char)c;
    else if (c == ' ') out += "%20";
    else { sprintf(buf, "%%%02X", c); out += buf; }
  }
  return out;
}
static inline String ensureMacEncoded(const String& mac) {
  if (mac.indexOf("%3A") >= 0) return mac;
  if (mac.indexOf(':') < 0)   return mac;
  String s = mac; s.replace(":", "%3A"); return s;
}

// ===== HTTP GET with retry =====
static bool httpGetWithRetry(const String& url, String& payload, int retries = 3, int backoffMs = 1200, int* statusOut = nullptr) {
  for (int attempt = 1; attempt <= retries; ++attempt) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setConnectTimeout(15000); http.setReuse(false);

    if (!http.begin(client, url)) {
      http.end();
    } else {
      int code = http.GET();
      if (statusOut) *statusOut = code;
      if (code == HTTP_CODE_OK) { payload = http.getString(); http.end(); return true; }
      http.end();
      if (code >= 400 && code < 500) return false;
    }
    delay(backoffMs * attempt);
  }
  return false;
}

// ===== Preference GET: returns song if found =====
static bool getPreferredSongForId(const String& macAddr, String& outSong) {
  const String idQ  = ensureMacEncoded(macAddr);
  const String keyQ = urlEncode(PREF_KEY);
  String url = String(PREF_URL) + "?id=" + idQ + "&key=" + keyQ;

  String payload; int status = 0;
  bool ok = httpGetWithRetry(url, payload, 3, 1200, &status);
  if (!ok) return false;

  // Try to parse JSON and extract a song field. Accept "value", then "name", then "song".
  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  if (doc.containsKey("value") && doc["value"].is<const char*>()) {
    outSong = doc["value"].as<const char*>();
    return outSong.length() > 0;
  }
  if (doc.containsKey("name") && doc["name"].is<const char*>()) {
    outSong = doc["name"].as<const char*>();
    return outSong.length() > 0;
  }
  if (doc.containsKey("song") && doc["song"].is<const char*>()) {
    outSong = doc["song"].as<const char*>();
    return outSong.length() > 0;
  }
  return false;
}

// ===== BLE callback =====
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) override {
    String name = device.haveName() ? String(device.getName().c_str()) : String("(no name)");
    String addr = String(device.getAddress().toString().c_str());

    Serial.printf("Found: %s | RSSI: %d | Addr: %s\n",
                  name.c_str(), device.getRSSI(), addr.c_str());

    if (WiFi.status() == WL_CONNECTED) {
      String song;
      if (getPreferredSongForId(addr, song)) {
        Serial.printf("Preference: %s -> %s\n", addr.c_str(), song.c_str());
      } else {
        Serial.printf("Preference: %s -> none\n", addr.c_str());
      }
    } else {
      Serial.println("WiFi not connected. Skipping preference lookup.");
    }
  }
};

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000UL) { delay(200); }
  Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi connected" : "WiFi connect failed");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  }
}

void setup() {
  Serial.begin(115200);
  connectWiFi();

  BLEDevice::init("");
  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pScan->setActiveScan(true);
  pScan->start(10, false); // scan for 10 seconds (non-blocking repeat handled in loop if desired)
}

void loop() {
  // Optional: restart scan to keep discovering
  static unsigned long last = 0;
  if (millis() - last > 11000UL) {
    BLEDevice::getScan()->start(10, false);
    last = millis();
  }
}
