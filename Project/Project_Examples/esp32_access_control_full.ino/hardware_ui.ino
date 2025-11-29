extern const RgbColor COLOR_OFF;
extern const RgbColor COLOR_ACCESS_OK;
extern const RgbColor COLOR_ACCESS_DENIED;
extern const RgbColor COLOR_ACCESS_ERROR;
extern const RgbColor COLOR_REG_WAIT;
extern const RgbColor COLOR_REG_OK;
extern const RgbColor COLOR_REG_ERROR;
extern const RgbColor COLOR_TIMEOUT;
extern const RgbColor COLOR_PROCESSING;
extern const RgbColor COLOR_DELETE_OK;
extern const RgbColor COLOR_DELETE_NOTFOUND;
extern const RgbColor COLOR_DELETE_ERROR;
extern const RgbColor COLOR_TOUCH_INFO;
extern const RgbColor COLOR_DELETE_ROOM_ERR;
extern const RgbColor COLOR_DELETE_ROOM;
extern const RgbColor COLOR_DELETE_MODE;

// Only buzzer is extern from main now
extern const int buzzerPin;

// Servo globals from main (MWE-style)
extern Servo doorServo;
extern const int DOOR_CLOSED_ANGLE;
extern const int DOOR_OPEN_ANGLE;
extern const int SERVO_PIN;
extern bool doorIsOpen;
extern unsigned long nextToggleAt;
extern const unsigned long OPEN_MS;

// Other globals coming from main (not repeated here in full)
extern bool displayOk;
extern unsigned long ledOffMillis;
extern StatusCode lastStatusCode;
extern String lastUid;
extern String lastStatus;
extern String tempUsername;
extern String roomID;
extern bool deleteModeArmed;
extern bool deleteRoomModeArmed;
extern String deleteRoomPendingRoom;
extern unsigned long touchLastTime;
extern int touchPressCount;
extern int lastTouchState;
extern const unsigned long TOUCH_MULTI_PRESS_WINDOW_MS;
extern const unsigned long LED_SHORT_MS;
extern const unsigned long LED_NORMAL_MS;
extern const int redPin;
extern const int greenPin;
extern const int bluePin;

void showMsg(const String &l1, const String &l2, const String &l3, bool serial) {
  if (serial) {
    Serial.println(l1 + " " + l2 + " " + l3);
  }
  if (!displayOk) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(l1);
  if (l2.length()) display.println(l2);
  if (l3.length()) display.println(l3);
  display.display();
}

// ========== SERVO HELPERS (MWE-style doorSet) ==========

// Called once from setup() in main
void setupServoLock() {
  doorServo.attach(SERVO_PIN);  // same as MWE: doorServo.attach(SERVO_PIN);
  doorIsOpen = false;
  doorServo.write(DOOR_CLOSED_ANGLE);
  nextToggleAt = 0;
}

// High-level door API used by status machine
void doorSet(bool open) {
  doorIsOpen = open;
  int angle = open ? DOOR_OPEN_ANGLE : DOOR_CLOSED_ANGLE;
  doorServo.write(angle);
}

// =======================================================

void setColor(int redValue, int greenValue, int blueValue) {
  analogWrite(redPin, redValue);
  analogWrite(greenPin, greenValue);
  analogWrite(bluePin, blueValue);
}

void setColor(const RgbColor &c) {
  setColor(c.r, c.g, c.b);
}

// Simple success "melody": 3 short pulses
void playSuccessMelody() {
  for (int i = 0; i < 3; ++i) {
    digitalWrite(buzzerPin, HIGH);
    delay(80);
    digitalWrite(buzzerPin, LOW);
    delay(60);
  }
}

// Generic pulse helper
void buzzerPulse(int onMs, int offMs, int count) {
  for (int i = 0; i < count; ++i) {
    digitalWrite(buzzerPin, HIGH);
    delay(onMs);
    digitalWrite(buzzerPin, LOW);
    if (offMs > 0) {
      delay(offMs);
    }
  }
}

// Delete ROOM mode armed (double tap): short-short
void playDeleteRoomArmPattern() {
  buzzerPulse(80, 80, 2);
}

// Delete USER mode armed (triple tap): short-short-short
void playDeleteUserArmPattern() {
  buzzerPulse(80, 60, 3);
}

// Access granted: 3 short pulses
void playAccessOkPattern() {
  buzzerPulse(80, 60, 3);
}

// Access denied / forbidden: 2 medium pulses
void playAccessDeniedPattern() {
  buzzerPulse(200, 120, 2);
}

// Access error (HTTP/WiFi/fail): 2 long pulses
void playAccessErrorPattern() {
  buzzerPulse(400, 150, 2);
}

// Registration waiting: 2 slow soft pulses
void playRegWaitPattern() {
  buzzerPulse(100, 200, 2);
}

// Registration OK: 2 quick pulses
void playRegOkPattern() {
  buzzerPulse(90, 70, 2);
}

// Registration error: 3 medium pulses
void playRegErrorPattern() {
  buzzerPulse(180, 80, 3);
}

// Timeout: long - short
void playTimeoutPattern() {
  buzzerPulse(250, 100, 1);
  buzzerPulse(120, 0, 1);
}

// Delete OK: 1 short pulse
void playDeleteOkPattern() {
  buzzerPulse(100, 0, 1);
}

// Delete not found: 2 short pulses
void playDeleteNotFoundPattern() {
  buzzerPulse(120, 80, 2);
}

// Delete error: 3 long pulses
void playDeleteErrorPattern() {
  buzzerPulse(250, 120, 3);
}

// Touch info (single tap): 2 very short pulses
void playTouchInfoPattern() {
  buzzerPulse(60, 40, 2);
}

void handleLedTimeout() {
  if (ledOffMillis != 0 && millis() > ledOffMillis) {
    setColor(COLOR_OFF);
    digitalWrite(buzzerPin, LOW);
    // Door movement is handled by handleDoorTimeout()
    ledOffMillis = 0;
  }
}

// Non-blocking door auto-close based on MWE-style timing
void handleDoorTimeout() {
  if (!doorIsOpen) return;
  if (nextToggleAt == 0) return;

  unsigned long now = millis();
  if (now >= nextToggleAt) {
    doorSet(false);
    nextToggleAt = 0;
    Serial.println("Door CLOSED (timeout)");
  }
}

void updateIndicatorsForStatus() {
  digitalWrite(buzzerPin, LOW);

  switch (lastStatusCode) {
    case StatusCode::AccessOk:
      showMsg("Access granted", "UID: " + lastUid);
      setColor(COLOR_ACCESS_OK);

      // Fully open using MWE-style doorSet, then auto-close after OPEN_MS
      doorSet(true);
      playAccessOkPattern();
      nextToggleAt = millis() + OPEN_MS;
      Serial.println("Door OPEN (access ok)");

      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::AccessDenied:
    case StatusCode::AccessForbidden:
      showMsg("Access denied", "UID: " + lastUid);
      setColor(COLOR_ACCESS_DENIED);
      doorSet(false);
      nextToggleAt = 0;
      playAccessDeniedPattern();
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::AccessWifiErr:
    case StatusCode::AccessHttpErr:
    case StatusCode::AccessFail:
      showMsg("Access error", lastStatus);
      setColor(COLOR_ACCESS_ERROR);
      doorSet(false);
      nextToggleAt = 0;
      playAccessErrorPattern();
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::RegWait:
      showMsg("Registration", "User: " + tempUsername, "Tap card...");
      setColor(COLOR_REG_WAIT);
      doorSet(false);
      nextToggleAt = 0;
      playRegWaitPattern();
      ledOffMillis = 0;
      break;

    case StatusCode::RegOk:
      showMsg("Registration OK", "UID: " + lastUid);
      setColor(COLOR_REG_OK);
      doorSet(false);
      nextToggleAt = 0;
      playRegOkPattern();
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::RegFail:
    case StatusCode::RegErrNoParam:
    case StatusCode::RegWifiErr:
      showMsg("Registration error", lastStatus);
      setColor(COLOR_REG_ERROR);
      doorSet(false);
      nextToggleAt = 0;
      playRegErrorPattern();
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::Timeout:
      showMsg("Registration timed out");
      setColor(COLOR_TIMEOUT);
      doorSet(false);
      nextToggleAt = 0;
      playTimeoutPattern();
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::AccessAttempt:
    case StatusCode::RegAttempt:
      showMsg("Processing...", "UID: " + lastUid);
      setColor(COLOR_PROCESSING);
      doorSet(false);
      nextToggleAt = 0;
      ledOffMillis = 0;
      break;

    case StatusCode::DeleteOk:
      showMsg("Delete OK", "UID: " + lastUid);
      setColor(COLOR_DELETE_OK);
      doorSet(false);
      nextToggleAt = 0;
      playDeleteOkPattern();
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::DeleteNotFound:
      showMsg("Delete failed", "UID not found");
      setColor(COLOR_DELETE_NOTFOUND);
      doorSet(false);
      nextToggleAt = 0;
      playDeleteNotFoundPattern();
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::DeleteFail:
    case StatusCode::DeleteWifiErr:
      showMsg("Delete error");
      setColor(COLOR_DELETE_ERROR);
      doorSet(false);
      nextToggleAt = 0;
      playDeleteErrorPattern();
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::DeleteAttempt:
    case StatusCode::None:
    default:
      setColor(COLOR_OFF);
      doorSet(false);
      nextToggleAt = 0;
      ledOffMillis = 0;
      break;
  }
}

// Global variable for debouncing
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;  // 50ms debounce

void handleTouch() {
  int reading = digitalRead(TOUCH_PIN);
  unsigned long now = millis();

  // Reset logic if multi-press window expires
  if (touchPressCount > 0 && (now - touchLastTime) > TOUCH_MULTI_PRESS_WINDOW_MS) {
    int count = touchPressCount;
    touchPressCount = 0;

    if (count == 1) {
      String l1 = "Room: " + roomID;
      String l2 = "Status: " + (lastStatus.length() ? lastStatus : String("NONE"));
      String l3 = lastUid.length() ? "UID: " + lastUid : "";
      showMsg(l1, l2, l3);
      setColor(COLOR_TOUCH_INFO);
      playTouchInfoPattern();
      ledOffMillis = millis() + LED_SHORT_MS;

    } else if (count == 2) {
      // QUEUE ROOM DELETE, CANCEL USER DELETE
      if (roomID.length() == 0) {
        showMsg("Delete room", "Invalid room");
        setColor(COLOR_DELETE_ROOM_ERR);
        ledOffMillis = millis() + LED_SHORT_MS;
        return;
      }

      deleteModeArmed = false;  // cancel any user delete
      deleteRoomModeArmed = true;
      deleteRoomPendingRoom = roomID;

      showMsg("Delete room", "Room " + deleteRoomPendingRoom, "Tap card");
      setColor(COLOR_DELETE_ROOM);
      playDeleteRoomArmPattern();  // sound for room delete queued
      ledOffMillis = 0;

    } else if (count >= 3) {
      // QUEUE USER DELETE, CANCEL ROOM DELETE
      deleteRoomModeArmed = false;
      deleteRoomPendingRoom = "";
      deleteModeArmed = true;

      showMsg("Delete mode", "Tap card to delete user");
      setColor(COLOR_DELETE_MODE);
      playDeleteUserArmPattern();  // sound for user delete queued
      ledOffMillis = 0;
    }
  }

  // Debounce Logic
  if (reading != lastTouchState) {
    lastDebounceTime = now;
  }

  if ((now - lastDebounceTime) > DEBOUNCE_DELAY) {
    // If the state has stabilized
    static int stableState = LOW;
    if (reading != stableState) {
      stableState = reading;

      // Rising Edge (Pressed)
      if (stableState == HIGH) {
        if (now - touchLastTime < TOUCH_MULTI_PRESS_WINDOW_MS) {
          touchPressCount++;
        } else {
          touchPressCount = 1;
        }
        touchLastTime = now;
      }
    }
  }

  lastTouchState = reading;
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
      display.println("Check Wiring");
      display.print("Code: 0x");
      display.println(ver, HEX);
      display.display();
    }
    setColor(255, 0, 0);
    delay(1000);
    setColor(0, 0, 0);
  }

  if (ver == 0xB2) {
    Serial.println("WARNING: Clone MFRC522 detected (0xB2).");
  }
  rfid.PCD_DumpVersionToSerial();
}
