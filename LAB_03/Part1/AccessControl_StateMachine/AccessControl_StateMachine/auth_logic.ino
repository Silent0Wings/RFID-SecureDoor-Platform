// Logic for credential check, lockout, state-to-hardware mapping

// Validate form input vs current credentials
bool checkAuth(String user, String pass) {
  return (user == String(storedUsername) && pass == String(storedPassword));
}

// Set outputs for idle/waiting
void setStateIdle() {
  setGreenLed(false);
  setRedLed(false);
  setServoPosition(0);
}

// Auth success: grant access/reset fail count
void setStateAuthSuccess() {
  setGreenLed(true);
  setRedLed(false);
  setServoPosition(180);
  failCount = 0;
}

// Auth fail: increment fail count, handle potential lockout
void setStateAuthFail() {
  setGreenLed(false);
  setRedLed(true);
  setServoPosition(0);
  failCount++;
  if (failCount >= 3) {
    currentState = LOCKED_OUT;
    lockoutStartTime = millis();
  } else {
    currentState = IDLE;
  }
}

// Lockout: keep locked/LED on during timeout
void setStateLockedOut() {
  setGreenLed(false);
  setRedLed(true);
  setServoPosition(0);
}

// Handle lockout timer and reset to idle when time elapsed
void handleLockout() {
  if (currentState == LOCKED_OUT && millis() - lockoutStartTime >= lockoutDuration) {
    currentState = IDLE;
    failCount = 0;
    setStateIdle();
  }
}
