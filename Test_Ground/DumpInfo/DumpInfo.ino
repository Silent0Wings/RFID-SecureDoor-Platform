#include <SPI.h>
#include <MFRC522.h>

// --- TTGO LoRa32 V1.0 mapping ---
#define SS_PIN   21
#define RST_PIN  22
#define SCK_PIN  18
#define MISO_PIN 19
#define MOSI_PIN 23

MFRC522 mfrc522(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  delay(500);
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  mfrc522.PCD_Init();
  delay(4);
  mfrc522.PCD_DumpVersionToSerial();
  Serial.println("Scan PICC to see UID, SAK, type, and data blocks...");
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    delay(10);
    return;
  }
  if (!mfrc522.PICC_ReadCardSerial()) {
    delay(10);
    return;
  }

  mfrc522.PICC_DumpToSerial(&(mfrc522.uid));
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(200);
}
