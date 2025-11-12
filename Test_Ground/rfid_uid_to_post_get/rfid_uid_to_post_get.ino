#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define SS_PIN 21
#define RST_PIN 22

const char* ssid = "SM-Yahya";
const char* password = "ya1234ya";
const char* postUrl = "http://172.28.219.124:5000/post_endpoint"; // POST endpoint
const char* serverBaseUrl = "http://172.28.219.124:5000";         // For GET endpoint
const size_t roomID = 101; 
MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("Ready to scan RFID...");

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi..");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
}

// Send UID as POST request to increment counter
void sendUID(String uidStr) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(postUrl);

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String postData = "uid=" + uidStr;

    int httpResponseCode = http.POST(postData);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("POST Response: " + response);
    } else {
      Serial.println("POST failed: " + String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

// GET user info by UID
void getUserInfo(String uidStr) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // Add ?roomID=roomID as a query parameter
    String getUrl = String(serverBaseUrl) + "/user/" + uidStr + "?roomID=" + String(roomID);
    http.begin(getUrl);

    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("GET Response: " + response);
    } else {
      Serial.println("GET failed: " + String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}


void loop() {
  // Check for new RFID card
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  // Convert UID bytes to uppercase hex string
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  Serial.print("UID Hex: ");
  Serial.println(uidStr);

  // Send UID as POST (increment counter)
  Serial.print("POST data: ");
  Serial.println("uid=" + uidStr);
  sendUID(uidStr);

  // Retrieve user info using GET
  Serial.println("Requesting user info...");
  getUserInfo(uidStr);

  // Halt card and stop encryption
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(3000); // Wait 3 seconds before next read
}
