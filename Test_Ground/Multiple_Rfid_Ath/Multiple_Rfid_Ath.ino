#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =========================================================
// - Clears all stored RFID UIDs at startup
// - Touch sensor triggers registration mode (store next tag)
// - If touched again after 1 second → enter deletion mode (remove next tag)
// - Reads scanned RFID and checks if stored in memory
//   → Match = Access Granted (green LED)
//   → No match = Access Denied (red LED)
// - Supports up to MAX_UIDS stored UIDs
// - Displays all actions on OLED and serial monitor
// =========================================================

// ===== PINS =====
#define SS_PIN 21
#define RST_PIN 22
#define SCK_PIN 18
#define MISO_PIN 19
#define MOSI_PIN 23
#define LED_RED 13
#define LED_GREEN 12

// ===== OLED =====
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

// ===== TOUCH =====
#define TOUCH_PIN 14
#define TOUCH_THRESHOLD 1

// ===== STORAGE =====
#define MAX_UIDS 5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
MFRC522 rfid(SS_PIN, RST_PIN);
Preferences prefs;

const char *STORAGE_NAMESPACE = "rfid_ns";
const char *KEY_PREFIX = "uid_";

bool registerNext = false;
bool deleteNext = false;
unsigned long registerStart = 0;

int lastState = LOW;
int currentState;

// ===== OLED PRINT =====
void showMsg(const String &l1, const String &l2 = "", const String &l3 = "", bool serial = true) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(l1);
  if (l2 != "") display.println(l2);
  if (l3 != "") display.println(l3);
  display.display();
  if (serial) Serial.println(l1 + " " + l2 + " " + l3);
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 init failed");
    while (true);
  }

  showMsg("RFID Auth System", "Initializing...");
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  prefs.begin(STORAGE_NAMESPACE, false);

  // Clear all UIDs on startup
  for (int i = 0; i < MAX_UIDS; i++) {
    String key = String(KEY_PREFIX) + i;
    prefs.remove(key.c_str());
  }
  showMsg("Memory cleared", "All keys deleted");
  showMsg("Ready for scan...");
}

// ===== LOOP =====
void loop() {
  currentState = digitalRead(TOUCH_PIN);

  // Touch pressed
  if (lastState == LOW && currentState == HIGH) {
    unsigned long now = millis();

    // If already registering and >1s passed, switch to deletion mode
    if (registerNext && (now - registerStart > 1000)) {
      registerNext = false;
      deleteNext = true;
      showMsg("Delete mode", "Next tag will be deleted");
    } else if (!registerNext && !deleteNext) {
      // Normal touch → register mode
      registerNext = true;
      registerStart = now;
      showMsg("Touch detected", "Next tag will be stored");
    }
    delay(700);
  }
  lastState = currentState;

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  // Build UID string
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  showMsg("Scanned UID:", uidStr);

  bool match = false;
  bool full = true;
  int matchIndex = -1;

  // Check all stored UIDs
  for (int i = 0; i < MAX_UIDS; i++) {
    String key = String(KEY_PREFIX) + i;
    String stored = prefs.getString(key.c_str(), "");
    if (stored == "") full = false;
    if (stored == uidStr) {
      match = true;
      matchIndex = i;
      break;
    }
  }

  // ===== DELETE MODE =====
  if (deleteNext) {
    if (match) {
      String key = String(KEY_PREFIX) + matchIndex;
      prefs.remove(key.c_str());
      showMsg("UID Deleted", uidStr);
    } else {
      showMsg("UID not found", uidStr);
    }
    deleteNext = false;
    return;
  }

  // ===== REGISTER MODE =====
  if (registerNext && !full && !match) {
    for (int i = 0; i < MAX_UIDS; i++) {
      String key = String(KEY_PREFIX) + i;
      String stored = prefs.getString(key.c_str(), "");
      if (stored == "") {
        prefs.putString(key.c_str(), uidStr);
        showMsg("Stored new UID", "Slot " + String(i));
        break;
      }
    }
    registerNext = false;
    return;
  }

  // ===== AUTHENTICATION =====
  if (match) {
    showMsg("Access Granted", uidStr);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, LOW);
  } else {
    showMsg("Access Denied", uidStr);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, HIGH);
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(1500);

  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  showMsg("Ready for next card...");
}
