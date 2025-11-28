// hardware_ui.ino
// OLED screen, RGB LED and touch-button helpers

// Uses globals defined in main.ino:
// - display, displayOk
// - redPin, greenPin, bluePin
// - roomID, lastUid, lastStatus
// - tempUsername
// - deleteModeArmed, deleteRoomModeArmed, deleteRoomPendingRoom
// - touchPressCount, touchLastTime, lastTouchState
// - ledOffMillis

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

void handleLedTimeout() {
  if (ledOffMillis != 0 && millis() > ledOffMillis) {
    setColor(0, 0, 0);
    ledOffMillis = 0;
  }
}

void updateIndicatorsForStatus() {
  if (lastStatus == "ACCESS_OK") {
    showMsg("Access granted", "UID: " + lastUid);
    setColor(0, 255, 0);
    ledOffMillis = millis() + 1500;
  } else if (lastStatus == "ACCESS_DENIED" || lastStatus == "ACCESS_FORBIDDEN") {
    showMsg("Access denied", "UID: " + lastUid);
    setColor(255, 0, 0);
    ledOffMillis = millis() + 1500;
  } else if (lastStatus == "ACCESS_WIFI_ERR" || lastStatus == "ACCESS_HTTP_ERR" || lastStatus == "ACCESS_FAIL") {
    showMsg("Access error", lastStatus);
    setColor(255, 0, 255);
    ledOffMillis = millis() + 1500;
  } else if (lastStatus == "REG_WAIT") {
    showMsg("Registration", "User: " + tempUsername, "Tap card...");
    setColor(0, 0, 255);
    ledOffMillis = 0;
  } else if (lastStatus == "REG_OK") {
    showMsg("Registration OK", "UID: " + lastUid);
    setColor(0, 255, 255);
    ledOffMillis = millis() + 1500;
  } else if (lastStatus == "REG_FAIL" || lastStatus == "REG_ERR_NOPARAM" || lastStatus == "REG_WIFI_ERR") {
    showMsg("Registration error", lastStatus);
    setColor(255, 165, 0);
    ledOffMillis = millis() + 1500;
  } else if (lastStatus == "TIMEOUT") {
    showMsg("Registration timed out");
    setColor(150, 0, 150);
    ledOffMillis = millis() + 1500;
  } else if (lastStatus == "ACCESS_ATTEMPT" || lastStatus == "REG_ATTEMPT") {
    showMsg("Processing...", "UID: " + lastUid);
    setColor(255, 255, 0);
    ledOffMillis = 0;
  } else if (lastStatus == "DELETE_OK") {
    showMsg("Delete OK", "UID: " + lastUid);
    setColor(255, 255, 255);
    ledOffMillis = millis() + 1500;
  } else if (lastStatus == "DELETE_NOTFOUND") {
    showMsg("Delete failed", "UID not found");
    setColor(255, 255, 0);
    ledOffMillis = millis() + 1500;
  } else if (lastStatus == "DELETE_FAIL") {
    showMsg("Delete error");
    setColor(0, 255, 200);
    ledOffMillis = millis() + 1500;
  } else {
    setColor(0, 0, 0);
    ledOffMillis = 0;
  }
}

void handleTouch() {
  int current = digitalRead(TOUCH_PIN);
  unsigned long now = millis();

  if (touchPressCount > 0 && (now - touchLastTime) > 600) {
    int count = touchPressCount;
    touchPressCount = 0;

    if (count == 1) {
      String l1 = "Room: " + roomID;
      String l2 = "Status: " + (lastStatus.length() ? lastStatus : String("NONE"));
      String l3 = lastUid.length() ? "UID: " + lastUid : "";
      showMsg(l1, l2, l3);

      setColor(0, 0, 255);
      ledOffMillis = millis() + 800;
    } else if (count == 2) {
      if (roomID.length() == 0) {
        showMsg("Delete room", "Invalid room");
        setColor(255, 0, 0);
        ledOffMillis = millis() + 800;
        return;
      }

      deleteRoomModeArmed = true;
      deleteRoomPendingRoom = roomID;

      showMsg("Delete room", "Room " + deleteRoomPendingRoom, "Tap card");
      setColor(255, 255, 0);
      ledOffMillis = 0;
    } else if (count >= 3) {
      deleteModeArmed = true;
      showMsg("Delete mode", "Tap card to delete");

      setColor(180, 0, 255);
      ledOffMillis = 0;
    }
  }

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
