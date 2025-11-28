// main.ino
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

// Delete touch sensor
bool deleteModeArmed = false;       // full user delete (3+ touches)
bool deleteRoomModeArmed = false;   // room delete (2 touches)
String deleteRoomPendingRoom = "";  // room to delete when card is scanned
unsigned long touchLastTime = 0;
int touchPressCount = 0;

// Global status for web pages
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
// HTTP backend functions (in http_backend.ino)
void tryRegisterRFID(const String &uid);
void checkAccessRFID(const String &uid);
void lookupUserByUid(const String &uid);
void deleteUser(const String &uid);
void deleteRoomForUid(const String &uid, const String &room);

// Web UI functions (in web_ui.ino)
String buildHomePageHtml();
String buildRegisterPageHtml(const String &message);
String buildLoginFormHtml();
String buildLoginResultHtml(const String &tableHtml, const String &errorText);
String buildStatusPageHtml();
String extractJsonField(const String &src, const char *key);
void setupRoutes();

// Hardware UI functions (in hardware_ui.ino)
void updateIndicatorsForStatus();
void handleTouch();
void handleLedTimeout();
void showMsg(const String &l1, const String &l2 = "", const String &l3 = "", bool serial = true);
void setColor(int redValue, int greenValue, int blueValue);

// Other helpers
float floatMap(float x, float in_min, float in_max, float out_min, float out_max);
void verifyRFID();

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

// ---------------------------
// Helper
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

    while (true) {
      setColor(255, 0, 0);
      delay(300);
      setColor(0, 0, 0);
      delay(300);
    }
  }

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
  analogSetAttenuation(ADC_11db);

  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);

  setColor(0, 0, 0);

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

  if (!MDNS.begin("rfid")) {
    Serial.println("mDNS start failed");
  } else {
    Serial.println("mDNS responder started: http://rfid.local");
  }

  MDNS.addService("http", "tcp", 80);

  if (displayOk) {
    showMsg("ESP32 Access", "IP: " + WiFi.localIP().toString(), "Room " + roomID);
  }

  setupRoutes();
  server.begin();
}

void loop() {
  int analogValue = analogRead(POT_PIN);

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
    lastEventMillis = millis();
  }

  String uid;
  if (!readCardUid(uid)) {
    if (millis() - lastRead > 5000) {
      Serial.println("No new card.");
      lastRead = millis();
    }
    return;
  }

  lastRead = millis();
  Serial.print("Card detected UID: ");
  Serial.println(uid);
  handleCardUid(uid);
}
