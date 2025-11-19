#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 21  
#define RST_PIN 22

MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  SPI.begin();                  // or SPI.begin(18, 19, 23, SS_PIN) if you wired to VSPI
  rfid.PCD_Init();
  rfid.PCD_DumpVersionToSerial();
  Serial.println("RFID test ready");
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }
  if (!rfid.PICC_ReadCardSerial()) {
    Serial.println("Card present but read failed");
    delay(50);
    return;
  }

  Serial.print("UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(200);
}
