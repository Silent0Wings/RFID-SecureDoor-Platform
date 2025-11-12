// ===== IoT Jukebox — Classic BT + BLE Auth/Cmd + Preference GET with POST fallback =====

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include <WiFiClientSecure.h>

// ---- BLE (GATT) ----
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_gatts_api.h"

// ====== CONFIG ======
#define BUZZER_PIN 21
const char* ssid = "Chimera";
const char* password = "Sranklord1";

// API base
static const char* API_BASE = "https://iotjukebox.onrender.com";

// BLE device name
const char* bNames = "IoT_Jukebox";

// Queue
#define QUEUE_SIZE 5
const char* songNames[] = { "harrypotter", "jigglypuffsong", "tetris", "gameofthrones" };
String songQueue[QUEUE_SIZE];
int queueStart = 0;  // points to current song
int queueCount = 0;  // number of valid songs in queue
int queuePos = 0;    // logical position in queue

// ====== SONG STRUCT ======
#define MELODY_MAX 128
typedef struct {
  int frequencies[MELODY_MAX];
  int durations[MELODY_MAX];
  int tempo;
  int noteCount;
} SongData;

// ====== GLOBAL STATE ======
SongData currentSong;
int lastLoadedIdx = -1;
bool songLoaded = false;

// Classic Bluetooth SPP
BluetoothSerial SerialBT;
volatile char btCmd = 0;
bool isPlaying = true;

// ====== BLE AUTH/CMD STATE ======
String g_userId = "";              // set by AUTH characteristic write
String g_prefKey = "IoT_Jukebox";  // default key
volatile char g_bleCmd = 0;        // set by CMD characteristic write

// ====== BLE UUIDs ======
static const char* AUTH_SVC_UUID = "7e6a1000-0000-0000-0000-000000000001";
static const char* AUTH_ID_UUID = "7e6a1001-0000-0000-0000-000000000001";
static const char* CMD_SVC_UUID = "7e6a3000-0000-0000-0000-000000000001";
static const char* CMD_UUID = "7e6a3001-0000-0000-0000-000000000001";

// ====== UTILS ======
// **URL-encode only ':' in MAC-like IDs**
String encodeIdForUrl(String id) {
  id.replace(":", "%3A");
  return id;
}

// **pick random song from local queue pool**
String pickRandomSong() {
  size_t n = sizeof(songNames) / sizeof(songNames[0]);
  if (n == 0) return "harrypotter";
  return String(songNames[random(n)]);
}

// **select queue position by song name (first match)**
bool selectSongByName(const String& name) {
  if (queueCount <= 0) return false;
  for (int i = 0; i < queueCount; ++i) {
    if (songQueue[i].equalsIgnoreCase(name)) {
      queuePos = i;
      songLoaded = false;
      isPlaying = true;
      Serial.printf("Selected by name: %s (pos=%d)\n", name.c_str(), queuePos);
      return true;
    }
  }
  // not found: replace current slot to keep structure unchanged
  songQueue[queuePos] = name;
  songLoaded = false;
  isPlaying = true;
  Serial.printf("Name not in queue, replaced current slot with: %s\n", name.c_str());
  return true;
}

// ====== QUEUE ======
void initQueue() {
  for (int i = 0; i < QUEUE_SIZE; ++i)
    songQueue[i] = songNames[i % (sizeof(songNames) / sizeof(songNames[0]))];
  queueCount = QUEUE_SIZE;  // keep your original behavior
  queueStart = 0;
  queuePos = 0;
}

String getCurrentSong() {
  return songQueue[(queueStart + queuePos) % queueCount];
}

// ====== PARSER ======
bool parseMelodyFromJson(const String& jsonPayload, SongData* song) {
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, jsonPayload);
  if (error) {
    Serial.print("JSON deserialization failed: ");
    Serial.println(error.c_str());
    song->noteCount = 0;
    return false;
  }
  song->tempo = doc["tempo"].as<int>();
  JsonArray melodyArr = doc["melody"].as<JsonArray>();
  int noteCount = 0;
  for (size_t i = 0; i + 1 < melodyArr.size() && noteCount < MELODY_MAX; i += 2) {
    song->frequencies[noteCount] = melodyArr[i].as<int>();
    song->durations[noteCount] = melodyArr[i + 1].as<int>();
    ++noteCount;
  }
  song->noteCount = noteCount;
  return true;
}

// ====== button behaviors ======
void handlePlayPause() {
  isPlaying = !isPlaying;
  Serial.print("Play state: ");
  Serial.println(isPlaying ? "Playing" : "Paused");
}
void handleNextSong() {
  Serial.print("Current Song ID: ");
  Serial.println(queuePos);
  queuePos = (queuePos + 1) % queueCount;
  songLoaded = false;
  Serial.print("Next Song ID: ");
  Serial.println(queuePos);
  Serial.print("Next song selected: ");
  Serial.println(getCurrentSong());
}
void handlePreviousSong() {
  Serial.print("Current Song ID: ");
  Serial.println(queuePos);
  queuePos = (queuePos - 1 + queueCount) % queueCount;
  songLoaded = false;
  Serial.print("Previous Song ID: ");
  Serial.println(queuePos);
  Serial.print("Previous song selected: ");
  Serial.println(getCurrentSong());
}

// ====== PLAYER ======
char interupt_Command = 0;

bool playSong(SongData* song) {
  for (int i = 0; i < song->noteCount; ++i) {
    // merge BLE command into SPP path
    if (g_bleCmd != 0) {
      btCmd = g_bleCmd;
      g_bleCmd = 0;
    }

    int freq = song->frequencies[i];
    int beat = song->durations[i];
    int durationMs = abs(beat) * (60000 / song->tempo) / 4;

    if (freq > 0) tone(BUZZER_PIN, freq, durationMs);

    unsigned long start = millis();
    while (millis() - start < (unsigned long)(durationMs * 1.3)) {
      if (SerialBT.available() || btCmd != 0) {
        if (btCmd == 0) btCmd = SerialBT.read();
        Serial.print("Bluetooth command received (while playing): ");
        Serial.println(btCmd);

        if (btCmd == 'P') {
          noTone(BUZZER_PIN);
          handlePlayPause();
          btCmd = 0;
          return 'P';
        }
        if (btCmd == 'B') {
          noTone(BUZZER_PIN);
          handlePreviousSong();
          btCmd = 0;
          return 'B';
        }
        if (btCmd == 'N') {
          noTone(BUZZER_PIN);
          handleNextSong();
          btCmd = 0;
          return 'N';
        }
        btCmd = 0;
      }
      delay(5);
    }
    noTone(BUZZER_PIN);
  }
  return 0;
}

// ====== HTTPS HELPERS (secure, no cert pin, short timeouts) ======
bool httpsGET(const String& url, int& code, String& body) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setReuse(false);
  Serial.printf("GET %s\n", url.c_str());
  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return false;
  }
  code = http.GET();
  if (code > 0) body = http.getString();
  http.end();
  return true;
}

bool httpsPOSTForm(const String& url, const String& formBody, int& code, String& body) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setReuse(false);
  Serial.printf("POST %s\n", url.c_str());
  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  code = http.POST(formBody);
  if (code > 0) body = http.getString();
  http.end();
  return true;
}

// ====== PREFERENCE: GET with POST fallback ======
// **returns true if a valid song was resolved into outSong**
bool getPreferenceSong(const String& id, const String& key, String& outSong) {
  outSong = "";
  String url = String(API_BASE) + "/preference?id=" + encodeIdForUrl(id) + "&key=" + key;
  int code = -1;
  String body;
  if (!httpsGET(url, code, body)) return false;
  Serial.printf("Preference GET -> %d\n", code);
  if (code != 200) return false;

  DynamicJsonDocument doc(512);
  auto err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("Pref JSON parse err: %s\n", err.c_str());
    return false;
  }

  if (doc["name"].is<const char*>()) {
    String name = String(doc["name"].as<const char*>());
    if (name.length() > 0) {
      outSong = name;
      return true;
    }
  }
  return false;
}

// **fallback: pick random from queue and POST, then GET again**
bool resolvePreferenceWithFallback(const String& id, const String& key, String& outSong) {
  if (getPreferenceSong(id, key, outSong)) return true;

  // invalid -> POST random choice
  String chosen = pickRandomSong();
  String postUrl = String(API_BASE) + "/preference";
  String body = "id=" + encodeIdForUrl(id) + "&key=" + key + "&value=" + chosen;
  int pcode = -1;
  String presp;
  if (!httpsPOSTForm(postUrl, body, pcode, presp)) {
    Serial.println("POST failed");
    return false;
  }
  Serial.printf("Preference POST -> %d\n", pcode);
  if (pcode < 200 || pcode >= 400) return false;

  // confirm via GET
  return getPreferenceSong(id, key, outSong);
}

// ====== SONG LOADER ======
bool loadSongByIndex(int idx, SongData* out) {
  String url = String("https://iotjukebox.onrender.com/song?name=") + getCurrentSong();
  Serial.print("\nRequesting: ");
  Serial.println(url);

  // Turn off Classic BT before HTTPS (reduce RF conflicts)
  SerialBT.end();
  delay(50);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setReuse(false);

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTPS connection.");
    SerialBT.begin(bNames, true);
    return false;
  }

  int httpCode = http.GET();
  Serial.print("HTTP status: ");
  Serial.println(httpCode);

  String payload = "";
  if (httpCode > 0) payload = http.getString();

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("Success! Song data received:");
    if (!parseMelodyFromJson(payload, out))
      Serial.println("Failed to parse song JSON.");
  } else {
    Serial.print("Unexpected HTTP status code: ");
    Serial.println(httpCode);
    Serial.print("Error message: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();

  // Re-enable Classic BT
  SerialBT.begin(bNames, true);
  delay(50);

  return (httpCode == HTTP_CODE_OK && out->noteCount > 0);
}

// ====== BLE CALLBACKS ======
class ConnCB : public BLEServerCallbacks {
  void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* p) override {
    const uint8_t* a = p->connect.remote_bda;
    char mac[18];
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
    Serial.printf("BLE connected: %s\n", mac);
  }
  void onDisconnect(BLEServer* s) override {
    Serial.println("BLE disconnected. Advertising...");
    s->getAdvertising()->start();
  }
};

class AuthCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String s = String(c->getValue().c_str());
    g_userId = s;
    Serial.printf("AUTH userId set: '%s'\n", g_userId.c_str());

    // **On ID write, resolve preference now if WiFi is ready**
    if (WiFi.status() == WL_CONNECTED && g_userId.length() > 0) {
      String prefSong;
      if (resolvePreferenceWithFallback(g_userId, g_prefKey, prefSong)) {
        Serial.printf("Resolved preference song: %s\n", prefSong.c_str());
        selectSongByName(prefSong);
      } else {
        Serial.println("Preference resolve failed");
      }
    }
  }
};

class CmdCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String s = String(c->getValue().c_str());
    if (s.length() == 0) return;
    g_bleCmd = s[0];
  }
};

// ====== BLE SETUP ======
void setupBLE() {
  BLEDevice::init("TTGO_Jukebox");
  BLEServer* srv = BLEDevice::createServer();
  srv->setCallbacks(new ConnCB());

  // AUTH service
  BLEService* auth = srv->createService(AUTH_SVC_UUID);
  BLECharacteristic* authId = auth->createCharacteristic(
    AUTH_ID_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  authId->addDescriptor(new BLE2902());
  authId->setCallbacks(new AuthCB());
  authId->setValue("WRITE_USER_ID");
  auth->start();

  // CMD service
  BLEService* cmd = srv->createService(CMD_SVC_UUID);
  BLECharacteristic* cmdChar = cmd->createCharacteristic(
    CMD_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
  cmdChar->addDescriptor(new BLE2902());
  cmdChar->setCallbacks(new CmdCB());
  cmdChar->setValue("P|N|B");
  cmd->start();

  srv->getAdvertising()->start();
  Serial.println("BLE advertising");
}

// ====== SETUP ======
void setup() {
  initQueue();
  randomSeed((uint32_t)esp_timer_get_time());

  Serial.begin(115200);
  Serial.println("Starting...");

  Serial.print("Connecting to SSID: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Classic Bluetooth SPP in SLAVE mode
  SerialBT.begin(bNames, true);
  Serial.println("Classic Bluetooth started. Connect with your app.");

  // **Start BLE services for AUTH and CMD**
  setupBLE();
}

// ====== LOOP ======
void loop() {
  // **Consume pending BLE command in main loop as well**
  if (g_bleCmd != 0) {
    btCmd = g_bleCmd;
    g_bleCmd = 0;
    Serial.print("BLE->Main cmd: ");
    Serial.println(btCmd);
  }

  if (SerialBT.available() || btCmd != 0) {
    if (interupt_Command != 0) {
      btCmd = interupt_Command;
      interupt_Command = 0;
    }
    if (btCmd == 0) btCmd = SerialBT.read();
    Serial.print("Bluetooth command received: ");
    Serial.println(btCmd);
    Serial.print(" (ASCII ");
    Serial.print((int)btCmd);
    Serial.println(")");

    if (btCmd == 'P') handlePlayPause();
    else if (btCmd == 'N') handleNextSong();
    else if (btCmd == 'B') handlePreviousSong();

    btCmd = 0;
  }

  // **If authenticated and WiFi is up but no preference applied yet, try once**
  static bool triedPrefOnce = false;
  if (!triedPrefOnce && WiFi.status() == WL_CONNECTED && g_userId.length() > 0) {
    triedPrefOnce = true;
    String prefSong;
    if (resolvePreferenceWithFallback(g_userId, g_prefKey, prefSong)) {
      Serial.printf("Applied preference at loop: %s\n", prefSong.c_str());
      selectSongByName(prefSong);
    } else {
      Serial.println("Preference resolve (loop) failed");
    }
  }

  if (isPlaying && WiFi.status() == WL_CONNECTED) {
    if (!songLoaded || lastLoadedIdx != queuePos) {
      Serial.print("Free heap: ");
      Serial.println(ESP.getFreeHeap());
      songLoaded = loadSongByIndex(queuePos, &currentSong);
      lastLoadedIdx = songLoaded ? queuePos : -1;
    }
    if (songLoaded) {
      interupt_Command = playSong(&currentSong);
    }
    delay(200);
  }

  delay(20);
}
