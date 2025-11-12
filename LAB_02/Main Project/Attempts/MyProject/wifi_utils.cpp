#include "wifi_utils.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

const char *API_BASE = "https://iotjukebox.onrender.com";

String encodeMac(String mac)
{
    mac.replace(":", "%3A");
    return mac;
}

void sendGet(String mac, String key, String value)
{
    String url = String(API_BASE) + "/preference?id=" + encodeMac(mac) + "&key=" + key + "&value=" + value;

    Serial.println("GET URL: " + url);
    int code = -1;

    for (int i = 1; i <= 5; i++)
    {
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin(client, url);
        http.setTimeout(15000);
        code = http.GET();

        Serial.printf("GET (try %d) -> HTTP %d\n", i, code);
        if (code == 200)
        {
            Serial.println(http.getString());
            http.end();
            return;
        }
        http.end();
        delay(1000);
    }
    Serial.println("GET failed after retries.");
}

void sendPost(String mac, String key, String value)
{
    String url = String(API_BASE) + "/preference?id=" + encodeMac(mac) + "&key=" + key + "&value=" + value;

    Serial.println("== PowerShell-style POST ==");
    Serial.println("Invoke-WebRequest -Uri \"" + url + "\" -Method POST");

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);

    int code = http.POST(""); // empty body
    Serial.printf("POST -> HTTP %d\n", code);
    if (code > 0)
        Serial.println(http.getString());
    else
        Serial.println("POST failed");

    http.end();
}
