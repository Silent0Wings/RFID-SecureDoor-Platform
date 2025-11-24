#include <Wire.h>
#define OLED_SDA 21
#define OLED_SCL 22

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);
  Serial.println("I2C Scanner");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found I2C device at 0x");
      Serial.println(addr, HEX);
    }
  }
  Serial.println("Done");
}

void loop() {}
