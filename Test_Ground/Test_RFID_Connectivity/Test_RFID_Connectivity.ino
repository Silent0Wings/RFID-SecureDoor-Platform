#include <SPI.h>
#include <MFRC522.h>

#define RC522_CS 17      // your chosen free GPIO
#define RC522_RST 22
MFRC522 mfrc522(RC522_CS, RC522_RST);

// if it return VersionReg: 0x0 not connected
// anything else 0xAA suxcess or anything else press reset button to check many times
void setup() {
  Serial.begin(115200);
  // Use LoRa SPI pins explicitly:
  SPI.begin(5, 19, 27, RC522_CS);
  mfrc522.PCD_Init();
  byte v = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print("VersionReg: 0x");
  Serial.println(v, HEX);
}
void loop() {}
