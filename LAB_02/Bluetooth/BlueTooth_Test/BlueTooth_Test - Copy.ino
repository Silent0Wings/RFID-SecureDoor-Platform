#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include <WiFiClientSecure.h>


// ====== CONFIG ======
#define BUZZER_PIN 21
const char* ssid = "REDACTED_SSID";
const char* password = "REDACTED_PASSWORD";
const char* songNames[] = { "harrypotter", "jigglypuffsong", "tetris", "gameofthrones" };
const char* bNames = "IoT_Jukebox";
#define QUEUE_SIZE 5
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

// ====== BLUETOOTH + CONTROL ======
BluetoothSerial SerialBT;
volatile char btCmd = 0;
bool isPlaying = true;


// ====== QUEUE ======

void initQueue() {
  for (int i = 0; i < QUEUE_SIZE; ++i)
    songQueue[i] = songNames[i % (sizeof(songNames) / sizeof(songNames[0]))];
  queueCount = sizeof(songNames) / sizeof(songNames[0]);
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
bool playSong(SongData* song) {
  for (int i = 0; i < song->noteCount; ++i) {
    int freq = song->frequencies[i];
    int beat = song->durations[i];
    int durationMs = abs(beat) * (60000 / song->tempo) / 4;

    if (freq > 0)
      tone(BUZZER_PIN, freq, durationMs);

    unsigned long start = millis();
    // check Bluetooth input during note delay
    while (millis() - start < durationMs * 1.3) {
      if (SerialBT.available()) {
        btCmd = SerialBT.read();
        Serial.print("Bluetooth command received (while playing): ");
        Serial.println(btCmd);

        if (btCmd == 'P') {  // pause
          Serial.println("Paused by Bluetooth command");
          noTone(BUZZER_PIN);
          handlePlayPause();
          return 'P';  // exit playback early
        }
        if (btCmd == 'B') {
          noTone(BUZZER_PIN);
          handlePreviousSong();
          return 'B';
        }
        if (btCmd == 'N') {  // next or previous
          noTone(BUZZER_PIN);
          handleNextSong();
          return 'N';  // stop current playback
        }
      }
      delay(5);  // small yield to keep loop responsive
    }
    noTone(BUZZER_PIN);
  }
  return 0;
}

bool loadSongByIndex(int idx, SongData* out) {
  String url = String("https://iotjukebox.onrender.com/song?name=") + getCurrentSong();
  Serial.print("\nRequesting: ");
  Serial.println(url);

  // Turn off Bluetooth before HTTPS
  SerialBT.end();
  delay(50);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setReuse(false);

  if (!http.begin(client, url)) {
    Serial.println("Failed to begin HTTPS connection.");
    // Re-enable Bluetooth even on failure
    SerialBT.begin(bNames, true);
    return false;
  }

  int httpCode = http.GET();
  Serial.print("HTTP status: ");
  Serial.println(httpCode);

  String payload = "";
  if (httpCode > 0) {
    payload = http.getString();
  }

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

  // Re-enable Bluetooth
  SerialBT.begin(bNames, true);
  delay(50);

  return (httpCode == HTTP_CODE_OK && out->noteCount > 0);
}



// ====== SETUP ======
void setup() {
  initQueue();

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

  // Bluetooth in SLAVE mode (reduces conflict with Wi-Fi)
  SerialBT.begin(bNames, true);
  Serial.println("Bluetooth started. Connect with your app!");
}




char interupt_Command = 0;

// ====== LOOP ======
void loop() {
  if (SerialBT.available()) {
    if (interupt_Command != 0) {
      btCmd = interupt_Command;
      interupt_Command = 0;
    }
    btCmd = SerialBT.read();
    Serial.print("Bluetooth command received: ");
    Serial.println(btCmd);

    Serial.print(" (ASCII ");
    Serial.print((int)btCmd);
    Serial.println(")");

    if (btCmd == 'P') {
      handlePlayPause();
    } else if (btCmd == 'N') {
      handleNextSong();
    } else if (btCmd == 'B') {
      handlePreviousSong();
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

  delay(500);
}