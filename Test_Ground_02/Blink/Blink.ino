#define LED_PIN 21  // Try 2; if nothing blinks, try 25 or 22

void setup() {
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_PIN, HIGH); // LED on
  delay(500);
  digitalWrite(LED_PIN, LOW);  // LED off
  delay(500);
}
