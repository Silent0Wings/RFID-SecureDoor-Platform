int sensorPin = 14;  // Your analog input pin
const int NUM_SAMPLES = 10;
bool Debug = true;
// --- Sensor Reading ---
int readSensorRaw() {
  return analogRead(sensorPin);
}

// --- Filtering ---
int sensorMovingAverage() {
  static int readings[NUM_SAMPLES];
  static int readIdx = 0;
  static long sum = 0;
  static bool inited = false;
  if (!inited) {
    for (int i = 0; i < NUM_SAMPLES; i++) readings[i] = readSensorRaw();
    for (int i = 0; i < NUM_SAMPLES; i++) sum += readings[i];
    inited = true;
  }
  sum -= readings[readIdx];
  readings[readIdx] = readSensorRaw();
  sum += readings[readIdx];
  readIdx = (readIdx + 1) % NUM_SAMPLES;
  return sum / NUM_SAMPLES;
}

// --- Presence Check ---
bool isPresenceDetected(int threshold = 3000) {
  int value = sensorMovingAverage();
  return (value > threshold);
}

// --- Serial Output (UI) ---
void printSensorStatus() {
  int filtered = sensorMovingAverage();

  if (Debug)
    Serial.print(filtered);
  else
    Serial.println(filtered);

  if (Debug)
    if (isPresenceDetected(2400)) {
      /* 
        2400 was chosen based on the serial plotter with debug off at
        rest with no object hovering the infrared sensor only varies in beween 2300 and 2400
      */
      Serial.println(" PRESENCE DETECTED");
    } else {
      Serial.println(" NO PRESENCE");
    }
}

// --- Main Arduino Functions ---
void setup() {
  Serial.begin(9600);
}

void loop() {
  printSensorStatus();
  delay(10);
}
