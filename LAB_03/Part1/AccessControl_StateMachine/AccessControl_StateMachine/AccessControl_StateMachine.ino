// SOEN422 – Lab 3 Access Control System
// Main file: setup and state variables, endpoint registration

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <ESPmDNS.h>

const char *ssid = "Chimera";
const char *password = "Sranklord1";

/*
alternative Network
*/
const char *ssid1 = "REDACTED_SSID";
const char *password1 = "REDACTED_PASSWORD";

bool alternativeNetwork = true;
WebServer server(80);

// open http://accesspanel.local

// Hardware and logic globals
Servo myservo;
const int servoPin = 21;
const int ledGreen = 13;
const int ledRed = 12;

// State machine: credentials can be updated in admin panel
char storedUsername[16] = "admin";
char storedPassword[16] = "admin";
enum AccessState { IDLE,
                   AUTH_SUCCESS,
                   AUTH_FAIL,
                   LOCKED_OUT };
AccessState currentState = IDLE;
unsigned long lockoutStartTime = 0;
const unsigned long lockoutDuration = 120000;
int failCount = 0;

// Prototypes
void handleLogin();
void handleRoot();
void handleNotFound();
void handleAdmin();

void setup() {
  Serial.begin(115200);
  pinMode(ledGreen, OUTPUT);
  pinMode(ledRed, OUTPUT);
  myservo.attach(servoPin, 1000, 2000);
  setStateIdle();

  WiFi.mode(WIFI_STA);
  if (alternativeNetwork)
    WiFi.begin(ssid1, password1);
  else
    WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // mDNS local domain setup
  if (MDNS.begin("accesspanel")) {  // accessible at http://accesspanel.local
    Serial.println("mDNS responder started: accesspanel.local");
  } else {
    Serial.println("Error setting up MDNS responder");
  }

  // Register endpoints
  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.onNotFound(handleNotFound);
  server.on("/admin", handleAdmin);

  server.begin();
}

void loop() {
  server.handleClient();
  handleLockout();  // Check lockout transitions
  delay(2);
}
