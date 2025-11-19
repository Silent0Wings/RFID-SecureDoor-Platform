// Pin definitions
const int redPin = 13;
const int greenPin = 12;
const int bluePin = 27;

void setup() {
  // Set the RGB pins as outputs
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
}

void loop() {
  // Red
  setColor(255, 0, 0);
  delay(1000);

  // Green
  setColor(0, 255, 0);
  delay(1000);

  // Blue
  setColor(0, 0, 255);
  delay(1000);

  // White (all colors on)
  setColor(255, 255, 255);
  delay(1000);

  // Turn off
  setColor(0, 0, 0);
  delay(1000);
}

// Function to set the RGB color
void setColor(int redValue, int greenValue, int blueValue) {
  // PWM analogWrite values for each color
  analogWrite(redPin, redValue);
  analogWrite(greenPin, greenValue);
  analogWrite(bluePin, blueValue);
}
