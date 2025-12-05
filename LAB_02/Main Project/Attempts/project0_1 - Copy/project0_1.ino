#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include <WiFiClientSecure.h>

// ===== CONFIG =====
#define BUZZER_PIN 21
const char* ssid = "REDACTED_SSID";
const char* password = "Sranklord1";
const char* bNames = "IoT_Jukebox";
static const char* kPrefKey = "40256377";

const char* songNames[] = { "harrypotter", "jigglypuffsong", "tetris", "gameofthrones" };
#define QUEUE_SIZE 5
#define MELODY_MAX 128
#define MAX_MSG_LEN 64

// ===== TYPES =====
struct SongData {
  int frequencies[MELODY_MAX];
  int durations[MELODY_MAX];
  int tempo;
  int noteCount;
};

enum Action {
  ACTION_NONE = 0,
  ACTION_PLAY_TOGGLE,
  ACTION_NEXT,
  ACTION_PREV
};

// ===== GLOBAL STATE =====
BluetoothSerial SerialBT;
String songQueue[QUEUE_SIZE];
int queueStart = 0;
int queueCount = 0;
int queuePos = 0;

SongData currentSong;
int lastLoadedIdx = -1;
bool songLoaded = false;
bool isPlaying = true;

String globalCommand = "";
String deviceIdEnc = "";  // URL-encoded device id

// ===== BEHAVIOR 0: Small helpers =====
String encodeMac(String mac) {
  //mac.replace(":", "%3A");
  return mac;
}
String getCurrentSong() {
  if (queueCount == 0) return "harrypotter";
  return songQueue[(queueStart + queuePos) % queueCount];
}


// ===== BEHAVIOR 1: Wi-Fi connect =====
void connectWiFi(const char* ssid, const char* pass) {
  Serial.print("Connecting to SSID: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ===== BEHAVIOR 2: Bluetooth start/stop =====
void startBluetooth(const char* name) {
  SerialBT.begin(name, true);
  Serial.println("Bluetooth started");
}
void stopBluetooth() {
  SerialBT.end();
}

// ===== BEHAVIOR 3: Queue init =====
void initQueue() {
  const int baseCount = sizeof(songNames) / sizeof(songNames[0]);
  for (int i = 0; i < QUEUE_SIZE; ++i) {
    songQueue[i] = songNames[i % baseCount];
  }
  queueCount = baseCount;
  queueStart = 0;
  queuePos = 0;
}

// ===== BEHAVIOR 4: Single-char control =====
void handlePlayPause() {
  isPlaying = !isPlaying;
  Serial.print("Play state: ");
  Serial.println(isPlaying ? "Playing" : "Paused");
}
void handleNextSong() {
  Serial.printf("Next from %d -> ", queuePos);
  queuePos = (queuePos + 1) % queueCount;
  songLoaded = false;
  Serial.println(queuePos);
  Serial.print("Now: ");
  Serial.println(getCurrentSong());
}
void handlePreviousSong() {
  Serial.printf("Prev from %d -> ", queuePos);
  queuePos = (queuePos - 1 + queueCount) % queueCount;
  songLoaded = false;
  Serial.println(queuePos);
  Serial.print("Now: ");
  Serial.println(getCurrentSong());
}

// ===== BEHAVIOR 5: String command validation/store =====
bool isValidStringCommand(const String& s) {
  if (s.length() <= 1) return false;
  bool hasAlnum = false;
  for (size_t k = 0; k < s.length(); ++k) {
    char c = s[k];
    if (c < 32 || c > 126) return false;  // printable ASCII only
    if (isAlphaNumeric(c)) hasAlnum = true;
  }
  return hasAlnum;  // rejects "||||"
}

// ===== BEHAVIOR 6: Normalize + read one Bluetooth message =====
String normalizeMessage(String s) {
  s.trim();
  int i = 0;
  while (i < s.length() && s[i] == '|') i++;
  int j = s.length() - 1;
  while (j >= i && s[j] == '|') j--;
  s = (i <= j) ? s.substring(i, j + 1) : "";
  s.trim();
  if ((int)s.length() > MAX_MSG_LEN) s = s.substring(0, MAX_MSG_LEN);
  return s;
}
bool readBluetoothMessage(String& out) {
  if (!SerialBT.available()) return false;
  out = "";
  unsigned long t0 = millis();
  while (millis() - t0 < 20) {  // coalesce short burst
    while (SerialBT.available()) {
      char c = (char)SerialBT.read();
      if (c == '\n') {
        out = normalizeMessage(out);
        return out.length() > 0;
      }
      out += c;
      if ((int)out.length() >= MAX_MSG_LEN) {
        out = normalizeMessage(out);
        return true;
      }
    }
    delay(2);
  }
  out = normalizeMessage(out);
  return out.length() > 0;
}

// ===== BEHAVIOR 7: HTTP GET song with retry =====
bool httpGetWithRetry(const String& url, String& payload, int retries = 3, int backoffMs = 1200, int* status = nullptr) {
  for (int attempt = 1; attempt <= retries; ++attempt) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setReuse(false);

    if (!http.begin(client, url)) {
      Serial.println("HTTP begin failed");
      http.end();
    } else {
      int code = http.GET();
      if (status) *status = code;
      Serial.printf("HTTP status: %d (attempt %d/%d)\n", code, attempt, retries);
      if (code == HTTP_CODE_OK) {
        payload = http.getString();
        http.end();
        return true;
      }
      http.end();
      if (code >= 400 && code < 500) return false; // stop retrying on client errors
    }
    delay(backoffMs * attempt);
  }
  return false;
}


// ===== BEHAVIOR 8a: helpers =====
void pushSongToHeadAndScheduleLoad(const String& name) {
  songQueue[queueStart] = name;  // put at head
  queuePos = 0;                  // point to head
  songLoaded = false;            // force reload
  lastLoadedIdx = -1;
  Serial.print("Queued at head: ");
  Serial.println(name);
}

bool extractNameFromJson(const String& payload, String& outName) {
  // Robust parse and diagnostics
  DynamicJsonDocument doc(256);  // tiny is enough here
  DeserializationError err = deserializeJson(doc, payload.c_str(), payload.length());
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }
  JsonVariant v = doc["name"];
  if (v.isNull()) {
    Serial.println("JSON missing 'name'");
    return false;
  }
  const char* n = v.as<const char*>();
  if (!n || !*n) {
    Serial.println("'name' empty");
    return false;
  }
  outName = n;
  return true;
}


// ===== BEHAVIOR 8: Preference API =====

// encoding helpers
static inline String urlEncode(const String& in) {
  String out;
  out.reserve(in.length() * 3 + 1);
  char buf[4];
  for (size_t i = 0; i < in.length(); ++i) {
    unsigned char c = (unsigned char)in[i];
    bool unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    if (unreserved) out += (char)c;
    else if (c == ' ') out += "%20";
    else {
      sprintf(buf, "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

// ensure "08:F9:..." -> "08%3AF9%3A..."
static inline String ensureIdEncoded(const String& in) {
  if (in.indexOf("%3A") >= 0) return in;  // already encoded
  if (in.indexOf(':') < 0) return in;     // nothing to encode
  String s = in;
  s.replace(":", "%3A");
  return s;
}

// POST form with retry + BT coexistence + fallback to POST-with-query
int sendPostPreference(const String& idEnc, const String& key, const String& value, String* outPayload) {
  const String idQ = ensureIdEncoded(idEnc);
  const String keyQ = urlEncode(key);
  const String valQ = urlEncode(value);
  const String url = "https://iotjukebox.onrender.com/preference";
  const String body = "id=" + idQ + "&key=" + keyQ + "&value=" + valQ;

  int code = -1;
  stopBluetooth();
  for (int attempt = 1; attempt <= 4; ++attempt) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setReuse(false);
    Serial.printf("POST form attempt %d/4\n", attempt);
    if (!http.begin(client, url)) {
      Serial.println("HTTP begin failed");
      http.end();
      code = -1;
    } else {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      Serial.println("POST body: " + body);
      code = http.POST(body);
      Serial.printf("POST -> HTTP %d\n", code);
      if (outPayload) *outPayload = (code > 0 ? http.getString() : "");
      http.end();
      if (code > 0) break;
    }
    delay(1200 * attempt);
  }
  startBluetooth(bNames);
  if (code == HTTP_CODE_OK || code == HTTP_CODE_CREATED) return code;

  // Fallback: POST with querystring and empty body
  const String urlQS = "https://iotjukebox.onrender.com/preference?id=" + idQ + "&key=" + keyQ + "&value=" + valQ;
  stopBluetooth();
  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setConnectTimeout(15000);
    http.setReuse(false);
    Serial.println("POST fallback as query: " + urlQS);
    if (!http.begin(client, urlQS)) {
      Serial.println("HTTP begin failed");
      http.end();
    } else {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      int c2 = http.POST("");  // empty body
      Serial.printf("POST(fallback) -> HTTP %d\n", c2);
      if (outPayload) *outPayload = (c2 > 0 ? http.getString() : "");
      http.end();
      code = c2;
    }
  }
  startBluetooth(bNames);
  return code;
}

int sendPostPreferenceJson(const String& idEnc, const String& key, const String& jsonBody, String* outPayload) {
  String url = "https://iotjukebox.onrender.com/preference?id=" + ensureIdEncoded(idEnc) + "&key=" + urlEncode(key);
  Serial.println("POST JSON -> " + url + " body: " + jsonBody);

  stopBluetooth();
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setReuse(false);

  int code = -1;
  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    http.end();
  } else {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(jsonBody);
    Serial.printf("POST(JSON) -> HTTP %d\n", code);
    if (outPayload) *outPayload = (code > 0 ? http.getString() : "");
    http.end();
  }
  startBluetooth(bNames);
  return code;
}

// boolean wrapper for compatibility
// boolean wrapper for compatibility
bool sendPostPreference(const String& idEnc, const String& key, const String& value) {
  String tmp;
  int rc = sendPostPreference(idEnc, key, value, &tmp);
  return (rc == HTTP_CODE_OK || rc == HTTP_CODE_CREATED);
}



// GET with encoding + BT coexistence
bool sendGetPreference(const String& idEnc, const String& key, const String& value, String& payloadOut) {
  const String idQ = ensureIdEncoded(idEnc);
  const String keyQ = urlEncode(key);
  String url = "https://iotjukebox.onrender.com/preference?id=" + idQ + "&key=" + keyQ;
  if (value.length()) url += "&value=" + urlEncode(value);
  Serial.println("GET URL: " + url);

  stopBluetooth();
  int status = 0;
  bool ok = httpGetWithRetry(url, payloadOut, 4, 1200, &status);
  startBluetooth(bNames);

  if (!ok) {
    if (status >= 400 && status < 500) Serial.printf("GET(pref) client error %d\n", status);
    else Serial.println("GET(pref) failed after retries");
  } else {
    Serial.println(payloadOut);
  }
  return ok;
}


// original signature retained
bool sendGetPreference(const String& idEnc, const String& key, const String& value) {
  String _;
  return sendGetPreference(idEnc, key, value, _);
}

// ===== BEHAVIOR 9b: POST-only sync (skip GET) =====
void storeStringCommandPostOnly(const String& key) {
  globalCommand = key;
  Serial.print("Stored command (POST only): ");
  Serial.println(globalCommand);

  String postResp;
  int postCode = sendPostPreference(kPrefKey, globalCommand, getCurrentSong(), &postResp);

  if (postCode == HTTP_CODE_OK || postCode == HTTP_CODE_CREATED) {
    Serial.println("Preference created/updated.");
    return;
  }
  if (postCode == HTTP_CODE_BAD_REQUEST) {
    // server expects JSON {"name":"<current>"}
    String json = String("{\"name\":\"") + getCurrentSong() + "\"}";
    String jsonResp;
    sendPostPreferenceJson(kPrefKey, globalCommand, json, &jsonResp);
    return;
  }
  Serial.printf("POST(pref) failed, HTTP %d\n", postCode);
}


// ===== BEHAVIOR 9: Preference sync (GET first, else POST) =====
void storeStringCommand(const String& s) {
  globalCommand = s;  // key = Bluetooth device name
  Serial.print("Stored command: ");
  Serial.println(globalCommand);

  // 1) Try GET: /preference?id=<student_id>&key=<device_name>
  String getResp;
  if (sendGetPreference(kPrefKey, globalCommand, "", getResp)) {
    String nextName;
    if (extractNameFromJson(getResp, nextName)) {
      // server returned a target song -> set it now
      pushSongToHeadAndScheduleLoad(nextName);
      return;
    }
    Serial.println("GET present but no 'name' field.");
  } else {
    Serial.println("GET failed or not found; will POST.");
  }

  // 2) Not present or invalid -> POST current song
  String postResp;
  int postCode = sendPostPreference(kPrefKey, globalCommand, getCurrentSong(), &postResp);

  if (postCode == HTTP_CODE_OK || postCode == HTTP_CODE_CREATED) {
    Serial.println("Preference created/updated.");
    return;
  }

  if (postCode == HTTP_CODE_BAD_REQUEST) {
    // server expects JSON body
    String json = String("{\"name\":\"") + getCurrentSong() + "\"}";
    String jsonResp;
    sendPostPreferenceJson(kPrefKey, globalCommand, json, &jsonResp);
    return;
  }

  Serial.printf("POST(pref) failed, HTTP %d\n", postCode);
}

// ===== BEHAVIOR 10: Classify and map message to action =====
Action processMessage(const String& raw) {
  String msg = normalizeMessage(raw);
  const int len = msg.length();
  if (len == 0) return ACTION_NONE;

  if (len == 1) {
    const char c = msg[0];
    Serial.printf("Single-char command: %c\n", c);
    if (c == 'P') return ACTION_PLAY_TOGGLE;
    if (c == 'N') return ACTION_NEXT;
    if (c == 'B') return ACTION_PREV;
    Serial.println("Unknown single-char command");
    return ACTION_NONE;
  }

  // NEW: commands like "_myDeviceKey" -> POST current song, skip GET
  if (msg[0] == '_' && len > 1) {
    String key = msg.substring(1);
    storeStringCommandPostOnly(key);
    return ACTION_NONE;
  }

  Serial.print("String command: ");
  Serial.println(msg);
  if (msg.startsWith("CMD:") || isValidStringCommand(msg)) {
    // existing behavior (GET first, then POST fallback)
    storeStringCommand(msg);
  } else {
    Serial.println("Invalid string format");
  }
  return ACTION_NONE;
}


// ===== BEHAVIOR 11: Parse song JSON =====
bool parseMelodyFromJson(const String& jsonPayload, SongData& out) {
  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, jsonPayload);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    out.noteCount = 0;
    return false;
  }
  out.tempo = doc["tempo"] | 120;
  JsonArray m = doc["melody"].as<JsonArray>();
  int count = 0;
  for (size_t i = 0; i + 1 < m.size() && count < MELODY_MAX; i += 2) {
    out.frequencies[count] = m[i].as<int>();
    out.durations[count] = m[i + 1].as<int>();
    ++count;
  }
  out.noteCount = count;
  return count > 0;
}

// ===== BEHAVIOR 12: Load song over HTTPS =====
bool loadSongByIndex(int idx, SongData& out) {
  const String name = getCurrentSong();
  if (name.length() == 0) return false;

  const String url = String("https://iotjukebox.onrender.com/song?name=") + name;
  Serial.print("GET ");
  Serial.println(url);

  stopBluetooth();
  String payload;
  bool ok = httpGetWithRetry(url, payload, 4, 1200);
  startBluetooth(bNames);

  if (!ok) {
    Serial.println("HTTP failed after retries");
    return false;
  }
  if (!parseMelodyFromJson(payload, out)) {
    Serial.println("Song JSON invalid");
    return false;
  }
  return true;
}

// ===== BEHAVIOR 13: Play song, observe controls =====
Action playSong(SongData& s) {
  if (s.noteCount <= 0 || s.tempo <= 0) return ACTION_NONE;

  for (int i = 0; i < s.noteCount; ++i) {
    const int freq = s.frequencies[i];
    const int beat = s.durations[i];
    const int durationMs = abs(beat) * (60000 / s.tempo) / 4;

    if (freq > 0) tone(BUZZER_PIN, freq, durationMs);

    const unsigned long start = millis();
    while (millis() - start < (unsigned long)(durationMs * 1.3)) {
      String incoming;
      if (readBluetoothMessage(incoming)) {
        Action a = processMessage(incoming);
        if (a == ACTION_PLAY_TOGGLE || a == ACTION_NEXT || a == ACTION_PREV) {
          noTone(BUZZER_PIN);
          return a;
        }
      }
      delay(5);
    }
    noTone(BUZZER_PIN);
  }
  return ACTION_NONE;
}

// ===== ARDUINO ENTRY =====
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);

  initQueue();
  connectWiFi(ssid, password);
  startBluetooth(bNames);

  // Compute and cache encoded device id
  deviceIdEnc = encodeMac(WiFi.macAddress());



  Serial.println("Ready");
}

void loop() {
  // Handle inbound Bluetooth
  {
    String msg;
    if (readBluetoothMessage(msg)) {
      Action a = processMessage(msg);
      if (a == ACTION_PLAY_TOGGLE) handlePlayPause();
      else if (a == ACTION_NEXT) handleNextSong();
      else if (a == ACTION_PREV) handlePreviousSong();
    }
  }

  // Load and play as needed
  if (isPlaying && WiFi.status() == WL_CONNECTED) {
    if (!songLoaded || lastLoadedIdx != queuePos) {
      Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
      songLoaded = loadSongByIndex(queuePos, currentSong);
      lastLoadedIdx = songLoaded ? queuePos : -1;
    }
    if (songLoaded) {
      Action a = playSong(currentSong);
      if (a == ACTION_PLAY_TOGGLE) handlePlayPause();
      else if (a == ACTION_NEXT) handleNextSong();
      else if (a == ACTION_PREV) handlePreviousSong();
    }
  }

  delay(200);
}
