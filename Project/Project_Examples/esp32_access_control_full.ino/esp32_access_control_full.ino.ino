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

// NEW: OLED and I2C display
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =============================
// RFID pins
// =============================
#define SS_PIN 21
#define RST_PIN 22

// =============================
// New hardware pins (LEDs, OLED, touch)
// =============================
#define LED_RED 27
#define LED_GREEN 13
#define LED_BLUE 12

#define TOUCH_PIN 14
#define POT_PIN 37  // potentiometer analog pin

#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C


// RGB pin definitions for analogWrite logic
const int redPin = LED_RED;
const int greenPin = LED_GREEN;
const int bluePin = LED_BLUE;

// =============================
// WiFi and backend
// =============================
const char *ssid = "SM-Yahya";
const char *password = "ya1234ya";
const char *registerUrl = "http://172.28.219.124:5000/register";  // POST user/pass/uid/roomID
const char *accessBaseUrl = "http://172.28.219.124:5000/user";    // GET uid/roomID
const char *statsUrl = "http://172.28.219.124:5000/stats";        // GET user/password
String roomID = "101";                                            // will be updated from potentiometer

// =============================
// RFID and server objects
// =============================
MFRC522 rfid(SS_PIN, RST_PIN);
AsyncWebServer server(80);

// NEW: OLED display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
static unsigned long lastRead = 0;

// =============================
// State
// =============================
String tempUsername = "";
String tempPassword = "";
bool waitingForRFID = false;
unsigned long rfidTimeout = 0;

// ---------------------------
// Delete touch sensor
// ---------------------------
// Delete touch sensor
bool deleteModeArmed = false;       // full user delete (3+ touches)
bool deleteRoomModeArmed = false;   // room delete (2 touches)
String deleteRoomPendingRoom = "";  // room to delete when card is scanned
unsigned long touchLastTime = 0;
int touchPressCount = 0;


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

// NEW: hardware state
bool displayOk = false;
unsigned long ledOffMillis = 0;
int lastTouchState = LOW;

// =============================
// Function prototypes
// =============================
void tryRegisterRFID(const String &uid);  // you already implement this somewhere
void checkAccessRFID(const String &uid);  // you already implement this somewhere

// NEW: helper prototypes
void updateIndicatorsForStatus();
void handleTouch();
void handleLedTimeout();
void showMsg(const String &l1, const String &l2 = "", const String &l3 = "", bool serial = true);
void setColor(int redValue, int greenValue, int blueValue);


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

  // NEW: update LEDs and OLED on timeout
  updateIndicatorsForStatus();
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


void deleteUser(const String &uid) {
  if (WiFi.status() != WL_CONNECTED) {
    lastStatus = "DELETE_WIFI_ERR";
    showMsg("Delete error", "WiFi");
    return;
  }

  HTTPClient http;
  String url = String("http://172.28.219.124:5000/user/") + uid;

  http.begin(url);
  int code = http.sendRequest("DELETE");
  String resp = http.getString();
  http.end();

  if (code == 200) {
    lastStatus = "DELETE_OK";
    showMsg("Deleted:", uid);
  } else if (code == 404) {
    lastStatus = "DELETE_NOTFOUND";
    showMsg("Delete failed", "UID not found");
  } else {
    lastStatus = "DELETE_FAIL";
    showMsg("Delete error", "HTTP " + String(code));
  }
}

void deleteRoomForUid(const String &uid, const String &room) {
  Serial.print("DELETE ROOM CALL uid=[");
  Serial.print(uid);
  Serial.print("] room=[");
  Serial.print(room);
  Serial.println("]");

  if (uid.isEmpty()) {
    lastStatus = "DELETE_FAIL";
    showMsg("Delete error", "No UID");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    lastStatus = "DELETE_WIFI_ERR";
    showMsg("Delete error", "WiFi");
    return;
  }

  // FIXED: define URL FIRST
  String url = "http://172.28.219.124:5000/room/" + uid + "/" + room;

  Serial.print("DELETE URL: ");
  Serial.println(url);

  HTTPClient http;
  http.begin(url);
  int code = http.sendRequest("DELETE");
  String resp = http.getString();
  http.end();

  Serial.print("DELETE HTTP code: ");
  Serial.println(code);
  Serial.print("DELETE resp: ");
  Serial.println(resp);

  if (code == 200) {
    lastStatus = "DELETE_OK";
    showMsg("Room removed", "UID: " + uid, "Room " + room);
  } else if (code == 400) {
    lastStatus = "DELETE_NOTFOUND";
    showMsg("Delete failed", "Room not found");
  } else if (code == 404) {
    lastStatus = "DELETE_NOTFOUND";
    showMsg("Delete failed", "UID not found");
  } else {
    lastStatus = "DELETE_FAIL";
    showMsg("Delete error", "HTTP " + String(code));
  }
}



void handleCardUid(const String &uid) {
  lastUid = uid;
  lastEventMillis = millis();

  // FULL USER DELETE (3+ touches)
  if (deleteModeArmed) {
    deleteModeArmed = false;
    lastStatus = "DELETE_ATTEMPT";
    deleteUser(uid);
    updateIndicatorsForStatus();
    return;
  }

  // ROOM DELETE (2 touches)
  if (deleteRoomModeArmed) {
    deleteRoomModeArmed = false;
    lastStatus = "DELETE_ATTEMPT";
    deleteRoomForUid(uid, deleteRoomPendingRoom);
    updateIndicatorsForStatus();
    return;
  }

  // REGISTRATION MODE
  if (waitingForRFID && millis() <= rfidTimeout) {
    Serial.println("RFID in REGISTRATION window, sending to backend...");
    lastStatus = "REG_ATTEMPT";
    tryRegisterRFID(uid);
  }
  // ACCESS MODE
  else {
    Serial.println("RFID in ACCESS mode, checking permissions...");
    lastStatus = "ACCESS_ATTEMPT";
    checkAccessRFID(uid);
    lookupUserByUid(uid);
  }

  updateIndicatorsForStatus();
}


// =============================
// NEW: OLED and LED helpers
// =============================

// Simple OLED print helper, also logs to Serial
void showMsg(const String &l1, const String &l2, const String &l3, bool serial) {
  String serialLine = l1;
  if (l2.length()) serialLine += " " + l2;
  if (l3.length()) serialLine += " " + l3;

  if (serial) {
    Serial.println(serialLine);
  }

  if (!displayOk) {
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(l1);
  if (l2.length()) display.println(l2);
  if (l3.length()) display.println(l3);
  display.display();
}

// Function to set the RGB color using PWM
void setColor(int redValue, int greenValue, int blueValue) {
  // PWM analogWrite values for each color
  analogWrite(redPin, redValue);
  analogWrite(greenPin, greenValue);
  analogWrite(bluePin, blueValue);
}

// Turn LEDs off after timeout
void handleLedTimeout() {
  if (ledOffMillis != 0 && millis() > ledOffMillis) {
    setColor(0, 0, 0);  // all off
    ledOffMillis = 0;
  }
}

// Map lastStatus + lastUid to RGB colors and OLED messages
void updateIndicatorsForStatus() {

  // ===== ACCESS SUCCESS =====
  if (lastStatus == "ACCESS_OK") {
    showMsg("Access granted", "UID: " + lastUid);
    setColor(0, 255, 0);  // GREEN
    ledOffMillis = millis() + 1500;
  }

  // ===== ACCESS DENIED =====
  else if (lastStatus == "ACCESS_DENIED" || lastStatus == "ACCESS_FORBIDDEN") {
    showMsg("Access denied", "UID: " + lastUid);
    setColor(255, 0, 0);  // RED
    ledOffMillis = millis() + 1500;
  }

  // ===== WIFI / HTTP FAILURE =====
  else if (lastStatus == "ACCESS_WIFI_ERR" || lastStatus == "ACCESS_HTTP_ERR" || lastStatus == "ACCESS_FAIL") {
    showMsg("Access error", lastStatus);
    setColor(255, 0, 255);  // MAGENTA = error
    ledOffMillis = millis() + 1500;
  }

  // ===== WAITING FOR CARD DURING REG =====
  else if (lastStatus == "REG_WAIT") {
    showMsg("Registration", "User: " + tempUsername, "Tap card...");
    setColor(0, 0, 255);  // BLUE = waiting
    ledOffMillis = 0;     // stays on
  }

  // ===== REGISTRATION SUCCESS =====
  else if (lastStatus == "REG_OK") {
    showMsg("Registration OK", "UID: " + lastUid);
    setColor(0, 255, 255);  // CYAN = success
    ledOffMillis = millis() + 1500;
  }

  // ===== REGISTRATION FAILURE =====
  else if (lastStatus == "REG_FAIL" || lastStatus == "REG_ERR_NOPARAM" || lastStatus == "REG_WIFI_ERR") {
    showMsg("Registration error", lastStatus);
    setColor(255, 165, 0);  // ORANGE = warning
    ledOffMillis = millis() + 1500;
  }

  // ===== REGISTRATION TIMEOUT =====
  else if (lastStatus == "TIMEOUT") {
    showMsg("Registration timed out");
    setColor(150, 0, 150);  // PURPLE
    ledOffMillis = millis() + 1500;
  }

  // ===== PROCESSING =====
  else if (lastStatus == "ACCESS_ATTEMPT" || lastStatus == "REG_ATTEMPT") {
    showMsg("Processing...", "UID: " + lastUid);
    setColor(255, 255, 0);  // YELLOW = processing
    ledOffMillis = 0;       // stays on
  } else if (lastStatus == "DELETE_OK") {
    showMsg("Delete OK", "UID: " + lastUid);
    setColor(255, 255, 255);
    ledOffMillis = millis() + 1500;
  }

  else if (lastStatus == "DELETE_NOTFOUND") {
    showMsg("Delete failed", "UID not found");
    setColor(255, 255, 0);
    ledOffMillis = millis() + 1500;
  }

  else if (lastStatus == "DELETE_FAIL") {
    showMsg("Delete error");
    setColor(0, 255, 200);
    ledOffMillis = millis() + 1500;
  }


  // ===== DEFAULT / UNKNOWN =====
  else {
    setColor(0, 0, 0);  // OFF
    ledOffMillis = 0;
  }
}


// Simple touch handler: short press shows current status on OLED
void handleTouch() {
  int current = digitalRead(TOUCH_PIN);
  unsigned long now = millis();

  // Resolve tap sequence after gap
  if (touchPressCount > 0 && (now - touchLastTime) > 600) {
    int count = touchPressCount;
    touchPressCount = 0;

    // 1 touch: show status + BLUE
    if (count == 1) {
      String l1 = "Room: " + roomID;
      String l2 = "Status: " + (lastStatus.length() ? lastStatus : String("NONE"));
      String l3 = lastUid.length() ? "UID: " + lastUid : "";
      showMsg(l1, l2, l3);

      setColor(0, 0, 255);            // blue = status view
      ledOffMillis = millis() + 800;  // auto off after debug
    }
    // 2 touches: delete current room for last UID + YELLOW
    else if (count == 2) {
      if (roomID.length() == 0) {
        showMsg("Delete room", "Invalid room");
        setColor(255, 0, 0);
        ledOffMillis = millis() + 800;
        return;
      }

      deleteRoomModeArmed = true;
      deleteRoomPendingRoom = roomID;

      showMsg("Delete room", "Room " + deleteRoomPendingRoom, "Tap card");
      setColor(255, 255, 0);  // yellow = room delete armed
      ledOffMillis = 0;       // keep on until action
    }
    // 3+ touches: arm delete mode (old 2-touch behavior) + PURPLE
    else if (count >= 3) {
      deleteModeArmed = true;
      showMsg("Delete mode", "Tap card to delete");

      setColor(180, 0, 255);  // purple = armed delete
      ledOffMillis = 0;
    }
  }

  // Edge detect touch
  if (lastTouchState == LOW && current == HIGH) {
    if (now - touchLastTime < 600) {
      touchPressCount++;
    } else {
      touchPressCount = 1;
    }
    touchLastTime = now;
  }

  lastTouchState = current;
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
  } else if (code == 404) {
    lastStatus = "ACCESS_DENIED";
    lastStatsError = "UID not found";
    lastLoginSuggestedUser = "";

  } else {
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

  if (lastStatsRawJson.length()) {
    html += "<h3>Last stats JSON</h3><pre>" + lastStatsRawJson + "</pre>";
  }

  html +=
    "<div class='links'><a href='/'>Home</a> | <a href='/login'>Login</a></div>"
    "</div>"
    "<script>"
    "setTimeout(function(){ location.reload(); }, 2000);"  // auto-refresh every 2s
    "</script>"
    "</body></html>";

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

    // NEW: reflect registration wait on OLED/LEDs
    updateIndicatorsForStatus();

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
// hELPER
// ---------------------------
float floatMap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void verifyRFID() {
  rfid.PCD_Init();
  delay(50);

  byte ver = rfid.PCD_ReadRegister(MFRC522::VersionReg);

  Serial.print("MFRC522 Firmware: 0x");
  Serial.println(ver, HEX);

  // Accept 0x92 (official) and 0xB2 (clone)
  if (ver != 0x92 && ver != 0xB2) {
    Serial.println("ERROR: Unknown MFRC522 firmware or reader not connected!");

    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("RFID ERROR");
      display.println("Unknown firmware");
      display.print("Found: 0x");
      display.println(ver, HEX);
      display.display();
    }

    // Flash red forever
    while (true) {
      setColor(255, 0, 0);
      delay(300);
      setColor(0, 0, 0);
      delay(300);
    }
  }

  // Optional: warn if using clone 0xB2
  if (ver == 0xB2) {
    Serial.println("WARNING: Clone MFRC522 detected (0xB2). UID reading OK, other functions may fail.");
  }

  rfid.PCD_DumpVersionToSerial();
}




// ---------------------------
// setup / loop
// ---------------------------
void setup() {
  Serial.begin(115200);
  analogSetAttenuation(ADC_11db);  // for full 0-3.3V range on ADC

  // NEW: init LEDs and touch
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);

  // start with all LEDs off
  setColor(0, 0, 0);


  // NEW: init OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 init failed");
    displayOk = false;
  } else {
    displayOk = true;
    showMsg("ESP32 Access", "Room " + roomID, "Initializing...");
  }

  SPI.begin();
  verifyRFID();





  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // =============================
  // mDNS HOSTNAME SUPPORT
  // =============================
  if (!MDNS.begin("rfid")) {  // rfid is the hostname
    Serial.println("mDNS start failed");
  } else {
    Serial.println("mDNS responder started: http://rfid.local");
  }

  // Advertise HTTP service on port 80
  MDNS.addService("http", "tcp", 80);

  if (displayOk) {
    showMsg("ESP32 Access", "IP: " + WiFi.localIP().toString(), "Room " + roomID);
  }

  setupRoutes();
  server.begin();
}

void loop() {
  int analogValue = analogRead(POT_PIN);  // 0..4095

  // Map analog input to room numbers 100..110
  int roomNum = (int)floatMap(analogValue, 0, 4095, 100, 110);
  roomID = roomNum;

  static int lastRoom = -1;
  if (roomNum != lastRoom) {
    lastRoom = roomNum;
    lastEventMillis = millis();
    showMsg("ESP32 Access", "Room " + String(roomID));
  }

  handleRegistrationTimeout();
  handleTouch();
  handleLedTimeout();

  if (!waitingForRFID && millis() - lastEventMillis > 2000) {
    showMsg("ESP32 Access", "Room " + String(roomID));
    lastEventMillis = millis();  // <-- throttle to every 2 s
  }


  String uid;
  if (!readCardUid(uid)) {
    if (millis() - lastRead > 5000) {
      Serial.println("No new card.");
      lastRead = millis();  // <-- update here to limit spam
    }
    return;
  }

  lastRead = millis();
  Serial.print("Card detected UID: ");
  Serial.println(uid);
  handleCardUid(uid);
}
