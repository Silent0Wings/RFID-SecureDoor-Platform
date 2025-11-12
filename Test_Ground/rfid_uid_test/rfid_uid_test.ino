#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 21
#define RST_PIN 22

MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("Ready to scan RFID...");
}

void loop() {
  // Check for new RFID card
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  // Convert UID bytes to uppercase hex string
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  Serial.print("UID Hex: ");
  Serial.println(uidStr);

  // Prepare POST data string
  String postData = "uid=" + uidStr;
  Serial.print("POST data: ");
  Serial.println(postData);

  // Halt card and stop encryption
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(3000); // Wait 3 seconds before next read
}
