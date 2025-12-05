// Functions to abstract hardware control (LEDs, Servo) for modular code

// Set green LED ON/OFF
void setGreenLed(bool state) {
  digitalWrite(ledGreen, state ? HIGH : LOW);
}

// Set red LED ON/OFF
void setRedLed(bool state) {
  digitalWrite(ledRed, state ? HIGH : LOW);
}

// Move servo to specific angle (0 = lock, 180 = unlock)
void setServoPosition(int angle) {
    myservo.attach(servoPin);   // Ensure attached
    myservo.write(angle);       // Move to desired angle
    delay(300);                 // Wait for servo movement
    myservo.detach();           // Stop PWM and vibration

    /* 
    previous code :
      myservo.write(angle);
    */
}

// Fancy blink for error/lockout (call if desired in lockout state)
// void blinkRedGreen(unsigned long interval) { ... }
