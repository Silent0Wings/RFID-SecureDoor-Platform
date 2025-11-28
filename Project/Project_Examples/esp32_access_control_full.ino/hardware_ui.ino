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

// Extern pins defined in main.ino
extern const int relayPin;
extern const int buzzerPin;

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

void setColor(int redValue, int greenValue, int blueValue) {
  analogWrite(redPin, redValue);
  analogWrite(greenPin, greenValue);
  analogWrite(bluePin, blueValue);
}

void setColor(const RgbColor &c) {
  setColor(c.r, c.g, c.b);
}

void handleLedTimeout() {
  if (ledOffMillis != 0 && millis() > ledOffMillis) {
    setColor(COLOR_OFF);
    
    // RELAY LOCK LOGIC: Ensure door is locked when LED turns off
    digitalWrite(relayPin, LOW); 
    digitalWrite(buzzerPin, LOW);

    ledOffMillis = 0;
  }
}

void updateIndicatorsForStatus() {
  // Reset Door/Buzzer defaults
  digitalWrite(relayPin, LOW);
  digitalWrite(buzzerPin, LOW);

  switch (lastStatusCode) {
    case StatusCode::AccessOk:
      showMsg("Access granted", "UID: " + lastUid);
      setColor(COLOR_ACCESS_OK);
      // OPEN DOOR
      digitalWrite(relayPin, HIGH); 
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::AccessDenied:
    case StatusCode::AccessForbidden:
      showMsg("Access denied", "UID: " + lastUid);
      setColor(COLOR_ACCESS_DENIED);
      // BUZZER ALERT
      digitalWrite(buzzerPin, HIGH);
      delay(100); // Short beep
      digitalWrite(buzzerPin, LOW);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::AccessWifiErr:
    case StatusCode::AccessHttpErr:
    case StatusCode::AccessFail:
      showMsg("Access error", lastStatus);
      setColor(COLOR_ACCESS_ERROR);
      // ERROR TONE
      digitalWrite(buzzerPin, HIGH);
      delay(500); 
      digitalWrite(buzzerPin, LOW);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::RegWait:
      showMsg("Registration", "User: " + tempUsername, "Tap card...");
      setColor(COLOR_REG_WAIT);
      ledOffMillis = 0;
      break;

    case StatusCode::RegOk:
      showMsg("Registration OK", "UID: " + lastUid);
      setColor(COLOR_REG_OK);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::RegFail:
    case StatusCode::RegErrNoParam:
    case StatusCode::RegWifiErr:
      showMsg("Registration error", lastStatus);
      setColor(COLOR_REG_ERROR);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::Timeout:
      showMsg("Registration timed out");
      setColor(COLOR_TIMEOUT);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::AccessAttempt:
    case StatusCode::RegAttempt:
      showMsg("Processing...", "UID: " + lastUid);
      setColor(COLOR_PROCESSING);
      ledOffMillis = 0;
      break;

    case StatusCode::DeleteOk:
      showMsg("Delete OK", "UID: " + lastUid);
      setColor(COLOR_DELETE_OK);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::DeleteNotFound:
      showMsg("Delete failed", "UID not found");
      setColor(COLOR_DELETE_NOTFOUND);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::DeleteFail:
    case StatusCode::DeleteWifiErr:
      showMsg("Delete error");
      setColor(COLOR_DELETE_ERROR);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::DeleteAttempt:
    case StatusCode::None:
    default:
      setColor(COLOR_OFF);
      ledOffMillis = 0;
      break;
  }
}

// Global variable for debouncing
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50; // 50ms debounce

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
      ledOffMillis = millis() + LED_SHORT_MS;
    } else if (count == 2) {
      if (roomID.length() == 0) {
        showMsg("Delete room", "Invalid room");
        setColor(COLOR_DELETE_ROOM_ERR);
        ledOffMillis = millis() + LED_SHORT_MS;
        return;
      }
      deleteRoomModeArmed = true;
      deleteRoomPendingRoom = roomID;
      showMsg("Delete room", "Room " + deleteRoomPendingRoom, "Tap card");
      setColor(COLOR_DELETE_ROOM);
      ledOffMillis = 0;
    } else if (count >= 3) {
      deleteModeArmed = true;
      showMsg("Delete mode", "Tap card to delete");
      setColor(COLOR_DELETE_MODE);
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

  // CRITICAL FIX: Removed while(true) loop
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
    // Set error color but allow system to boot (maybe it's intermittent)
    setColor(255, 0, 0); 
    delay(1000);
    setColor(0,0,0);
  }

  if (ver == 0xB2) {
    Serial.println("WARNING: Clone MFRC522 detected (0xB2).");
  }
  rfid.PCD_DumpVersionToSerial();
}