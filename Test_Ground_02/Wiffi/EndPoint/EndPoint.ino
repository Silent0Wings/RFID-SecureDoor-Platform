#include <WiFi.h>
#include <HTTPClient.h>

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
const char* ssid = "REDACTED_SSID";
const char* password = "REDACTED_PASSWORD";

//---------
// Debug stuf
// -------
// This laptop should also be connected to the same network as the board
// Open serial Monitor to see connection messages
// Reset the board & try different port if nothing shows up in the serial monitor.

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

  for (int i = 0; i < 5; i++) {
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

    http.end();
    delay(1000); // Short pause between requests
  }
}

void loop() {
  // Leave empty, nothing here
}
