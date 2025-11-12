#include <SPI.h>
#include <MFRC522.h>

// Pin map (confirmed working)
#define RC522_CS 17
#define RC522_RST 22
#define SCK_PIN 5
#define MISO_PIN 19
#define MOSI_PIN 27

MFRC522 rfid(RC522_CS, RC522_RST);

void setup() {
  Serial.begin(115200);
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, RC522_CS);
  rfid.PCD_Init();

  Serial.println("RC522 ready. Scan an RFID tag...");
}

void loop() {
  // wait for new card
  //Serial.println("Loop...");

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  Serial.print("Card UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) Serial.print(" 0");
    else Serial.print(" ");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();

  // card type
  MFRC522::PICC_Type type = rfid.PICC_GetType(rfid.uid.sak);
  Serial.print("Card type: ");
  Serial.println(rfid.PICC_GetTypeName(type));

  // halt
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(500);
}
