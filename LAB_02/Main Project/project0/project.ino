// ESP32 TTGO — BLE AUTH+PREF+CMD + Wi-Fi + Player + Queue + Pref-merge

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_gatts_api.h"

// ===== Pins =====
#define BUZZER_PIN 21

// ===== Wi-Fi =====
const char* WIFI_SSID = "REDACTED_SSID";
const char* WIFI_PASS = "REDACTED_PASSWORD";
bool useStaticIP = false;
IPAddress local_IP(172,30,140,200), gateway(172,30,140,129), subnet(255,255,255,128);

// ===== API =====
static const char* API_BASE = "https://iotjukebox.onrender.com";
static const char* PREF_GET_QS  = "/preference?id=";      // + id + "&key=" + key
static const char* PREF_POST_QS = "/preference?id=";      // + id + "&key=" + key + "&value=" + val
static const char* SONG_QS      = "/song?name=";          // + name

// ===== BLE UUIDs =====
static const char* AUTH_SVC_UUID = "7e6a1000-0000-0000-0000-000000000001";
static const char* AUTH_ID_UUID  = "7e6a1001-0000-0000-0000-000000000001";
static const char* PREF_SVC_UUID = "7e6a2000-0000-0000-0000-000000000001";
static const char* PREF_KEY_UUID = "7e6a2001-0000-0000-0000-000000000001";
static const char* PREF_VAL_UUID = "7e6a2002-0000-0000-0000-000000000001";
static const char* CMD_SVC_UUID  = "7e6a3000-0000-0000-0000-000000000001";
static const char* CMD_UUID      = "7e6a3001-0000-0000-0000-000000000001";

// ===== State =====
String g_studentId, g_prefKey, g_prefVal, g_currSong;
volatile char g_cmd = 0;
bool g_isPlaying = true;

// ===== Queue (default + dynamic append on preference) =====
static const char* DEFAULT_SONGS[] = {"harrypotter","tetris","gameofthrones","jigglypuffsong"};
const int DEFAULT_LEN = sizeof(DEFAULT_SONGS)/sizeof(DEFAULT_SONGS[0]);
const int QUEUE_MAX = 8;
String Q[QUEUE_MAX];
int qLen = 0;
int qIndex = 0;

void initQueue() {
  qLen = min(QUEUE_MAX, DEFAULT_LEN);
  for (int i = 0; i < qLen; ++i) Q[i] = DEFAULT_SONGS[i];
  qIndex = 0;
}
int findInQueue(const String& name) {
  for (int i = 0; i < qLen; ++i) if (Q[i].equalsIgnoreCase(name)) return i;
  return -1;
}
void selectSongByIdx(int idx) {
  if (qLen == 0) return;
  qIndex = (idx % qLen + qLen) % qLen;
  g_currSong = Q[qIndex];
  g_isPlaying = true;
}
void appendIfMissing(const String& name) {              // O(qLen), no dup, append or replace oldest
  if (name.isEmpty()) return;
  if (findInQueue(name) >= 0) return;
  if (qLen < QUEUE_MAX) Q[qLen++] = name;
  else { // replace the next slot after current to keep rotation natural
    int pos = (qIndex + 1) % qLen;
    Q[pos] = name;
  }
}
void integratePreferenceSong(const String& name) {      // select pref; append if absent
  appendIfMissing(name);
  int j = findInQueue(name);
  if (j < 0 && qLen > 0) j = qIndex; // fallback
  if (j >= 0) selectSongByIdx(j);
}

// ===== Song buffer =====
#define MELODY_MAX 256
struct SongData { int frequencies[MELODY_MAX]; int durations[MELODY_MAX]; int tempo; int noteCount; };
SongData g_song; bool g_songLoaded = false;

// ===== Wi-Fi =====
void wifiConnect() {
  if (useStaticIP) WiFi.config(local_IP, gateway, subnet);
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi connecting to %s\n", WIFI_SSID);
  uint32_t t0 = millis(); while (WiFi.status()!=WL_CONNECTED && millis()-t0<15000){ delay(200); Serial.print("."); }
  Serial.println(); if (WiFi.status()==WL_CONNECTED) Serial.printf("WiFi OK IP=%s\n", WiFi.localIP().toString().c_str()); else Serial.println("WiFi failed");
}

// ===== HTTP =====
bool httpGET(const String& path, String& body, int& code) {
  body=""; code=-1; WiFiClientSecure c; c.setInsecure(); HTTPClient h;
  String url = String(API_BASE)+path; if(!h.begin(c,url)){ Serial.println("HTTP begin failed"); return false; }
  h.setConnectTimeout(15000); code=h.GET(); if(code>0) body=h.getString(); h.end();
  Serial.printf("GET %s -> %d\n", url.c_str(), code); return code>0;
}
bool httpPOST(const String& path, int& code) {
  code=-1; WiFiClientSecure c; c.setInsecure(); HTTPClient h;
  String url = String(API_BASE)+path; if(!h.begin(c,url)){ Serial.println("HTTP begin failed"); return false; }
  h.setConnectTimeout(15000); code=h.POST((uint8_t*)nullptr,0); h.end();
  Serial.printf("POST %s -> %d\n", url.c_str(), code); return code>0;
}

// ===== Preference resolve (GET; on error POST then GET) =====
bool resolvePreference(String& outSong) {
  outSong=""; if(g_studentId.isEmpty()||g_prefKey.isEmpty()){ Serial.println("Pref resolve: missing studentId/key"); return false; }
  String getPath = String(PREF_GET_QS)+g_studentId+"&key="+g_prefKey;
  String body; int code; if(!httpGET(getPath, body, code)) return false;
  if (code==200) {
    DynamicJsonDocument doc(256); auto err=deserializeJson(doc,body);
    if(!err && doc["name"].is<const char*>()){ outSong=String(doc["name"].as<const char*>()); Serial.printf("Pref GET ok: %s -> '%s'\n", g_prefKey.c_str(), outSong.c_str()); return true; }
    Serial.println("Pref GET parse fail"); return false;
  }
  if (!g_prefVal.isEmpty()) {
    String postPath = String(PREF_POST_QS)+g_studentId+"&key="+g_prefKey+"&value="+g_prefVal;
    int pcode; if (httpPOST(postPath,pcode) && pcode>=200 && pcode<400) {
      if (httpGET(getPath, body, code) && code==200) {
        DynamicJsonDocument doc(256); auto err=deserializeJson(doc,body);
        if(!err && doc["name"].is<const char*>()){ outSong=String(doc["name"].as<const char*>()); Serial.printf("Pref POST+GET ok: %s -> '%s'\n", g_prefKey.c_str(), outSong.c_str()); return true; }
      }
    } else { Serial.printf("Pref POST failed code=%d\n", pcode); }
  } else { Serial.println("No PREF/VAL set. Write a song name first."); }
  return false;
}

// ===== /song parser =====
bool parseSongJson(const String& json, SongData* s) {
  s->noteCount=0; DynamicJsonDocument doc(8192); if(deserializeJson(doc,json)){ Serial.println("JSON parse error"); return false; }
  if(!doc["tempo"].is<int>() || !doc["melody"].is<JsonArray>()){ Serial.println("JSON missing fields"); return false; }
  s->tempo = doc["tempo"].as<int>(); JsonArray arr = doc["melody"].as<JsonArray>(); int n=0;
  for(size_t i=0; i+1<arr.size() && n<MELODY_MAX; i+=2){ s->frequencies[n]=arr[i].as<int>(); s->durations[n]=arr[i+1].as<int>(); n++; }
  s->noteCount=n; Serial.printf("Parsed song: notes=%d tempo=%d\n", n, s->tempo); return n>0;
}
bool loadSongByName(const String& name, SongData* out) {
  if (name.isEmpty()) return false; String body; int code;
  if (!httpGET(String(SONG_QS)+name, body, code)) return false;
  if (code!=200){ Serial.printf("Song GET failed: %d\n", code); return false; }
  return parseSongJson(body, out);
}

// ===== Player =====
char fetchCmdAndClear(){ char c=g_cmd; if(c!=0){ g_cmd=0; Serial.printf("CMD (player): %c\n", c);} return c; }
char playSong(SongData* s){
  if(s->noteCount<=0||s->tempo<=0) return 0;
  for(int i=0;i<s->noteCount;++i){
    char c=fetchCmdAndClear();
    if(c=='S'){ noTone(BUZZER_PIN); g_isPlaying=false; return 'S'; }
    if(c=='P'){ g_isPlaying=!g_isPlaying; if(!g_isPlaying){ noTone(BUZZER_PIN); return 'P'; }}
    if(c=='N'||c=='B'){ noTone(BUZZER_PIN); return c; }
    if(!g_isPlaying) return 'P';
    int freq=s->frequencies[i], beat=s->durations[i];
    int durMs = abs(beat)*(60000/s->tempo)/4;
    if(freq>0) tone(BUZZER_PIN, freq, durMs);
    unsigned long t=millis(); while(millis()-t < (unsigned long)(durMs*1.3)){
      char k=fetchCmdAndClear();
      if(k=='S'){ noTone(BUZZER_PIN); g_isPlaying=false; return 'S'; }
      if(k=='P'){ g_isPlaying=!g_isPlaying; if(!g_isPlaying){ noTone(BUZZER_PIN); return 'P'; }}
      if(k=='N'||k=='B'){ noTone(BUZZER_PIN); return k; }
      delay(5);
    }
    noTone(BUZZER_PIN);
  }
  return 0; // finished
}

// ===== BLE callbacks =====
class ConnCB: public BLEServerCallbacks{
  void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* p) override {
    const uint8_t* a=p->connect.remote_bda; char mac[18]; sprintf(mac,"%02X:%02X:%02X:%02X:%02X:%02X",a[0],a[1],a[2],a[3],a[4],a[5]);
    Serial.printf("BLE connected: %s\n", mac);
  }
  void onDisconnect(BLEServer* s) override { Serial.println("BLE disconnected. Advertising..."); s->getAdvertising()->start(); }
};
class AuthCB: public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* c) override { String s=String(c->getValue().c_str()); g_studentId=s; Serial.printf("AUTH studentId='%s'\n", g_studentId.c_str()); }
};
class PrefKeyCB: public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* c) override {
    String s=String(c->getValue().c_str()); g_prefKey=s; Serial.printf("PREF key='%s'\n", g_prefKey.c_str());
    if(WiFi.status()==WL_CONNECTED){ if(resolvePreference(g_currSong)){ integratePreferenceSong(g_currSong); g_songLoaded=false; } }
  }
};
class PrefValCB: public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* c) override { String s=String(c->getValue().c_str()); g_prefVal=s; Serial.printf("PREF val='%s'\n", g_prefVal.c_str()); }
};
class CmdCB: public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* c) override {
    String s=String(c->getValue().c_str()); if(!s.length()) return; g_cmd=s[0];
    if(g_cmd=='P') Serial.println("CMD: PLAY/PAUSE");
    else if(g_cmd=='N') Serial.println("CMD: NEXT");
    else if(g_cmd=='B') Serial.println("CMD: PREV");
    else if(g_cmd=='S') Serial.println("CMD: STOP");
    else Serial.printf("CMD unknown '%c'\n", g_cmd);
  }
};

// ===== BLE setup =====
void setupBLE(){
  BLEDevice::init("TTGO_Jukebox");
  BLEServer* srv=BLEDevice::createServer(); srv->setCallbacks(new ConnCB());

  BLEService* auth=srv->createService(AUTH_SVC_UUID);
  BLECharacteristic* authId=auth->createCharacteristic(AUTH_ID_UUID, BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE);
  authId->addDescriptor(new BLE2902()); authId->setCallbacks(new AuthCB()); authId->setValue("WRITE_STUDENT_ID"); auth->start();

  BLEService* pref=srv->createService(PREF_SVC_UUID);
  BLECharacteristic* prefKey=pref->createCharacteristic(PREF_KEY_UUID, BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE);
  BLECharacteristic* prefVal=pref->createCharacteristic(PREF_VAL_UUID, BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE);
  prefKey->addDescriptor(new BLE2902()); prefVal->addDescriptor(new BLE2902());
  prefKey->setCallbacks(new PrefKeyCB()); prefVal->setCallbacks(new PrefValCB());
  prefKey->setValue("WRITE_DEVICE_NAME_KEY"); prefVal->setValue("WRITE_SONG_NAME"); pref->start();

  BLEService* cmd=srv->createService(CMD_SVC_UUID);
  BLECharacteristic* cmdChar=cmd->createCharacteristic(CMD_UUID, BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_READ);
  cmdChar->addDescriptor(new BLE2902()); cmdChar->setCallbacks(new CmdCB()); cmdChar->setValue("P|N|B|S"); cmd->start();

  srv->getAdvertising()->start(); Serial.println("BLE advertising");
}

// ===== Arduino =====
void setup(){
  Serial.begin(115200); delay(200);
  initQueue();                     // default queue
  wifiConnect();
  pinMode(BUZZER_PIN, OUTPUT);
  setupBLE();
  selectSongByIdx(0);              // start from first queue item
}
void loop(){
  // lazy-load selected song
  if(!g_currSong.isEmpty()){
    if(!g_songLoaded){
      Serial.printf("Loading song '%s'\n", g_currSong.c_str());
      g_songLoaded = loadSongByName(g_currSong, &g_song);
      Serial.println(g_songLoaded? "Load ok":"Load failed");
    }
    if(g_songLoaded && g_isPlaying){
      char r=playSong(&g_song);
      if(r=='N') { selectSongByIdx(qIndex+1); g_songLoaded=false; }
      else if(r=='B') { selectSongByIdx(qIndex-1); g_songLoaded=false; }
      else if(r==0) {  // finished -> auto-next
        selectSongByIdx(qIndex+1); g_songLoaded=false;
      }
    }
  }
  delay(20);
}
