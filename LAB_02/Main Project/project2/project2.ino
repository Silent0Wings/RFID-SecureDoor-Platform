// ===== IoT Jukebox — Bonded BLE + Classic BT + Wi-Fi Preference Fetch (Persistent Bond) =====

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include <WiFiClientSecure.h>

// BLE
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"

#define BUZZER_PIN 21
const char* ssid = "REDACTED_SSID";
const char* password = "REDACTED_PASSWORD";
const char* bNames = "IoT_Jukebox";
static const char* API_BASE = "https://iotjukebox.onrender.com";

// ==== Queue ====
#define QUEUE_SIZE 5
const char* songNames[] = { "harrypotter", "jigglypuffsong", "tetris", "gameofthrones" };
String songQueue[QUEUE_SIZE];
int queueStart = 0, queueCount = 0, queuePos = 0;

// ==== Song Struct ====
#define MELODY_MAX 128
struct SongData { int frequencies[MELODY_MAX]; int durations[MELODY_MAX]; int tempo; int noteCount; };
SongData currentSong;
int lastLoadedIdx = -1; bool songLoaded = false;

// ==== Bluetooth ====
BluetoothSerial SerialBT;
volatile char btCmd = 0; bool isPlaying = true;

// ==== BLE State ====
String g_userId = ""; String g_prefKey = "IoT_Jukebox";
volatile char g_bleCmd = 0; volatile bool g_fetchPrefPending = false;

// ==== UUIDs ====
static const char* AUTH_SVC_UUID = "7e6a1000-0000-0000-0000-000000000001";
static const char* AUTH_ID_UUID  = "7e6a1001-0000-0000-0000-000000000001";
static const char* CMD_SVC_UUID  = "7e6a3000-0000-0000-0000-000000000001";
static const char* CMD_UUID      = "7e6a3001-0000-0000-0000-000000000001";

// ==== Utils ====
String encodeIdForUrl(String id){ id.replace(":", "%3A"); return id; }
String pickRandomSong(){ size_t n = sizeof(songNames)/sizeof(songNames[0]); return String(songNames[random(n)]); }

void initQueue(){ for(int i=0;i<QUEUE_SIZE;i++) songQueue[i]=songNames[i%4]; queueCount=QUEUE_SIZE; }
String getCurrentSong(){ return songQueue[(queueStart+queuePos)%queueCount]; }

bool selectSongByName(const String& name){
  for(int i=0;i<queueCount;i++){
    if(songQueue[i].equalsIgnoreCase(name)){ queuePos=i; songLoaded=false; isPlaying=true; return true; }
  }
  songQueue[queuePos]=name; songLoaded=false; isPlaying=true; return true;
}

// ==== Parser ====
bool parseMelodyFromJson(const String& json, SongData* s){
  DynamicJsonDocument doc(8192);
  if(deserializeJson(doc,json)) return false;
  s->tempo=doc["tempo"].as<int>();
  JsonArray arr=doc["melody"].as<JsonArray>(); int n=0;
  for(size_t i=0;i+1<arr.size() && n<MELODY_MAX;i+=2){
    s->frequencies[n]=arr[i].as<int>(); s->durations[n]=arr[i+1].as<int>(); n++;
  }
  s->noteCount=n; return n>0;
}

// ==== Player ====
char interupt_Command=0;
void handlePlayPause(){ isPlaying=!isPlaying; Serial.println(isPlaying?"Playing":"Paused"); }
void handleNextSong(){ queuePos=(queuePos+1)%queueCount; songLoaded=false; }
void handlePreviousSong(){ queuePos=(queuePos-1+queueCount)%queueCount; songLoaded=false; }

bool playSong(SongData* s){
  for(int i=0;i<s->noteCount;++i){
    if(g_bleCmd!=0){ btCmd=g_bleCmd; g_bleCmd=0; }
    int f=s->frequencies[i], b=s->durations[i];
    int dur=abs(b)*(60000/s->tempo)/4; if(f>0) tone(BUZZER_PIN,f,dur);
    unsigned long start=millis();
    while(millis()-start<(unsigned long)(dur*1.3)){
      if(SerialBT.available()||btCmd!=0){
        if(btCmd==0) btCmd=SerialBT.read();
        if(btCmd=='P'){ noTone(BUZZER_PIN); handlePlayPause(); btCmd=0; return 'P'; }
        if(btCmd=='N'){ noTone(BUZZER_PIN); handleNextSong(); btCmd=0; return 'N'; }
        if(btCmd=='B'){ noTone(BUZZER_PIN); handlePreviousSong(); btCmd=0; return 'B'; }
        btCmd=0;
      }
      delay(5);
    }
    noTone(BUZZER_PIN);
  }
  return 0;
}

// ==== HTTPS ====
bool httpsGET(const String& url,int& code,String& body){
  WiFiClientSecure c; c.setInsecure(); HTTPClient h;
  if(!h.begin(c,url)) return false; h.setConnectTimeout(15000);
  code=h.GET(); if(code>0) body=h.getString(); h.end(); return true;
}
bool httpsPOSTForm(const String& url,const String& data,int& code,String& body){
  WiFiClientSecure c; c.setInsecure(); HTTPClient h;
  if(!h.begin(c,url)) return false; h.addHeader("Content-Type","application/x-www-form-urlencoded");
  code=h.POST(data); if(code>0) body=h.getString(); h.end(); return true;
}

// ==== Preference GET/POST ====
bool getPreferenceSong(const String& id,const String& key,String& out){
  out=""; int code=-1; String body;
  String url=String(API_BASE)+"/preference?id="+encodeIdForUrl(id)+"&key="+key;
  if(!httpsGET(url,code,body)) return false; if(code!=200) return false;
  DynamicJsonDocument doc(512); if(deserializeJson(doc,body)) return false;
  if(doc["name"].is<const char*>()){ out=doc["name"].as<String>(); return true; }
  return false;
}
bool resolvePreferenceWithFallback(const String& id,const String& key,String& out){
  if(getPreferenceSong(id,key,out)) return true;
  String chosen=pickRandomSong();
  String postUrl=String(API_BASE)+"/preference";
  String form="id="+encodeIdForUrl(id)+"&key="+key+"&value="+chosen;
  int code=-1; String resp;
  if(!httpsPOSTForm(postUrl,form,code,resp)) return false;
  if(code<200||code>=400) return false;
  return getPreferenceSong(id,key,out);
}

// ==== Loader ====
bool loadSongByIndex(int idx,SongData* out){
  String url=String(API_BASE)+"/song?name="+getCurrentSong();
  SerialBT.end(); delay(50);
  WiFiClientSecure c; c.setInsecure(); HTTPClient h;
  if(!h.begin(c,url)){ SerialBT.begin(bNames,true); return false; }
  int code=h.GET(); String p=""; if(code>0)p=h.getString();
  if(code==200) parseMelodyFromJson(p,out);
  h.end(); SerialBT.begin(bNames,true); delay(50);
  return (code==200&&out->noteCount>0);
}

// ==== BLE Callbacks ====
class ConnCB:public BLEServerCallbacks{
  void onConnect(BLEServer* s,esp_ble_gatts_cb_param_t* p) override{
    Serial.println("BLE connected"); SerialBT.end();
  }
  void onDisconnect(BLEServer* s) override{
    Serial.println("BLE disconnected, advertising again");
    SerialBT.begin(bNames,true); s->getAdvertising()->start();
  }
};
class AuthCB:public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* c) override{
    g_userId=String(c->getValue().c_str()); g_fetchPrefPending=true;
  }
};
class CmdCB:public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* c) override{
    String s=String(c->getValue().c_str()); if(!s.length())return;
    g_bleCmd=s[0];
  }
};

// ==== Security ====
class MySecurity:public BLESecurityCallbacks{
 public:
  uint32_t onPassKeyRequest()override{return 123456;}
  void onPassKeyNotify(uint32_t pk)override{Serial.printf("PassKey: %06u\n",pk);}
  bool onSecurityRequest()override{return true;}
  bool onConfirmPIN(uint32_t)override{return true;}
  void onAuthenticationComplete(esp_ble_auth_cmpl_t c)override{
    Serial.printf("Auth %s\n",c.success?"OK":"FAIL");
  }
};

// ==== BLE Setup ====
void setupBLE(){
  BLEDevice::init("TTGO_Jukebox");
  BLESecurity* sec=new BLESecurity();
  BLEDevice::setSecurityCallbacks(new MySecurity());
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec->setCapability(ESP_IO_CAP_OUT);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK|ESP_BLE_ID_KEY_MASK);
  uint32_t passkey=123456;
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY,&passkey,sizeof(passkey));

  BLEServer* srv=BLEDevice::createServer(); srv->setCallbacks(new ConnCB());
  BLEService* auth=srv->createService(AUTH_SVC_UUID);
  BLECharacteristic* authId=auth->createCharacteristic(AUTH_ID_UUID,BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE);
  authId->addDescriptor(new BLE2902()); authId->setCallbacks(new AuthCB());
  authId->setValue("WRITE_USER_ID");
  authId->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED|ESP_GATT_PERM_WRITE_ENCRYPTED);
  auth->start();

  BLEService* cmd=srv->createService(CMD_SVC_UUID);
  BLECharacteristic* cmdChar=cmd->createCharacteristic(CMD_UUID,BLECharacteristic::PROPERTY_READ|BLECharacteristic::PROPERTY_WRITE);
  cmdChar->addDescriptor(new BLE2902()); cmdChar->setCallbacks(new CmdCB());
  cmdChar->setValue("P|N|B");
  cmdChar->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED|ESP_GATT_PERM_WRITE_ENCRYPTED);
  cmd->start();

  srv->getAdvertising()->start();
  Serial.println("BLE advertising (bonded, passkey 123456)");
}

// ==== Setup ====
void setup(){
  initQueue(); randomSeed(esp_timer_get_time());
  Serial.begin(115200);
  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED){delay(500);Serial.print(".");}
  Serial.println("\nWiFi connected"); Serial.println(WiFi.localIP());

  SerialBT.setPin("123456",6);
  SerialBT.begin(bNames,true);
  Serial.println("Classic BT started (PIN 123456)");

  setupBLE();
}

// ==== Loop ====
void loop(){
  if(g_bleCmd!=0){btCmd=g_bleCmd;g_bleCmd=0;}
  if(g_fetchPrefPending&&WiFi.status()==WL_CONNECTED&&g_userId.length()>0){
    g_fetchPrefPending=false; String pref;
    if(resolvePreferenceWithFallback(g_userId,g_prefKey,pref)){
      Serial.printf("Preference applied: %s\n",pref.c_str()); selectSongByName(pref);
    }
  }
  if(SerialBT.available()||btCmd!=0){
    if(interupt_Command!=0){btCmd=interupt_Command;interupt_Command=0;}
    if(btCmd==0)btCmd=SerialBT.read();
    if(btCmd=='P')handlePlayPause(); else if(btCmd=='N')handleNextSong(); else if(btCmd=='B')handlePreviousSong();
    btCmd=0;
  }
  if(isPlaying&&WiFi.status()==WL_CONNECTED){
    if(!songLoaded||lastLoadedIdx!=queuePos){
      songLoaded=loadSongByIndex(queuePos,&currentSong);
      lastLoadedIdx=songLoaded?queuePos:-1;
    }
    if(songLoaded)interupt_Command=playSong(&currentSong);
  }
  delay(20);
}
