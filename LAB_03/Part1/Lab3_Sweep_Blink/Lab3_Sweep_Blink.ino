#include <ESP32Servo.h>
//#include <WiFi.h>
//#include <WebServer.h>
 
Servo myservo;
const int servoPin = 21;
const int ledPin13 = 13;
const int ledPin12 = 12;

unsigned long previousMillis = 0;
const long blinkInterval = 500; // ms

unsigned long lastServoUpdate = 0;
const long servoUpdateInterval = 15; // ms
int pos = 0;
int direction = 1; // 1: increasing, -1: decreasing
bool ledState = LOW;

// ----- LED Behavior -----
void updateLEDs(unsigned long currentMillis) {
  if (currentMillis - previousMillis >= blinkInterval) {
    previousMillis = currentMillis;
    ledState = !ledState;
    digitalWrite(ledPin13, ledState ? HIGH : LOW);
    digitalWrite(ledPin12, ledState ? LOW : HIGH);
  }
}

// ----- Servo Sweep Behavior -----
void updateServo(unsigned long currentMillis) {
  if (currentMillis - lastServoUpdate >= servoUpdateInterval) {
    lastServoUpdate = currentMillis;
    myservo.write(pos);
    pos += direction;
    if (pos >= 180) {
      direction = -1;
      pos = 180;
    } else if (pos <= 0) {
      direction = 1;
      pos = 0;
    }
  }
}

void setup() {
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  myservo.setPeriodHertz(50);
  myservo.attach(servoPin, 1000, 2000);

  pinMode(ledPin13, OUTPUT);
  pinMode(ledPin12, OUTPUT);
}

void loop() {
  unsigned long currentMillis = millis();
  updateLEDs(currentMillis);
  updateServo(currentMillis);
}
