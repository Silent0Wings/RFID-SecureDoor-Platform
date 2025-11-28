#define ENABLE_WEBSERVER_AUTHENTICATION 0

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =============================
// CONFIGURATION & CREDENTIALS
// =============================
const char *WIFI_SSID = "SM-Yahya";      // wiffi name
const char *WIFI_PASS = "ya1234ya";  // wiffi pswd
const char *BACKEND_IP = "172.28.219.124";     // ip of backend
const int BACKEND_PORT = 5000;

// =============================
// PIN DEFINITIONS
// =============================
#define SS_PIN 21
#define RST_PIN 22
#define LED_RED 27
#define LED_GREEN 13
#define LED_BLUE 12
#define TOUCH_PIN 14
#define POT_PIN 37
#define DOOR_RELAY_PIN 26  // NEW: Door Lock Relay
#define BUZZER_PIN 25      // NEW: Piezo Buzzer

// OLED I2C
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

// =============================
// RGB color struct + named colors
// =============================
struct RgbColor { int r; int g; int b; };
const RgbColor COLOR_OFF = { 0, 0, 0 };
const RgbColor COLOR_ACCESS_OK = { 0, 255, 0 };
const RgbColor COLOR_ACCESS_DENIED = { 255, 0, 0 };
const RgbColor COLOR_ACCESS_ERROR = { 255, 0, 255 };
const RgbColor COLOR_REG_WAIT = { 0, 0, 255 };
const RgbColor COLOR_REG_OK = { 0, 255, 255 };
const RgbColor COLOR_REG_ERROR = { 255, 165, 0 };
const RgbColor COLOR_TIMEOUT = { 150, 0, 150 };
const RgbColor COLOR_PROCESSING = { 255, 255, 0 };
const RgbColor COLOR_DELETE_OK = { 255, 255, 255 };
const RgbColor COLOR_DELETE_NOTFOUND = { 255, 255, 0 };
const RgbColor COLOR_DELETE_ERROR = { 0, 255, 200 };
const RgbColor COLOR_TOUCH_INFO = { 0, 0, 255 };
const RgbColor COLOR_DELETE_ROOM_ERR = { 255, 0, 0 };
const RgbColor COLOR_DELETE_ROOM = { 255, 255, 0 };
const RgbColor COLOR_DELETE_MODE = { 180, 0, 255 };

// =============================
// Status codes
// =============================
enum class StatusCode : uint8_t {
  None, RegAttempt, RegWait, RegOk, RegFail, RegErrNoParam, RegWifiErr,
  AccessAttempt, AccessOk, AccessDenied, AccessForbidden, AccessWifiErr, AccessHttpErr, AccessFail,
  Timeout, DeleteAttempt, DeleteOk, DeleteNotFound, DeleteFail, DeleteWifiErr
};

// =============================
// Timing constants (ms)
// =============================
constexpr unsigned long REGISTER_TIMEOUT_MS = 15000;
constexpr unsigned long STATUS_IDLE_REFRESH_MS = 2000;
constexpr unsigned long NO_CARD_LOG_INTERVAL_MS = 5000;
constexpr unsigned long TOUCH_MULTI_PRESS_WINDOW_MS = 600;
constexpr unsigned long LED_SHORT_MS = 800;
constexpr unsigned long LED_NORMAL_MS = 1500;

// Global status
StatusCode lastStatusCode = StatusCode::None;
String lastStatus = "NONE";

const char *statusCodeToText(StatusCode s) {
  switch (s) {
    case StatusCode::AccessOk: return "ACCESS_OK";
    case StatusCode::AccessDenied: return "ACCESS_DENIED";
    // ... (Keep existing mappings if needed, shortened for brevity)
    default: return "STATUS_UPDATE";
  }
}

void setStatus(StatusCode code) {
  lastStatusCode = code;
  // Simple mapping for display
  if(code == StatusCode::AccessOk) lastStatus = "ACCESS_OK";
  else if(code == StatusCode::AccessDenied) lastStatus = "ACCESS_DENIED";
  else lastStatus = "PROCESSING"; 
}

// Pins for Hardware UI
const int redPin = LED_RED;
const int greenPin = LED_GREEN;
const int bluePin = LED_BLUE;
const int relayPin = DOOR_RELAY_PIN; // Exposed for hardware_ui
const int buzzerPin = BUZZER_PIN;    // Exposed for hardware_ui

// =============================
// WiFi and backend
// =============================
String registerUrl;
String accessBaseUrl;
String statsUrl;
String roomID = "101";

// =============================
// Objects
// =============================
MFRC522 rfid(SS_PIN, RST_PIN);
AsyncWebServer server(80);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
static unsigned long lastRead = 0;

// =============================
// State
// =============================
String tempUsername = "";
String tempPassword = "";
bool waitingForRFID = false;
unsigned long registrationStartTime = 0; // REPLACED: rfidTimeout for overflow safety

bool deleteModeArmed = false;
bool deleteRoomModeArmed = false;
String deleteRoomPendingRoom = "";
unsigned long touchLastTime = 0;
int touchPressCount = 0;

String lastUid = "";
unsigned long lastEventMillis = 0;
int lastStatsHttpCode = 0;
String lastStatsRawJson = "";
String lastStatsError = "";
String lastStatsUser = "";
unsigned long lastStatsMillis = 0;

String lastLoginSuggestedUser = "";
String lastLoginSuggestedUid = "";

bool displayOk = false;
unsigned long ledOffMillis = 0;
int lastTouchState = LOW;

// =============================
// Prototypes
// =============================
void tryRegisterRFID(const String &uid);
void checkAccessRFID(const String &uid);
void lookupUserByUid(const String &uid);
void deleteUser(const String &uid);
void deleteRoomForUid(const String &uid, const String &room);
String buildHomePageHtml();
String buildRegisterPageHtml(const String &message);
String buildLoginFormHtml();
String buildLoginResultHtml(const String &tableHtml, const String &errorText);
String buildStatusPageHtml();
// extern String extractJsonField(const String &src, const char *key); // Defined in http_backend
void setupRoutes();
void updateIndicatorsForStatus();
void handleTouch();
void handleLedTimeout();
void showMsg(const String &l1, const String &l2 = "", const String &l3 = "", bool serial = true);
void setColor(int redValue, int greenValue, int blueValue);
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
  // Fix: Overflow safe check
  if (millis() - registrationStartTime > REGISTER_TIMEOUT_MS) {
    Serial.println("RFID registration timed out.");
    resetRegistrationWindow();
    setStatus(StatusCode::Timeout);
    updateIndicatorsForStatus();
  }
}

bool readCardUid(String &uidOut) {
  if (!rfid.PICC_IsNewCardPresent()) return false;
  if (!rfid.PICC_ReadCardSerial()) return false;

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

  if (deleteModeArmed) {
    deleteModeArmed = false;
    setStatus(StatusCode::DeleteAttempt);
    deleteUser(uid);
    updateIndicatorsForStatus();
    return;
  }

  if (deleteRoomModeArmed) {
    deleteRoomModeArmed = false;
    setStatus(StatusCode::DeleteAttempt);
    deleteRoomForUid(uid, deleteRoomPendingRoom);
    updateIndicatorsForStatus();
    return;
  }

  // Safe overflow check
  if (waitingForRFID && (millis() - registrationStartTime <= REGISTER_TIMEOUT_MS)) {
    Serial.println("RFID in REGISTRATION window...");
    setStatus(StatusCode::RegAttempt);
    tryRegisterRFID(uid);
  } else {
    Serial.println("RFID in ACCESS mode...");
    setStatus(StatusCode::AccessAttempt);
    checkAccessRFID(uid);
    lookupUserByUid(uid);
  }
  updateIndicatorsForStatus();
}

float floatMap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ---------------------------
// Setup & Loop
// ---------------------------
void setup() {
  Serial.begin(115200);
  
  // Construct URLs dynamically
  String baseUrl = String("http://") + BACKEND_IP + ":" + BACKEND_PORT;
  registerUrl = baseUrl + "/register";
  accessBaseUrl = baseUrl + "/user";
  statsUrl = baseUrl + "/stats";

  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  
  // NEW: Setup Door and Buzzer
  pinMode(DOOR_RELAY_PIN, OUTPUT);
  digitalWrite(DOOR_RELAY_PIN, LOW); // Default Locked
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

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
  verifyRFID(); // Now non-blocking

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  if (MDNS.begin("rfid")) {
    Serial.println("mDNS: http://rfid.local");
  }

  if (displayOk) {
    showMsg("ESP32 Access", "IP: " + WiFi.localIP().toString(), "Room " + roomID);
  }

  setupRoutes();
  server.begin();
}

void loop() {
  int analogValue = analogRead(POT_PIN);
  int roomNum = (int)floatMap(analogValue, 0, 4095, 100, 110);
  roomID = String(roomNum);

  static int lastRoom = -1;
  if (roomNum != lastRoom) {
    lastRoom = roomNum;
    lastEventMillis = millis();
    showMsg("ESP32 Access", "Room " + roomID);
  }

  handleRegistrationTimeout();
  handleTouch();
  handleLedTimeout();

  if (!waitingForRFID && millis() - lastEventMillis > STATUS_IDLE_REFRESH_MS) {
    showMsg("ESP32 Access", "Room " + roomID);
    lastEventMillis = millis();
  }

  String uid;
  if (!readCardUid(uid)) {
    if (millis() - lastRead > NO_CARD_LOG_INTERVAL_MS) {
      lastRead = millis();
    }
    return;
  }

  lastRead = millis();
  Serial.println("Card: " + uid);
  handleCardUid(uid);
}