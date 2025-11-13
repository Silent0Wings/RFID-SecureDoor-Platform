#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define SS_PIN 21
#define RST_PIN 22

const char* ssid = "SM-Yahya";
const char* password = "ya1234ya";



const char* postUrl = "http://172.28.219.124:5000/post_endpoint";
const char* serverBaseUrl = "http://172.28.219.124:5000";
const String roomID = "101"; // Room this device controls (now a String!)

/*
  ESP32 RFID reader program

  - Connects to specified Wi-Fi network
  - Reads RFID card UIDs using MFRC522
  - Sends UID to a server via HTTP POST
  - Requests user info from server via HTTP GET with UID and room ID
  - Prints responses to serial monitor
  - Suitable for access control or user tracking in a specified room
  - Waits 3 seconds between reads

  this communicates with :
  C:\Users\ypers\OneDrive\Documents\SOEN422\Server\rfid-server-DBS
*/




MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("Ready to scan RFID...");

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi..");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
}

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

void getUserInfo(String uidStr, String roomID) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String getUrl = String(serverBaseUrl) + "/user/" + uidStr + "?roomID=" + roomID;
    Serial.print("Requesting GET for UID ");
    Serial.print(uidStr);
    Serial.print(" and roomID ");
    Serial.println(roomID);

    http.begin(getUrl);

    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("GET Response: ");
      Serial.println(response);
    } else {
      Serial.print("GET failed: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  Serial.print("UID Hex: ");
  Serial.println(uidStr);

  Serial.print("POST data: ");
  Serial.println("uid=" + uidStr);
  sendUID(uidStr);

  Serial.println("Requesting user info...");
  getUserInfo(uidStr, roomID); // roomID is a String already

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(3000);
}
