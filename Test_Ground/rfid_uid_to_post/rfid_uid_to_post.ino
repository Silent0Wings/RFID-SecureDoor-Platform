#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define SS_PIN 21
#define RST_PIN 22

const char* ssid = "SM-Yahya";
const char* password = "ya1234ya";
const char* serverUrl = "http://172.28.219.124:5000/post_endpoint";  // Set to your actual server endpoint

/*
  I will perform a web search to see if there is a concise summary or
  explanation available for a code snippet similar to the ESP32 + MFRC522 RFID
  reader code that sends scanned UID to a server. This might help provide a clear and compact summary for the entire code.
  this communicate with :
  C:\Users\ypers\OneDrive\Documents\SOEN422\Server\rfid-server
*/

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

// Send UID as POST request
void sendUID(String uidStr) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);

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

  // Prepare POST data string & send
  Serial.print("POST data: ");
  Serial.println("uid=" + uidStr);
  sendUID(uidStr);

  // Halt card and stop encryption
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(3000);  // Wait 3 seconds before next read
}
