/*
  ESP32 RFID access controller with web UI and HTTP backend.

  Purpose:
  - Runs on an ESP32 with an MFRC522 RFID reader.
  - Talks to a Node.js backend that stores users in an Excel file.
  - Exposes simple web pages for home, registration, login, and status.

  Backend endpoints used:
  - registerUrl  (POST /register)    : Register a new card with user, password, uid, roomID.
  - accessBaseUrl (GET /user/:uid)   : Check if a given UID has access to a specific room.
  - statsUrl     (GET /stats)        : Fetch a single user's data by username/password.

  RFID flow:
  - loop() polls the MFRC522; readCardUid() builds the UID string.
  - handleCardUid() routes the UID:
      - If a registration window is active (waitingForRFID + rfidTimeout), calls tryRegisterRFID().
      - Otherwise, calls checkAccessRFID() to validate access.
  - handleRegistrationTimeout() closes the registration window and updates lastStatus on timeout.

  HTTP calls:
  - tryRegisterRFID():
      - Uses tempUsername/tempPassword and the scanned UID.
      - Sends a JSON POST to /register with user, password, uid, roomID.
      - Updates lastStatus and lastStats* fields based on HTTP result.
  - checkAccessRFID():
      - Calls /user/:uid?roomID=... on the backend.
      - Parses the flat JSON "access" field.
      - Updates lastStatus (ACCESS_OK / ACCESS_DENIED / errors) and lastStats*.
      - Hook points for door relay, LEDs, buzzer are marked with TODO comments.

  Web interface (AsyncWebServer):
  - GET "/"        : Home page with buttons for Admin login, Register new card, and System status.
  - GET "/register":
      - No params    -> shows a form for user/password.
      - With params  -> stores tempUsername/tempPassword, opens a 15s RFID registration window,
                        and instructs the user to tap the new card.
  - GET "/login"   :
      - No params    -> shows login form.
      - With params  -> calls backend /stats, renders a table with that single user's fields,
                        and shows backend HTTP status and raw JSON.
  - GET "/status"  : Shows WiFi state, last UID, lastStatus, age of last RFID event,
                     and last backend HTTP details plus raw JSON.
  - onNotFound()   : Redirects any unknown path to "/".

  State for UI and debugging:
  - tempUsername, tempPassword        : pending registration credentials.
  - waitingForRFID, rfidTimeout       : control the registration window.
  - lastUid, lastStatus, lastEventMillis
  - lastStatsHttpCode, lastStatsRawJson, lastStatsError, lastStatsUser, lastStatsMillis
    These are used by the status and login pages and by Serial logs to show the latest events.
*/


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
const char *ssid = "SM-Yahya";
const char *password = "ya1234ya";
const char *registerUrl = "http://172.28.219.124:5000/register";  // POST user/pass/uid/roomID
const char *accessBaseUrl = "http://172.28.219.124:5000/user";    // GET uid/roomID
const char *statsUrl = "http://172.28.219.124:5000/stats";        // GET user/password
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

// ---------------------------
// Global status for web pages
// ---------------------------
String lastUid = "";
String lastStatus = "";  // "REG_ATTEMPT", "ACCESS_OK", "ACCESS_DENIED", "TIMEOUT", etc.
unsigned long lastEventMillis = 0;

int lastStatsHttpCode = 0;
String lastStatsRawJson = "";
String lastStatsError = "";
String lastStatsUser = "";
unsigned long lastStatsMillis = 0;


String lastLoginSuggestedUser = "";
String lastLoginSuggestedUid = "";

// =============================
// Function prototypes
// =============================
void tryRegisterRFID(const String &uid);  // you already implement this somewhere
void checkAccessRFID(const String &uid);  // you already implement this somewhere

// ---------------------------
// Helpers
// ---------------------------
void resetRegistrationWindow() {
  waitingForRFID = false;
  tempUsername = "";
  tempPassword = "";
}

void handleRegistrationTimeout() {
  if (!waitingForRFID) return;
  if (millis() <= rfidTimeout) return;

  Serial.println("RFID registration timed out.");
  resetRegistrationWindow();
  lastStatus = "TIMEOUT";
}

bool readCardUid(String &uidOut) {
  if (!rfid.PICC_IsNewCardPresent()) {
    return false;
  }
  if (!rfid.PICC_ReadCardSerial()) {
    Serial.println("RFID: card present but read failed");
    return false;
  }

  uidOut = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidOut += "0";
    uidOut += String(rfid.uid.uidByte[i], HEX);
  }
  uidOut.toUpperCase();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return true;
}

void handleCardUid(const String &uid) {
  lastUid = uid;
  lastEventMillis = millis();

  if (waitingForRFID && millis() <= rfidTimeout) {
    Serial.println("RFID in REGISTRATION window, sending to backend...");
    lastStatus = "REG_ATTEMPT";
    tryRegisterRFID(uid);
  } else {
    Serial.println("RFID in ACCESS mode, checking permissions...");
    lastStatus = "ACCESS_ATTEMPT";
    checkAccessRFID(uid);  // normal access check
    lookupUserByUid(uid);  // extra: update login suggestion
  }
}

// ---------------------------
// Home page HTML
// ---------------------------
String buildHomePageHtml() {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;text-align:center;}"
    ".card{max-width:480px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "a.button{display:block;margin:0.5rem 0;padding:0.5rem 1rem;"
    "border:1px solid #007bff;border-radius:4px;text-decoration:none;"
    "color:#007bff;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>ESP32 Access Control</h2>"
    "<p>Room "
    + roomID + "</p>"
               "<a class='button' href='/login'>Admin login</a>"
               "<a class='button' href='/register'>Register new card</a>"
               "<a class='button' href='/status'>System status</a>"
               "</div>"
               "</body></html>";
  return html;
}

String buildRegisterPageHtml(const String &message) {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;}"
    ".card{max-width:480px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "label{display:block;margin-top:0.5rem;}"
    "input[type=text],input[type=password]{width:100%;padding:0.5rem;"
    "margin-top:0.25rem;box-sizing:border-box;}"
    "button{margin-top:1rem;padding:0.5rem 1rem;width:100%;}"
    ".msg{margin-top:0.5rem;white-space:pre-wrap;}"
    ".links{margin-top:1rem;text-align:center;}"
    "a{text-decoration:none;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>Register new card</h2>"
    "<form action='/register' method='GET'>"
    "<label>User:<input name='user' type='text' required></label>"
    "<label>Password:<input name='password' type='password' required></label>"
    "<button type='submit'>Start registration</button>"
    "</form>";

  if (message.length()) {
    html += "<div class='msg'>" + message + "</div>";
  }

  html +=
    "<div class='links'>"
    "<a href='/'>Home</a> | <a href='/status'>Status</a>"
    "</div>"
    "</div></body></html>";

  return html;
}


// Simple JSON extractor for flat keys
String extractJsonField(const String &src, const char *key) {
  String pattern = String("\"") + key + "\"";
  int p = src.indexOf(pattern);
  if (p < 0) return "";
  p = src.indexOf(':', p);
  if (p < 0) return "";
  p++;
  while (p < (int)src.length() && (src[p] == ' ' || src[p] == '\"')) p++;
  String out;
  while (p < (int)src.length() && src[p] != '\"' && src[p] != ',' && src[p] != '}') {
    out += src[p++];
  }
  return out;
}

// =============================
// Backend calls for RFID
// =============================

void lookupUserByUid(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String("http://172.28.219.124:5000/uid-name/") + uid;
  Serial.print("LOOKUP: GET ");
  Serial.println(url);

  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String resp = http.getString();
    String uname = extractJsonField(resp, "user");
    if (uname.length()) {
      lastLoginSuggestedUser = uname;
      lastLoginSuggestedUid = uid;
      Serial.print("LOOKUP: got username ");
      Serial.println(uname);
    }
  }
  http.end();
}


// Registration: called when waitingForRFID == true
void tryRegisterRFID(const String &uid) {
  // no pending user data
  if (tempUsername.isEmpty() || tempPassword.isEmpty()) {
    Serial.println("REG: Missing username/password for registration.");
    lastStatus = "REG_ERR_NOPARAM";
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("REG: WiFi not connected.");
    lastStatus = "REG_WIFI_ERR";
    lastStatsError = "WiFi not connected";
    return;
  }

  HTTPClient http;
  http.begin(registerUrl);
  http.addHeader("Content-Type", "application/json");

  // minimal JSON body
  String body = "{";
  body += "\"user\":\"" + tempUsername + "\",";
  body += "\"password\":\"" + tempPassword + "\",";
  body += "\"uid\":\"" + uid + "\",";
  body += "\"roomID\":\"" + roomID + "\"";
  body += "}";

  Serial.print("REG: POST ");
  Serial.println(registerUrl);
  Serial.print("REG body: ");
  Serial.println(body);

  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  lastStatsHttpCode = code;
  lastStatsRawJson = resp;
  lastStatsMillis = millis();
  lastStatsUser = tempUsername;

  if (code >= 200 && code < 300) {
    Serial.println("REG: OK from backend.");
    lastStatus = "REG_OK";
    lastStatsError = "";
  } else {
    Serial.print("REG: backend error code ");
    Serial.println(code);
    lastStatus = "REG_FAIL";
    lastStatsError = "REG HTTP " + String(code) + " body: " + resp;
  }

  // registration window is done
  resetRegistrationWindow();
}

// Access check: called when waitingForRFID == false
void checkAccessRFID(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ACCESS: WiFi not connected.");
    lastStatus = "ACCESS_WIFI_ERR";
    lastStatsError = "WiFi not connected";
    return;
  }

  HTTPClient http;
  String url = String(accessBaseUrl) + "/" + uid + "?roomID=" + roomID;
  Serial.print("ACCESS: GET ");
  Serial.println(url);

  http.begin(url);
  int code = http.GET();
  String resp = http.getString();
  http.end();

  lastStatsHttpCode = code;
  lastStatsRawJson = resp;
  lastStatsMillis = millis();
  lastStatsUser = uid;  // or keep last user if you map uid->user elsewhere

  if (code <= 0) {
    Serial.print("ACCESS: HTTP error ");
    Serial.println(code);
    lastStatus = "ACCESS_HTTP_ERR";
    lastStatsError = "ACCESS HTTP " + String(code);
    return;
  }

  if (code == 200) {
    // backend success, check "access" field
    String accessStr = extractJsonField(resp, "access");
    accessStr.toLowerCase();

    bool hasAccess =
      accessStr == "true" || accessStr == "yes" || accessStr == "1";

    if (hasAccess) {
      Serial.println("ACCESS: granted.");
      lastStatus = "ACCESS_OK";
      lastStatsError = "";
      // TODO: open door, turn green LED, etc
    } else {
      Serial.println("ACCESS: denied by backend payload.");
      lastStatus = "ACCESS_DENIED";
      lastStatsError = "access field is false";
      // TODO: red LED, buzzer, etc
    }
  } else if (code == 403) {
    Serial.println("ACCESS: 403 forbidden.");
    lastStatus = "ACCESS_FORBIDDEN";
    lastStatsError = "ACCESS 403: " + resp;
  } else {
    Serial.print("ACCESS: unexpected HTTP ");
    Serial.println(code);
    lastStatus = "ACCESS_FAIL";
    lastStatsError = "ACCESS HTTP " + String(code) + " body: " + resp;
  }
}


// ---------------------------
// HTML builders
// ---------------------------
String buildLoginFormHtml() {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;}"
    ".card{max-width:480px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "label{display:block;margin-top:0.5rem;}"
    "input[type=text],input[type=password]{width:100%;padding:0.5rem;"
    "margin-top:0.25rem;box-sizing:border-box;}"
    "button{margin-top:1rem;padding:0.5rem 1rem;width:100%;}"
    ".links{margin-top:1rem;text-align:center;}"
    "a{text-decoration:none;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>Admin login</h2>"
    "<form action='/login' method='GET'>"
    "<label>User:<input name='user' type='text' required></label>"
    "<label>Password:<input name='password' type='password' required></label>"
    "<button type='submit'>Fetch my data</button>"
    "</form>"
    "<div class='links'><a href='/'>Back</a></div>"
    "</div>"
    "<script>"
    "setInterval(function(){"
    "fetch('/login-hint')"
    ".then(function(r){if(!r.ok) return null; return r.json();})"
    ".then(function(d){"
    "if(!d || !d.user) return;"
    "var inp = document.querySelector('input[name=\"user\"]');"
    "if(inp && !inp.value){ inp.value = d.user; }"
    "})"
    ".catch(function(e){});"
    "},1000);"
    "</script>"
    "</body></html>";
  return html;
}


String buildLoginResultHtml(const String &tableHtml, const String &errorText) {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;}"
    ".card{max-width:480px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "h2{margin-top:0;}"
    ".error{color:#b00020;margin-top:0.5rem;white-space:pre-wrap;"
    "word-wrap:break-word;}"
    "table{width:100%;border-collapse:collapse;margin-top:0.5rem;}"
    "th,td{border:1px solid #ccc;padding:0.4rem;text-align:left;"
    "font-size:0.9rem;}"
    ".links{margin-top:1rem;text-align:center;}"
    "a{text-decoration:none;}"
    "pre{white-space:pre-wrap;font-size:0.8rem;"
    "background:#f5f5f5;padding:0.5rem;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>My access data</h2>";

  if (errorText.length() > 0) {
    html += "<div class='error'>" + errorText + "</div>";
  } else {
    html += tableHtml;
    html += "<h3>Backend call status</h3>";
    html += "<table>";
    html += "<tr><td>HTTP code</td><td>" + String(lastStatsHttpCode) + "</td></tr>";
    html += "<tr><td>User</td><td>" + lastStatsUser + "</td></tr>";
    html += "<tr><td>Age (ms)</td><td>" + String(millis() - lastStatsMillis) + "</td></tr>";
    html += "</table>";
    html += "<h3>Raw JSON</h3><pre>" + lastStatsRawJson + "</pre>";
  }

  html +=
    "<div class='links'>"
    "<a href='/login'>Back to login</a><br>"
    "<a href='/status'>System status</a><br>"
    "<a href='/'>Home</a>"
    "</div>"
    "</div></body></html>";

  return html;
}

String buildStatusPageHtml() {
  String html;
  html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<style>"
    "body{font-family:Arial,Helvetica,sans-serif;margin:0;padding:1rem;}"
    ".card{max-width:600px;margin:0 auto;border:1px solid #ccc;"
    "padding:1rem;border-radius:8px;}"
    "table{width:100%;border-collapse:collapse;margin-top:0.5rem;}"
    "th,td{border:1px solid #ccc;padding:0.4rem;text-align:left;"
    "font-size:0.9rem;}"
    "pre{white-space:pre-wrap;font-size:0.8rem;"
    "background:#f5f5f5;padding:0.5rem;}"
    ".links{text-align:center;margin-top:1rem;}"
    "</style>"
    "</head><body>"
    "<div class='card'>"
    "<h2>System status</h2>"
    "<table>"
    "<tr><th>Field</th><th>Value</th></tr>";

  html += "<tr><td>WiFi</td><td>" + String(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED") + "</td></tr>";
  html += "<tr><td>Last RFID UID</td><td>" + lastUid + "</td></tr>";
  html += "<tr><td>Last RFID status</td><td>" + lastStatus + "</td></tr>";
  html += "<tr><td>Last RFID age (ms)</td><td>" + String(millis() - lastEventMillis) + "</td></tr>";
  html += "<tr><td>Last stats user</td><td>" + lastStatsUser + "</td></tr>";
  html += "<tr><td>Last stats HTTP</td><td>" + String(lastStatsHttpCode) + "</td></tr>";
  html += "<tr><td>Last stats error</td><td>" + (lastStatsError.length() ? lastStatsError : "none") + "</td></tr>";
  html += "</table>";

  if (lastStatsRawJson.length()) {
    html += "<h3>Last stats JSON</h3><pre>" + lastStatsRawJson + "</pre>";
  }

  html +=
    "<div class='links'><a href='/'>Home</a> | <a href='/login'>Login</a></div>"
    "</div></body></html>";

  return html;
}

// ---------------------------
// Route setup
// ---------------------------
void setupRoutes() {
  // HOME
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildHomePageHtml());
  });


  // LOGIN HINT - JSON with last suggested username from scanned card
  server.on("/login-hint", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"uid\":\"" + lastLoginSuggestedUid + "\",";
    json += "\"user\":\"" + lastLoginSuggestedUser + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });


  // REGISTER (UI to set tempUsername/tempPassword and open RFID window)
  server.on("/register", HTTP_GET, [](AsyncWebServerRequest *request) {
    // no params: show registration form
    if (!request->hasParam("user") || !request->hasParam("password")) {
      request->send(200, "text/html", buildRegisterPageHtml(""));
      return;
    }

    tempUsername = request->getParam("user")->value();
    tempPassword = request->getParam("password")->value();

    waitingForRFID = true;
    rfidTimeout = millis() + 15000;  // 15s window, adjust if needed
    lastStatus = "REG_WAIT";

    String msg = "Registration started.\n\n"
                 "User: "
                 + tempUsername + "\n"
                                  "Tap the new card on the reader within 15 seconds.";
    request->send(200, "text/html", buildRegisterPageHtml(msg));
  });

  // LOGIN (unchanged from your cleaned version)
  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("user") || !request->hasParam("password")) {
      request->send(200, "text/html", buildLoginFormHtml());
      return;
    }

    const String user = request->getParam("user")->value();
    const String pass = request->getParam("password")->value();
    lastStatsUser = user;

    String userJson;
    String errorText;
    int httpCode = -1;

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      const String url = String(statsUrl) + "?user=" + user + "&password=" + pass;

      http.begin(url);
      httpCode = http.GET();
      if (httpCode > 0) {
        userJson = http.getString();
      } else {
        errorText = "HTTP request failed, code " + String(httpCode);
      }
      http.end();
    } else {
      errorText = "No WiFi connection.";
    }

    lastStatsHttpCode = httpCode;
    lastStatsRawJson = userJson;
    lastStatsMillis = millis();

    String tableHtml;

    if (errorText.isEmpty() && httpCode == 200) {
      // CLEAR LOGIN HINTS ON SUCCESS
      lastLoginSuggestedUser = "";
      lastLoginSuggestedUid = "";
      const String vUser = extractJsonField(userJson, "user");
      const String vPass = extractJsonField(userJson, "password");
      const String vUid = extractJsonField(userJson, "uid");
      const String vCounter = extractJsonField(userJson, "counter");
      const String vRoom = extractJsonField(userJson, "roomID");
      const String vAccess = extractJsonField(userJson, "access");

      tableHtml =
        "<table>"
        "<tr><th>Field</th><th>Value</th></tr>"
        "<tr><td>user</td><td>"
        + vUser + "</td></tr>"
                  "<tr><td>password</td><td>"
        + vPass + "</td></tr>"
                  "<tr><td>uid</td><td>"
        + vUid + "</td></tr>"
                 "<tr><td>counter</td><td>"
        + vCounter + "</td></tr>"
                     "<tr><td>roomID</td><td>"
        + vRoom + "</td></tr>"
                  "<tr><td>access</td><td>"
        + vAccess + "</td></tr>"
                    "</table>";

      lastStatsError = "";
    } else {
      if (errorText.isEmpty()) {
        errorText = "Backend HTTP " + String(httpCode) + ". Body: " + userJson;
      }
      lastStatsError = errorText;
    }

    request->send(200, "text/html", buildLoginResultHtml(tableHtml, errorText));
  });

  // STATUS
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildStatusPageHtml());
  });

  // 404 -> home
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
}




// ---------------------------
// setup / loop
// ---------------------------
void setup() {
  Serial.begin(115200);

  SPI.begin();
  rfid.PCD_Init();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  setupRoutes();
  server.begin();
}

void loop() {
  handleRegistrationTimeout();

  String uid;
  if (!readCardUid(uid)) {
    return;  // nothing this iteration
  }

  Serial.print("Card detected UID: ");
  Serial.println(uid);

  handleCardUid(uid);
}
