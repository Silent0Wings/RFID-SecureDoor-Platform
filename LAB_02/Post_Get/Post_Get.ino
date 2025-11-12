#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "SM-Yahya";
const char* password = "ya1234ya";

// Encode ':' -> %3A for valid URLs
String encodeMac(String mac) {
  mac.replace(":", "%3A");
  return mac;
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  String mac = encodeMac("3a:cf:d2:03:b5:6b");
  String key = "IoT_Jukebox";
  String value = "harrypotter";

  sendGet(mac, key, value);
  delay(2000);
  sendPost(mac, key, value);
}

void loop() {}

// ---------------- GET ----------------
void sendGet(String mac, String key, String value) {
  HTTPClient http;
  String url = "https://iotjukebox.onrender.com/preference?id=" + mac +
               "&key=" + key + "&value=" + value;
  Serial.println("GET URL: " + url);
  http.begin(url);
  int code = http.GET();
  Serial.printf("GET -> HTTP %d\n", code);
  if (code > 0) Serial.println(http.getString());
  http.end();
}

// ---------------- POST ----------------
void sendPost(String mac, String key, String value) {
  HTTPClient http;
  http.begin("https://iotjukebox.onrender.com/preference");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "id=" + mac + "&key=" + key + "&value=" + value;
  Serial.println("POST body: " + body);
  int code = http.POST(body);
  Serial.printf("POST -> HTTP %d\n", code);
  if (code > 0) Serial.println(http.getString());
  http.end();
}
