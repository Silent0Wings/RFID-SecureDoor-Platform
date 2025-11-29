#include <ESP32Servo.h>

const int SERVO_PIN = 26;           // HS-422 signal pin
const int DOOR_CLOSED_ANGLE = 0;    // adjust after testing
const int DOOR_OPEN_ANGLE   = 90;   // adjust after testing

const unsigned long OPEN_MS   = 3000;
const unsigned long CLOSED_MS = 3000;

Servo doorServo;
bool doorIsOpen = false;
unsigned long nextToggleAt = 0;

void doorSet(bool open) {
  doorIsOpen = open;
  int angle = open ? DOOR_OPEN_ANGLE : DOOR_CLOSED_ANGLE;
  doorServo.write(angle);
}

void setup() {
  Serial.begin(115200);
  doorServo.attach(SERVO_PIN);
  doorSet(false);                            // start closed
  nextToggleAt = millis() + CLOSED_MS;       // wait before first open
  Serial.println("Periodic door test started");
}

void loop() {
  unsigned long now = millis();
  if (now >= nextToggleAt) {
    if (doorIsOpen) {
      doorSet(false);                        // go closed
      nextToggleAt = now + CLOSED_MS;
      Serial.println("Door CLOSED");
    } else {
      doorSet(true);                         // go open
      nextToggleAt = now + OPEN_MS;
      Serial.println("Door OPEN");
    }
  }
}
