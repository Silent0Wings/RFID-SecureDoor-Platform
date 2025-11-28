// hardware_ui.ino
// OLED screen, RGB LED and touch-button helpers

// Uses globals defined in main.ino:
// - display, displayOk
// - redPin, greenPin, bluePin
// - roomID, lastUid, lastStatus, lastStatusCode
// - tempUsername
// - deleteModeArmed, deleteRoomModeArmed, deleteRoomPendingRoom
// - touchPressCount, touchLastTime, lastTouchState
// - ledOffMillis
// - LED_SHORT_MS, LED_NORMAL_MS, TOUCH_MULTI_PRESS_WINDOW_MS

// Externs for color constants defined in main.ino
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

void setColor(int redValue, int greenValue, int blueValue) {
  analogWrite(redPin, redValue);
  analogWrite(greenPin, greenValue);
  analogWrite(bluePin, blueValue);
}

// Overload using named RGB colors
void setColor(const RgbColor &c) {
  setColor(c.r, c.g, c.b);
}

void handleLedTimeout() {
  if (ledOffMillis != 0 && millis() > ledOffMillis) {
    setColor(COLOR_OFF);
    ledOffMillis = 0;
  }
}

void updateIndicatorsForStatus() {
  switch (lastStatusCode) {
    case StatusCode::AccessOk:
      showMsg("Access granted", "UID: " + lastUid);
      setColor(COLOR_ACCESS_OK);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::AccessDenied:
    case StatusCode::AccessForbidden:
      showMsg("Access denied", "UID: " + lastUid);
      setColor(COLOR_ACCESS_DENIED);
      ledOffMillis = millis() + LED_NORMAL_MS;
      break;

    case StatusCode::AccessWifiErr:
    case StatusCode::AccessHttpErr:
    case StatusCode::AccessFail:
      showMsg("Access error", lastStatus);
      setColor(COLOR_ACCESS_ERROR);
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

void handleTouch() {
  int current = digitalRead(TOUCH_PIN);
  unsigned long now = millis();

  // End of multi-press window
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

  // Rising edge detection on touch input
  if (lastTouchState == LOW && current == HIGH) {
    if (now - touchLastTime < TOUCH_MULTI_PRESS_WINDOW_MS) {
      touchPressCount++;
    } else {
      touchPressCount = 1;
    }
    touchLastTime = now;
  }

  lastTouchState = current;
}
