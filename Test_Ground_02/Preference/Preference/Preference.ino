#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "REDACTED_SSID";
const char* password = "REDACTED_PASSWORD";

// Invoke-WebRequest -Uri "https://iotjukebox.onrender.com/preference?id=3a:3Acf:3Ad2:3A03:3Ab5:3A6b&key=YahyaPhone&value=harrypotter" -Method POST

// Encode ':' -> %3A for valid URLs
String encodeMac(String mac, bool val) {
  if (!val)
    return mac;
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

  String mac = encodeMac("3a:cf:d2:03:b5:6b",false);
  String key = "Yahya";
  String value = "harrypotter";

  sendPost(mac, key, value);
  delay(3000);
  sendGet(mac, key, value);
}

void loop() {}

// ---------------- GET ----------------
void sendGet(String mac, String key, String value) {
  HTTPClient http;
  String url = "https://iotjukebox.onrender.com/preference?id=" + mac + "&key=" + key + "&value=" + value;
  Serial.println("GET URL: " + url);
  http.begin(url);
  int code = http.GET();
  Serial.printf("GET -> HTTP %d\n", code);
  if (code > 0) Serial.println(http.getString());
  http.end();
}

// ---------------- POST ----------------
// Matches working PowerShell example: parameters in URL, not body
void sendPost(String mac, String key, String value) {
  HTTPClient http;
  String url = "https://iotjukebox.onrender.com/preference?id=" + mac + "&key=" + key + "&value=" + value;
  Serial.println("POST URL: " + url);
  http.begin(url);
  int code = http.POST("");  // empty body; params are in URL
  Serial.printf("POST -> HTTP %d\n", code);
  if (code > 0) Serial.println(http.getString());
  http.end();
}
