#define ENABLE_WEBSERVER_AUTHENTICATION 0     // Disable broken MD5 auth in ESPAsyncWebServer

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>     // mDNS support

// Named address:
// http://accesspanel.local/

#define SS_PIN 21
#define RST_PIN 22

const char* ssid = "SM-Yahya";
const char* password = "ya1234ya";
const char* nodeServerUrl = "http://172.28.219.124:5000/register";
const String roomID = "101";

MFRC522 rfid(SS_PIN, RST_PIN);
AsyncWebServer server(80);

String tempUsername = "";
String tempPassword = "";
bool waitingForRFID = false;
unsigned long rfidTimeout = 0;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  WiFi.begin(ssid, password);
  Serial.print("Connecting...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // =============================
  // mDNS – Named Address
  // =============================
  if (MDNS.begin("accesspanel")) {
    Serial.println("mDNS active: http://accesspanel.local/");
  } else {
    Serial.println("mDNS failed");
  }

  // =============================
  // Web registration form
  // =============================
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html =
      "<h2>Register</h2>"
      "<form action='/register' method='POST'>"
      "Username: <input name='user'><br>"
      "Password: <input name='password' type='password'><br>"
      "Room: <input name='roomID' value='" + roomID + "' readonly><br>"
      "<input type='submit'>"
      "</form>";
    request->send(200, "text/html", html);
  });

  // =============================
  // Handle registration POST
  // =============================
  server.on("/register", HTTP_POST, [](AsyncWebServerRequest *request) {
    tempUsername = "";
    tempPassword = "";

    if (request->hasParam("user", true))
      tempUsername = request->getParam("user", true)->value();

    if (request->hasParam("password", true))
      tempPassword = request->getParam("password", true)->value();

    // Start RFID waiting
    waitingForRFID = true;
    rfidTimeout = millis() + 5000UL;

    request->send(200, "text/html", "<h3>Now scan your RFID card within 5 seconds...</h3>");
  });

  server.begin();
}

void tryRegisterRFID(String uidStr) {
  waitingForRFID = false;

  if (tempUsername.isEmpty() || tempPassword.isEmpty())
    return;

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(nodeServerUrl);

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body =
      "user=" + tempUsername +
      "&password=" + tempPassword +
      "&uid=" + uidStr +
      "&roomID=" + roomID;

    int resp = http.POST(body);
    String servResp = http.getString();

    Serial.print("Registration Response: ");
    Serial.println(servResp);

    http.end();
  }

  tempUsername = "";
  tempPassword = "";
}

void loop() {
  // ---------------------------
  // RFID capture window
  // ---------------------------
  if (waitingForRFID && millis() <= rfidTimeout) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {

      String uidStr = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
        uidStr += String(rfid.uid.uidByte[i], HEX);
      }
      uidStr.toUpperCase();

      Serial.print("Registering RFID UID: ");
      Serial.println(uidStr);

      tryRegisterRFID(uidStr);

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
  }

  // ---------------------------
  // Timeout window
  // ---------------------------
  if (waitingForRFID && millis() > rfidTimeout) {
    Serial.println("RFID registration timed out.");
    waitingForRFID = false;

    tempUsername = "";
    tempPassword = "";
  }
}
