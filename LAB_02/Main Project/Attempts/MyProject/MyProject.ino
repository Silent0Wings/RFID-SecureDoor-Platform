#include <Arduino.h>
#include <WiFi.h>
#include <BluetoothSerial.h>

#include "wifi_utils.h"
#include "ble_utils.h"
#include "security.h"
#include "buzzer.h"

const char *ssid = "REDACTED_SSID";
const char *password = "REDACTED_PASSWORD";
const char *bNames = "IoT_Jukebox";

BluetoothSerial SerialBT; // Classic BT

void setup()
{
    Serial.begin(115200);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(300);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");

    SerialBT.begin(bNames, true);
    Serial.println("Classic Bluetooth active");

    buzzer_init();
    setupBLE(); // starts BLE advertising
}

void loop()
{
    if (btCmd)
    {
        Serial.printf("Command: %c\n", btCmd);
        btCmd = 0;
    }
    delay(100);
}
