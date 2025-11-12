#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// READ THE RFID CARD check if its the same as the one stored if it is led green if not led red
// if the touch sensor is triggered store the next card as the authentification id then continue

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
#define TOUCH_PIN 14       // must be one of: 0,2,4,12,13,14,15,27,32,33
#define TOUCH_THRESHOLD 1  // adjust based on readings
int lastState = LOW;       // the previous state from the input pin
int currentState;          // the current reading from the input pin

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
MFRC522 rfid(SS_PIN, RST_PIN);
Preferences prefs;

const char *STORAGE_NAMESPACE = "rfid_ns";
const char *KEY_UID = "stored_uid";

bool registerNext = false;

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
    while (true)
      ;
  }

  showMsg("RFID Auth System", "Initializing...");

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  rfid.PCD_Init();
  prefs.begin(STORAGE_NAMESPACE, false);

  String existingUID = prefs.getString(KEY_UID, "");
  if (existingUID.length() > 0)
    showMsg("Stored UID:", existingUID);
  else
    showMsg("No UID stored yet.");

  showMsg("Ready for scan...");
}

// ===== LOOP =====
void loop() {
  currentState = digitalRead(TOUCH_PIN);

  Serial.println(currentState);

  if (lastState == LOW && currentState == HIGH && !registerNext) {
    registerNext = true;
    showMsg("Touch detected", String("Next tag will be stored  currentState=") + currentState + ">=" + TOUCH_THRESHOLD);
    delay(700);
  }
  lastState = currentState;

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  showMsg("Scanned UID:", uidStr);
  String storedUID = prefs.getString(KEY_UID, "");

  if (registerNext) {
    prefs.putString(KEY_UID, uidStr);
    showMsg("UID stored.", uidStr);
    storedUID = uidStr;
    registerNext = false;
  }

  if (uidStr == storedUID) {
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
