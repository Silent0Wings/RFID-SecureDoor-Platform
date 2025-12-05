#include <WiFi.h>
#include <HTTPClient.h>

// Set these to match your actual router (must be 2.4 GHz)
const char* ssid = "REDACTED_SSID";
const char* password = "REDACTED_PASSWORD";

// This laptop should also be connected to the same network as the board
// open serial Monitor to see connection massages
// reset the board & try different port if nothing shows up in the serial monitor .

void setup() {
  Serial.begin(115200); 
  WiFi.begin(ssid, password);

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected, IP address: ");
  Serial.println(WiFi.localIP());

  // Example HTTP GET (change to your real API endpoint as needed)
  HTTPClient http;
  http.begin("http://yourserver.com/preference?name=User1_Bluetooth"); // Replace with your actual endpoint
  int httpCode = http.GET();

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Received:");
    Serial.println(payload);
  } else {
    Serial.println("Error in HTTP request");
  }
  http.end();
}

void loop() {
  // Leave empty, or add main logic here
}
