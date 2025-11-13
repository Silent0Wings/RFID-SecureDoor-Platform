#define ENABLE_WEBSERVER_AUTHENTICATION 0  // Disable MD5 auth in ESPAsyncWebServer

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

// =============================
// RFID pins
// =============================
#define SS_PIN 21
#define RST_PIN 22

// =============================
// WiFi and backend
// =============================
const char* ssid = "SM-Yahya";
const char* password = "ya1234ya";

const char* registerUrl = "http://172.28.219.124:5000/register";  // POST user/pass/uid/roomID
const char* accessBaseUrl  = "http://172.28.219.124:5000/user"  ;  // GET uid/roomID -> ALLOW/DENY
const char* statsUrl       = "http://172.28.219.124:5000/stats";      // GET user/password -> table text

const String roomID = "101";

// =============================
// RFID and server objects
// =============================
MFRC522 rfid(SS_PIN, RST_PIN);
AsyncWebServer server(80);

// =============================
// State
// =============================
String tempUsername = "";
String tempPassword = "";

bool waitingForRFID = false;
unsigned long rfidTimeout = 0;

String lastUid = "";
String lastStatus = "";  // "REGISTERED", "GRANTED", "DENIED", "TIMEOUT", etc.
unsigned long lastEventMillis = 0;

// =============================
// Access control pins (optional)
// =============================
// Example: use GPIO 12 as "door relay"
const int DOOR_RELAY_PIN = 12;

// =============================
// Helpers
// =============================
String uidToString() {
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  return uidStr;
}

void openDoor() {
  digitalWrite(DOOR_RELAY_PIN, HIGH);
  delay(1000);
  digitalWrite(DOOR_RELAY_PIN, LOW);
}

// =============================
// Backend calls
// =============================
void tryRegisterRFID(const String& uidStr) {
  waitingForRFID = false;

  if (tempUsername.isEmpty() || tempPassword.isEmpty()) {
    lastStatus = "REGISTER_FAILED_EMPTY_USER";
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(registerUrl);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body =
      "user=" + tempUsername + "&password=" + tempPassword + "&uid=" + uidStr + "&roomID=" + roomID;

    int resp = http.POST(body);
    String servResp = http.getString();

    Serial.print("Registration Response: ");
    Serial.println(servResp);

    if (resp == 200) {
      lastStatus = "REGISTERED";
    } else {
      lastStatus = "REGISTER_ERROR_" + String(resp);
    }

    http.end();
  } else {
    lastStatus = "REGISTER_FAILED_NO_WIFI";
  }

  tempUsername = "";
  tempPassword = "";
}

void checkAccessRFID(const String& uidStr) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Access check failed: no WiFi");
    lastStatus = "DENIED_NO_WIFI";
    return;
  }

  HTTPClient http;
  // GET /user/<uid>?roomID=101
  String url = String(accessBaseUrl) + "/" + uidStr + "?roomID=" + roomID;
  Serial.print("Access URL: ");
  Serial.println(url);

  http.begin(url);
  int resp = http.GET();
  String body = (resp > 0) ? http.getString() : "";
  http.end();

  Serial.print("Access HTTP: ");
  Serial.println(resp);
  Serial.print("Access body: ");
  Serial.println(body);

  if (resp == 200) {
    lastStatus = "GRANTED";
    openDoor();
  } else {
    lastStatus = "DENIED_" + String(resp);
  }
}


// =============================
// Setup
// =============================
void setup() {
  Serial.begin(115200);

  pinMode(DOOR_RELAY_PIN, OUTPUT);
  digitalWrite(DOOR_RELAY_PIN, LOW);

  SPI.begin();
  rfid.PCD_Init();

  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("accesspanel")) {
    Serial.println("mDNS active: http://accesspanel.local/");
  } else {
    Serial.println("mDNS failed");
  }

  // =============================
  // Root page: simple menu
  // =============================
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    String html;
    html = "<h2>Access Panel</h2>";
    html += "<p><a href='/register'>Register card</a></p>";
    html += "<p><a href='/status'>RFID status</a></p>";
    html += "<p><a href='/login'>Admin login</a></p>";
    request->send(200, "text/html", html);
  });

  // =============================
  // Registration form page (GET)
  // =============================
  server.on("/register", HTTP_GET, [](AsyncWebServerRequest* request) {
    String html;
    html = "<h2>Register</h2>"
           "<form action='/register' method='POST'>"
           "Username: <input name='user'><br>"
           "Password: <input name='password' type='password'><br>"
           "Room: <input name='roomID' value='"
           + roomID + "' readonly><br>"
                      "<input type='submit' value='Start registration'>"
                      "</form>";
    request->send(200, "text/html", html);
  });

  // =============================
  // Handle registration POST
  // =============================
  server.on("/register", HTTP_POST, [](AsyncWebServerRequest* request) {
    tempUsername = "";
    tempPassword = "";

    if (request->hasParam("user", true))
      tempUsername = request->getParam("user", true)->value();
    if (request->hasParam("password", true))
      tempPassword = request->getParam("password", true)->value();

    waitingForRFID = true;
    rfidTimeout = millis() + 5000UL;
    lastUid = "";
    lastStatus = "AWAITING_TAG";

    // Redirect to status page showing UID and timer
    request->redirect("/status");
  });

  // =============================
  // Status page: UID + timer + last status
  // =============================
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    unsigned long now = millis();
    long remaining = waitingForRFID ? (long)(rfidTimeout - now) : 0;
    if (remaining < 0) remaining = 0;
    uint32_t remainingSec = remaining / 1000;

    String html;
    html = "<h2>RFID Status</h2>";
    html += "<p>Mode: " + String(waitingForRFID ? "REGISTRATION" : "ACCESS") + "</p>";
    html += "<p>Last UID: " + (lastUid.isEmpty() ? String("NONE") : lastUid) + "</p>";
    html += "<p>Last status: " + (lastStatus.isEmpty() ? String("NONE") : lastStatus) + "</p>";
    if (waitingForRFID) {
      html += "<p>Time left: " + String(remainingSec) + " s</p>";
    }
    html += "<p><a href='/'>Back</a></p>";
    // Auto refresh every second to see countdown and new UID
    html += "<meta http-equiv='refresh' content='1'>";
    request->send(200, "text/html", html);
  });

  // =============================
  // Login page + stats fetch
  // GET /login  -> show form or results
  // =============================
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("user") || !request->hasParam("password")) {
      // Show login form
      String html;
      html = "<h2>Admin login</h2>"
             "<form action='/login' method='GET'>"
             "User: <input name='user'><br>"
             "Password: <input name='password' type='password'><br>"
             "<input type='submit' value='Fetch stats'>"
             "</form>"
             "<p><a href='/'>Back</a></p>";
      request->send(200, "text/html", html);
      return;
    }

    String user = request->getParam("user")->value();
    String pass = request->getParam("password")->value();

    String backendResp = "No response";
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      // Example: /stats?user=...&password=...
      String url = String(statsUrl) + "?user=" + user + "&password=" + pass;
      http.begin(url);
      int code = http.GET();
      if (code > 0) {
        backendResp = http.getString();
      } else {
        backendResp = "Error HTTP " + String(code);
      }
      http.end();
    } else {
      backendResp = "No WiFi";
    }

    // Show raw data (your backend should return lines with: user password uid counter roomID access)
    String html;
    html = "<h2>Access data</h2>";
    html += "<pre>" + backendResp + "</pre>";
    html += "<p><a href='/login'>Back to login</a></p>";
    html += "<p><a href='/'>Home</a></p>";
    request->send(200, "text/html", html);
  });

  server.begin();
}

// =============================
// Main loop: always read RFID
// =============================
void loop() {
  // ---------------------------
  // Timeout window
  // ---------------------------
  if (waitingForRFID && millis() > rfidTimeout) {
    Serial.println("RFID registration timed out.");
    waitingForRFID = false;
    tempUsername = "";
    tempPassword = "";
    lastStatus = "TIMEOUT";
  }

  // ---------------------------
  // RFID polling
  // ---------------------------
  if (!rfid.PICC_IsNewCardPresent()) {
    return;  // nothing detected this iteration
  }

  if (!rfid.PICC_ReadCardSerial()) {
    Serial.println("RFID: card present but read failed");
    return;
  }

  // Build UID string using helper
  String uidStr = uidToString();
  lastUid = uidStr;
  lastEventMillis = millis();

  Serial.print("Card detected UID: ");
  Serial.println(uidStr);

  if (waitingForRFID && millis() <= rfidTimeout) {
    Serial.println("RFID in REGISTRATION window, sending to backend...");
    tryRegisterRFID(uidStr);
  } else {
    Serial.println("RFID in ACCESS mode, checking permissions...");
    checkAccessRFID(uidStr);
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}
