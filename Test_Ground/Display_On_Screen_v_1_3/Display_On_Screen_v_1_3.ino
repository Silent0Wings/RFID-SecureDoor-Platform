#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_SDA   4
#define OLED_SCL   15
#define OLED_RST   16

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C

// RST is controlled by GPIO16
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("TTGO OLED test start");

  // 1) Reset OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);
  delay(20);

  // 2) Start I2C on the TTGO OLED pins
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);

  // Optional quick scan (will print nothing if no device)
  Serial.println("I2C scan:");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  Found device at 0x");
      Serial.println(addr, HEX);
    }
  }

  // 3) Init display without redoing Wire.begin
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false)) {
    Serial.println("SSD1306 init failed");
    while (true) { delay(1000); }
  }

  Serial.println("OLED init OK");

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("TTGO LoRa32 V1.3");
  display.println("OLED test OK");
  display.display();
  Serial.println("Text drawn");
}

void loop() { }
