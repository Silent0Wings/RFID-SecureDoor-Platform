// SOEN422 – Lab 3: Access Control with WiFi + IR + Servo + LoRaWAN (TTN)
// Web login/admin, 2-step auth (credentials + IR presence), 3-fail lockout,
// LEDs + servo, TTN uplink: {"field1":<user_id>,"field2":<event>}

#include <esp_system.h>
#include "esp32-hal-ledc.h"  // add near top
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <ESPmDNS.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>


// ---------- Debug ----------

bool debugSensor = false;
// ---------- LoRaWAN keys (OTAA) ----------
static const u1_t PROGMEM APPEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };                                                   // LSB
static const u1_t PROGMEM DEVEUI[8] = { 0xC5, 0x3C, 0x07, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };                                                   // LSB
static const u1_t PROGMEM APPKEY[16] = { 0x98, 0xB8, 0x84, 0xD7, 0xB1, 0x19, 0x01, 0x04, 0x25, 0x16, 0x4F, 0x2F, 0x2F, 0xB0, 0x90, 0x32 };  // MSB

void os_getArtEui(u1_t* b) {
  memcpy_P(b, APPEUI, 8);
}
void os_getDevEui(u1_t* b) {
  memcpy_P(b, DEVEUI, 8);
}
void os_getDevKey(u1_t* b) {
  memcpy_P(b, APPKEY, 16);
}

// ---------- LMIC pin map (TTGO LoRa32 V1/V1.3) ----------
const lmic_pinmap lmic_pins = {
  .nss = 18,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 14,
  .dio = { 26, 33, 32 }
};

// ---------- WiFi ----------
const char* ssid = "Chimera";
const char* pass = "Sranklord1";
const char* ssid1 = "SM-Yahya";
const char* pass1 = "ya1234ya";
bool useAlt = true;
WebServer server(80);

// ---------- Hardware ----------
Servo servoLock;
const int SERVO_PIN = 25;
const int LED_G = 13;
const int LED_R = 21;

// IR presence
#define IR_PIN 36
static const int IR_THRESHOLD = 2000;                 // tune per sensor
static const unsigned long PRESENCE_HOLD_MS = 4000;  // keep detected for 4s

// ---------- Auth state ----------
char storedUsername[16] = "admin";
char storedPassword[16] = "admin";

enum AccessState { IDLE,
                   AUTH_SUCCESS,
                   AUTH_FAIL,
                   LOCKED_OUT };
AccessState currentState = IDLE;
unsigned long lockoutStart = 0;
const unsigned long lockoutMs = 120000;
int failCount = 0;

// ---------- Events ----------
enum EventCode : uint8_t { EVT_AUTH_FAIL = 0,
                           EVT_AUTH_OK = 1,
                           EVT_LOCK_OPEN = 2,
                           EVT_LOCK_CLOSED = 3 };

static bool lockIsOpen = false;
static uint32_t lastUserId = 0;


// --- LMIC task ---
TaskHandle_t lmicTaskHandle = nullptr;
// Pin LMIC to core 0, keep web/Wi-Fi on core 1 (Arduino loop runs on core 1).
// Priority lower than Wi-Fi/system. Bigger stack to avoid overflows.
void lmicTask(void*) {
  const TickType_t sleepTicks = pdMS_TO_TICKS(10);
  for (;;) {
    // Run a few LMIC steps, then yield
    uint32_t t0 = millis();
    while (millis() - t0 < 5) os_runloop_once();
    vTaskDelay(sleepTicks);
  }
}
// ---------- Sensor filter (moving average) ----------
static const int NUM_SAMPLES = 10;

int readSensorRaw() {
  return analogRead(IR_PIN);
}

int sensorMovingAverage() {
  static int buf[NUM_SAMPLES];
  static int idx = 0;
  static long sum = 0;
  static bool init = false;

  if (!init) {
    for (int i = 0; i < NUM_SAMPLES; i++) {
      buf[i] = readSensorRaw();
      sum += buf[i];
    }
    init = true;
  }

  sum -= buf[idx];
  buf[idx] = readSensorRaw();
  sum += buf[idx];
  idx = (idx + 1) % NUM_SAMPLES;

  return (int)(sum / NUM_SAMPLES);
}



// ---------- Presence hold ----------
bool presenceHoldUpdate(int filtered) {
  static bool latched = false;
  static unsigned long lastHigh = 0;
  unsigned long now = millis();

  if (filtered > IR_THRESHOLD) {
    latched = true;
    lastHigh = now;
  } else if (latched && (now - lastHigh >= PRESENCE_HOLD_MS)) {
    latched = false;
  }
  return latched;
}

bool isPresenceDetectedHold() {
  return presenceHoldUpdate(sensorMovingAverage());
}

// ---------- Continuous presence output ----------
void presenceStatusTick() {
  static unsigned long t = 0;
  const unsigned long period = 250;
  unsigned long now = millis();

  if (now - t < period) return;
  t = now;

  int filtered = sensorMovingAverage();
  int latched = presenceHoldUpdate(filtered);

  if (debugSensor) {
    Serial.print("PRESENCE_LATCHED=");
    Serial.print(latched);
    Serial.print(" FILTERED=");
    Serial.println(filtered);
  }
}

// ---------- Minimal hardware helpers ----------
void setGreen(bool v) {
  digitalWrite(LED_G, v ? HIGH : LOW);
}
void setRed(bool v) {
  digitalWrite(LED_R, v ? HIGH : LOW);
}

void setServoAngle(int a) {
  servoLock.write(a);  // single attach done in setup
  delay(300);
}


// ---------- LMIC send helpers ----------
static osjob_t sendjob;
static char jsonBuf[64];


static char txq[16][64];
static uint8_t qHead = 0, qTail = 0;

// Backpressure: skip enqueue if the same code/user repeated too fast
bool qPushDedup(const char* s, uint32_t minMs = 1000) {
  static char last[64] = "";
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (strcmp(s, last) == 0 && (now - lastMs) < minMs) return false;

  strncpy(last, s, sizeof(last) - 1);
  last[63] = '\0';
  lastMs = now;

  uint8_t next = (uint8_t)((qHead + 1) & 15);              // 15 for size 16
  if (next == qTail) qTail = (uint8_t)((qTail + 1) & 15);  // drop oldest
  strncpy(txq[qHead], s, sizeof(txq[0]) - 1);
  txq[qHead][sizeof(txq[0]) - 1] = '\0';
  qHead = next;
  return true;
}


inline bool qEmpty() {
  return qHead == qTail;
}
inline void qPush(const char* s) {
  uint8_t next = (uint8_t)((qHead + 1) & 3);
  if (next == qTail) { qTail = (uint8_t)((qTail + 1) & 3); }  // drop oldest
  strncpy(txq[qHead], s, sizeof(txq[0]) - 1);
  txq[qHead][sizeof(txq[0]) - 1] = '\0';
  qHead = next;
}
static void try_send(osjob_t*) {
  if (LMIC.opmode & OP_TXRXPEND) {
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(2), try_send);
    return;
  }
  if (!qEmpty()) {
    const char* p = txq[qTail];
    LMIC_setTxData2(/*port*/ 1, (uint8_t*)p, strlen(p), /*confirmed*/ 0);
    qTail = (uint8_t)((qTail + 1) & 15);  // 15 for size 16
  }
}

// NEW: send_event(code, user)
void send_event(uint8_t code, uint32_t user_id) {
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"field1\":%lu,\"field2\":%u}", (unsigned long)user_id, code);
  if (qPushDedup(buf, 800)) {  // 0.8 s cooldown for identical events
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(1), try_send);
  }
}


void send_auth_event(bool ok, uint32_t user_id) {
  snprintf(jsonBuf, sizeof(jsonBuf), "{\"field1\":%lu,\"field2\":%d}", (unsigned long)user_id, ok ? 1 : 0);

  if (LMIC.opmode & OP_TXRXPEND) {
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(2), try_send);

  } else {
    LMIC_setTxData2(1, (uint8_t*)jsonBuf, strlen(jsonBuf), 0);
  }
}

// ---------- Simple 32-bit user id from username ----------
uint32_t userIdFromName(const String& s) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (size_t i = 0; i < s.length(); ++i) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return h;
}

// ---------- State transitions ----------
void setStateIdle() {
  bool wasOpen = lockIsOpen;

  setGreen(false);
  setRed(false);
  setServoAngle(0);
  currentState = IDLE;

  if (wasOpen) {
    lockIsOpen = false;
    send_event(EVT_LOCK_CLOSED, lastUserId);
  }
}

void setStateAuthSuccess() {
  setGreen(true);
  setRed(false);
  setServoAngle(180);
  failCount = 0;
  currentState = AUTH_SUCCESS;

  if (!lockIsOpen) {
    lockIsOpen = true;
    send_event(EVT_LOCK_OPEN, lastUserId);
  }
}


void setStateAuthFail() {
  setGreen(false);
  setRed(true);
  setServoAngle(0);
  failCount++;
  currentState = (failCount >= 3) ? LOCKED_OUT : IDLE;
  if (currentState == LOCKED_OUT) lockoutStart = millis();
}

void handleLockout() {
  if (currentState == LOCKED_OUT && millis() - lockoutStart >= lockoutMs) {
    failCount = 0;
    setStateIdle();
  }
}

// ---------- Auth helpers ----------
bool checkCreds(const String& u, const String& p) {
  return (u == String(storedUsername) && p == String(storedPassword));
}

// ---------- Web handlers ----------
void handleRoot() {
  if (currentState != AUTH_SUCCESS) {
    server.sendHeader("Location", "/login");
    server.send(301);
    return;
  }

  String html =
    "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>body{font-family:Arial;}input[type=submit]{padding:8px 16px;color:#fff;background:#d00;}</style></head><body>"
    "<h2>Access Granted</h2><p>Lock released.</p>"
    "<form action='/login' method='GET'><input type='hidden' name='DISCONNECT' value='YES'><input type='submit' value='Logout'></form>"
    "<hr><a href='/admin'>Admin</a></body></html>";

  server.send(200, "text/html", html);
}

void handleLogin() {
  String msg;
  bool locked = (currentState == LOCKED_OUT);

  if (server.hasArg("DISCONNECT")) {
    server.sendHeader("Set-Cookie", "ESPSESSIONID=0");
    setStateIdle();  // triggers EVT_LOCK_CLOSED if it was open
    // Optionally clear last user
    // lastUserId = 0;
    msg = "<span class='success'>Logged out.</span>";
  }


  if (server.hasArg("USERNAME") && server.hasArg("PASSWORD") && !locked) {
    String u = server.arg("USERNAME"), p = server.arg("PASSWORD");
    uint32_t uid = userIdFromName(u);
    bool okCreds = checkCreds(u, p);
    bool okPresence = isPresenceDetectedHold();
    bool ok = okCreds && okPresence;

    if (ok) {
      lastUserId = uid;       // track who opened
      setStateAuthSuccess();  // triggers EVT_LOCK_OPEN
      send_event(EVT_AUTH_OK, uid);
      server.sendHeader("Location", "/");
      server.sendHeader("Set-Cookie", "ESPSESSIONID=1");
      server.send(301);
    } else {
      setStateAuthFail();  // stays/returns closed, no lock event
      msg = !okCreds ? "<div class='error'>Wrong username/password.</div>"
                     : "<div class='error'>No presence detected.</div>";
      // Optional: keep auth-result telemetry
      send_event(EVT_AUTH_FAIL, uid);  // already present
    }
  }


  String lockedMsg = locked ? "<div class='error'>Too many failed attempts. Locked for 2 minutes.</div>" : "";

  String html =
    "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>body{font-family:Arial;max-width:400px;margin:auto}input{width:90%;padding:8px;margin:8px auto}"
    ".error{color:#d00}.success{color:#090}</style></head><body>"
    "<h2>Access Control Login</h2>"
    "<form action='/login' method='POST'>"
    "Username: <input name='USERNAME'><br>Password: <input type='password' name='PASSWORD'><br>"
    "<input type='submit' value='Login'></form>"
    + lockedMsg + msg + "<hr><a href='/admin'>Admin</a></body></html>";

  server.send(200, "text/html", html);
}

void handleAdmin() {
  String msg;

  if (server.hasArg("NEWUSER") && server.hasArg("NEWPASS")) {
    String nu = server.arg("NEWUSER"), np = server.arg("NEWPASS");
    if (nu.length() > 0 && np.length() > 0) {
      nu.toCharArray(storedUsername, 16);
      np.toCharArray(storedPassword, 16);
      msg = "<span class='success'>Updated.</span>";
    } else {
      msg = "<div class='error'>Empty fields.</div>";
    }
  }

  String html =
    "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>body{font-family:Arial;max-width:400px;margin:auto}.success{color:#090}.error{color:#d00}</style></head><body>"
    "<h2>Admin</h2><form method='POST'>New Username: <input name='NEWUSER'><br>"
    "New Password: <input type='password' name='NEWPASS'><br><input type='submit' value='Update'></form>"
    + msg + "<hr><a href='/'>Home</a></body></html>";

  server.send(200, "text/html", html);
}

void handleNotFound() {
  server.send(404, "text/html", "<h3>404</h3><a href='/'>Home</a>");
}

// ---------- LMIC events ----------
//Guard against re-joining
volatile bool joined = false;

void onEvent(ev_t ev) {
  switch (ev) {
    case EV_JOINING: Serial.println(F("JOINING")); break;
    case EV_JOINED:
      joined = true;
      LMIC_setLinkCheckMode(0);
      Serial.print(F("JOINED DevAddr="));
      Serial.println(LMIC.devaddr, HEX);
      // immediate test uplink
      qPushDedup("{\"field1\":0,\"field2\":99}", 0);
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(1), try_send);
      break;
    case EV_TXCOMPLETE:
      Serial.println(F("TX DONE"));
      os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(1), try_send);
      break;
    default: break;
  }
  yield();
}


// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_R, OUTPUT);
  analogReadResolution(12);

  // Servo: clean LEDC binding then attach on 25
  servoLock.detach();
  delay(20);
  servoLock.setPeriodHertz(50);
  servoLock.attach(SERVO_PIN, 500, 2400);
  setServoAngle(0);
  setStateIdle();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(useAlt ? ssid1 : ssid, useAlt ? pass1 : pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("accesspanel")) Serial.println("mDNS: accesspanel.local");

  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.on("/admin", handleAdmin);
  server.onNotFound(handleNotFound);
  server.begin();

  pinMode(lmic_pins.nss, OUTPUT);
  digitalWrite(lmic_pins.nss, HIGH);
  SPI.begin(5, 19, 27, 18);

  os_init();
  LMIC_reset();
  LMIC_setClockError(MAX_CLOCK_ERROR * 10 / 100);
#if defined(CFG_us915)
  LMIC_selectSubBand(1);
#endif
  LMIC_setLinkCheckMode(0);
  LMIC_startJoining();

  // Run LMIC on core 1, low priority
  xTaskCreatePinnedToCore(lmicTask, "lmic", 6144, nullptr, 1, &lmicTaskHandle, 1);

  Serial.println(F("Ready"));
}

void loop() {
  presenceStatusTick();
  server.handleClient();
  handleLockout();
  // os_runloop_once();  // REMOVE when LMIC has its own task
  delay(2);
}