#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// which pen will play 
#define BUZZER_PIN 21
//---------
// End Point
// -------
// List of songs :
// https://iotjukebox.onrender.com/song
// https://iotjukebox.onrender.com/song?name=harrypotter
// "harrypotter", "jigglypuffsong", "tetris", "gameofthrones"
// List of song names to test
const char* songNames[] = {"harrypotter", "jigglypuffsong", "tetris", "gameofthrones"};

//---------
// login stuff
// -------
// Set these to match your actual router (must be 2.4 GHz)
const char* ssid = "SM-Yahya";
const char* password = "ya1234ya";

//---------
// Debug stuf
// -------
// This laptop should also be connected to the same network as the board
// Open serial Monitor to see connection messages
// Reset the board & try different port if nothing shows up in the serial monitor.
// arduinojson install it from sketch -> included library -> manage library -> install

// for structuring the song data
#define MELODY_MAX 128
typedef struct {
  int frequencies[MELODY_MAX];
  int durations[MELODY_MAX];
  int tempo;
  int noteCount;
} SongData;

// parses json in this format 
/*
{
  "name": "songtitle",
  "tempo": "tempo",
  "melody": [
    "frequency0", "duration0",
    "frequency1", "duration1",
    *************************
    *************************
    *************************
    *************************
    "frequencyN", "durationN"
  ]
}
*/
bool parseMelodyFromJson(const String& jsonPayload, SongData *song) {
  StaticJsonDocument<2048> doc;
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
    song->durations[noteCount]   = melodyArr[i + 1].as<int>();
    ++noteCount;
  }
  song->noteCount = noteCount;
  return true;
}

// plays the song inside songdata
void playSong(SongData* song) {
  for (int i = 0; i < song->noteCount; ++i) {
    int freq = song->frequencies[i];
    int beat = song->durations[i];
    int durationMs = abs(beat) * (60000 / song->tempo) / 4; // 1/4 note formula
    if (freq > 0) {
      tone(BUZZER_PIN, freq, durationMs);
    }
    delay(durationMs * 1.3);  // Space between notes
    noTone(BUZZER_PIN);
  }
}


// helps debug different html codes responses
void handleHttpStatus(int httpCode, const String& payload, HTTPClient& http) {
  switch (httpCode) {
    case 200:
      Serial.println("Success! Song data received:");
      Serial.println(payload);
      break;
    case 404:
      Serial.println("Error: Song not found (404)");
      break;
    case 500:
      Serial.println("Server error (500). Try again later.");
      break;
    default:
      Serial.print("Unexpected HTTP status code: ");
      Serial.println(httpCode);
      Serial.println(http.errorToString(httpCode).c_str());
      break;
  }
}

void setup() {
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

  const int songsize = sizeof(songNames) / sizeof(songNames[0]);
  for (int i = 0; i < songsize; i++) {
    //https://iotjukebox.onrender.com/song?name=harrypotter
    String url = "https://iotjukebox.onrender.com/song?name=" + String(songNames[i]);
    Serial.print("\nRequesting: ");
    Serial.println(url);

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    Serial.print("HTTP status: ");
    Serial.println(httpCode);

    String payload = "";
    if (httpCode > 0) {
      payload = http.getString();
    }

    handleHttpStatus(httpCode, payload, http);
    SongData song;
    if (parseMelodyFromJson(payload, &song)) {
      // song.tempo, song.noteCount, song.frequencies[], song.durations[] are now ready to use!
      playSong(&song);
    }
    http.end();
    delay(1000); // Short pause between requests
  }
}

void loop() {
  // Leave empty, nothing here
}
