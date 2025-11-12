// IoT Jukebox — Bonded BLE (MAC as ID) + Classic BT + Wi-Fi Preference
// Immediate passkey prompt via delayed esp_ble_set_encryption after connect

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include <WiFiClientSecure.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "esp_gatts_api.h"
#include "esp_gap_ble_api.h"

// ===== CONFIG =====
#define BUZZER_PIN 21
const char* ssid     = "Chimera";
const char* password = "Sranklord1";
const char* bNames   = "IoT_Jukebox";
static const char* API_BASE = "https://iotjukebox.onrender.com";

// ===== Queue =====
#define QUEUE_SIZE 5
const char* songNames[] = { "harrypotter","jigglypuffsong","tetris","gameofthrones" };
String songQueue[QUEUE_SIZE];
int queueStart=0, queueCount=0, queuePos=0;

// ===== Song =====
#define MELODY_MAX 128
struct SongData { int frequencies[MELODY_MAX]; int durations[MELODY_MAX]; int tempo; int noteCount; };
SongData currentSong;
int lastLoadedIdx=-1; bool songLoaded=false;

// ===== Classic BT (SPP) =====
BluetoothSerial SerialBT;
volatile char btCmd=0; bool isPlaying=true;

// ===== BLE (MAC-based, force pairing after connect) =====
String g_deviceMAC="";
String g_prefKey="IoT_Jukebox";
volatile char g_bleCmd=0;
volatile bool g_prefFetchRequest=false;

bool g_reqEncrypt=false, g_bonded=false;
uint8_t g_peer[6]={0};
uint32_t g_connTick=0;

// Only CMD service
static const char* CMD_SVC_UUID = "7e6a3000-0000-0000-0000-000000000001";
static const char* CMD_UUID     = "7e6a3001-0000-0000-0000-000000000001";

// ===== Utils =====
String encodeIdForUrl(String id){ id.replace(":", "%3A"); return id; }
String pickRandomSong(){ size_t n=sizeof(songNames)/sizeof(songNames[0]); return String(songNames[random(n)]); }
void   initQueue(){ for(int i=0;i<QUEUE_SIZE;i++) songQueue[i]=songNames[i%4]; queueCount=QUEUE_SIZE; }
String getCurrentSong(){ return songQueue[(queueStart+queuePos)%queueCount]; }
bool   selectSongByName(const String& name){
  for(int i=0;i<queueCount;i++) if(songQueue[i].equalsIgnoreCase(name)){ queuePos=i; songLoaded=false; isPlaying=true; return true; }
  songQueue[queuePos]=name; songLoaded=false; isPlaying=true; return true;
}

// ===== Parser =====
bool parseMelodyFromJson(const String& json, SongData* s){
  DynamicJsonDocument doc(8192);
  if(deserializeJson(doc,json)) return false;
  if(!doc["tempo"].is<int>() || !doc["melody"].is<JsonArray>()) return false;
  s->tempo = doc["tempo"].as<int>();
  JsonArray arr = doc["melody"].as<JsonArray>();
  int n=0;
  for(size_t i=0;i+1<arr.size() && n<MELODY_MAX; i+=2){
    s->frequencies[n] = arr[i].as<int>();
    s->durations[n]   = arr[i+1].as<int>();
    n++;
  }
  s->noteCount = n;
  return n>0;
}

// ===== Player =====
char interupt_Command=0;
void handlePlayPause(){ isPlaying=!isPlaying; }
void handleNextSong(){ queuePos=(queuePos+1)%queueCount; songLoaded=false; }
void handlePreviousSong(){ queuePos=(queuePos-1+queueCount)%queueCount; songLoaded=false; }

bool playSong(SongData* s){
  if(!s || s->noteCount<=0 || s->tempo<=0) return 0;
  for(int i=0;i<s->noteCount;++i){
    if(g_bleCmd){ btCmd=g_bleCmd; g_bleCmd=0; }
    int f=s->frequencies[i], b=s->durations[i];
    int dur=abs(b)*(60000/s->tempo)/4;
    if(f>0) tone(BUZZER_PIN,f,dur);
    unsigned long t=millis();
    while(millis()-t<(unsigned long)(dur*13/10)){
      if(SerialBT.available()||btCmd){
        if(btCmd==0) btCmd=SerialBT.read();
        char c=btCmd; btCmd=0;
        if(c=='P'){ noTone(BUZZER_PIN); handlePlayPause(); return 'P'; }
        if(c=='N'){ noTone(BUZZER_PIN); handleNextSong(); return 'N'; }
        if(c=='B'){ noTone(BUZZER_PIN); handlePreviousSong(); return 'B'; }
      }
      delay(3);
      yield();
    }
    noTone(BUZZER_PIN);
  }
  return 0;
}

// ===== HTTPS =====
bool httpsGET(const String& url,int& code,String& body){
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h; h.setConnectTimeout(15000); h.setReuse(false);
  if(!h.begin(c,url)) return false;
  code=h.GET(); if(code>0) body=h.getString();
  h.end(); return true;
}
bool httpsPOSTForm(const String& url,const String& data,int& code,String& body){
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h; h.setConnectTimeout(15000); h.setReuse(false);
  if(!h.begin(c,url)) return false;
  h.addHeader("Content-Type","application/x-www-form-urlencoded");
  code=h.POST(data); if(code>0) body=h.getString();
  h.end(); return true;
}

// ===== Preference (id = MAC) =====
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

// ===== Song Loader =====
bool loadSongByIndex(int idx,SongData* out){
  String url=String(API_BASE)+"/song?name="+getCurrentSong();
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h; h.setConnectTimeout(15000); h.setReuse(false);
  if(!h.begin(c,url)) return false;
  int code=h.GET(); String p=""; if(code>0) p=h.getString();
  if(code==200) parseMelodyFromJson(p,out);
  h.end();
  return (code==200 && out->noteCount>0);
}

// ===== BLE Callbacks =====
class ConnCB:public BLEServerCallbacks{
  void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* p) override {
    char macBuf[18];
    sprintf(macBuf,"%02X:%02X:%02X:%02X:%02X:%02X",
            p->connect.remote_bda[0],p->connect.remote_bda[1],p->connect.remote_bda[2],
            p->connect.remote_bda[3],p->connect.remote_bda[4],p->connect.remote_bda[5]);
    g_deviceMAC = String(macBuf); g_deviceMAC.toLowerCase();
    memcpy(g_peer, p->connect.remote_bda, 6);
    g_connTick = millis();
    g_reqEncrypt = true;              // delayed pairing request
    g_bonded = false;
    g_prefFetchRequest = true;        // will run after bonded
  }
  void onDisconnect(BLEServer* s) override {
    s->getAdvertising()->start();
  }
};
class CmdCB:public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    if(v.length()==0) return;
    g_bleCmd = v[0];
  }
};

// ===== Security =====
class MySecurity:public BLESecurityCallbacks{
 public:
  uint32_t onPassKeyRequest() override { return 123456; }
  void onPassKeyNotify(uint32_t pk) override { Serial.printf("PassKey: %06u\n", pk); }
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t c) override {
    g_bonded = c.success;
    Serial.printf("Auth %s\n", c.success ? "OK" : "FAIL");
  }
};

// ===== BLE Setup =====
void setupBLE(){
  BLEDevice::init("TTGO_Jukebox");

  BLESecurity* sec=new BLESecurity();
  BLEDevice::setSecurityCallbacks(new MySecurity());
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec->setCapability(ESP_IO_CAP_OUT);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK|ESP_BLE_ID_KEY_MASK);
  uint32_t passkey=123456;
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY,&passkey,sizeof(passkey));

  BLEServer* srv=BLEDevice::createServer();
  srv->setCallbacks(new ConnCB());

  BLEService* cmd=srv->createService(CMD_SVC_UUID);
  BLECharacteristic* cmdChar=cmd->createCharacteristic(
      CMD_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  cmdChar->addDescriptor(new BLE2902());
  cmdChar->setCallbacks(new CmdCB());
  cmdChar->setValue("P|N|B");
  cmdChar->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  cmd->start();

  srv->getAdvertising()->start();
  Serial.println("BLE advertising (bonded, passkey 123456)");
}

// ===== Setup =====
void setup(){
  initQueue(); randomSeed(esp_timer_get_time());
  Serial.begin(115200);

  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED){ delay(300); }

  SerialBT.setPin("123456",6);
  SerialBT.begin(bNames,true);

  setupBLE();
}

// ===== Loop =====
void loop(){
  // delayed force pairing like the simple example
  if (g_reqEncrypt && millis() - g_connTick > 800) {
    g_reqEncrypt = false;
    esp_ble_set_encryption(g_peer, ESP_BLE_SEC_ENCRYPT_MITM);
  }

  // preference fetch only after bonding
  if(g_prefFetchRequest && g_bonded && WiFi.status()==WL_CONNECTED && g_deviceMAC.length()>0){
    g_prefFetchRequest=false;
    String pref;
    if(resolvePreferenceWithFallback(g_deviceMAC, g_prefKey, pref)){
      selectSongByName(pref);
    }
  }

  // merge BLE cmd into SPP path
  if(g_bleCmd){ btCmd=g_bleCmd; g_bleCmd=0; }

  if(SerialBT.available()||btCmd){
    if(interupt_Command){ btCmd=interupt_Command; interupt_Command=0; }
    if(btCmd==0) btCmd=SerialBT.read();
    if(btCmd=='P') handlePlayPause();
    else if(btCmd=='N') handleNextSong();
    else if(btCmd=='B') handlePreviousSong();
    btCmd=0;
  }

  if(isPlaying && WiFi.status()==WL_CONNECTED){
    if(!songLoaded || lastLoadedIdx!=queuePos){
      songLoaded = loadSongByIndex(queuePos,&currentSong);
      lastLoadedIdx = songLoaded ? queuePos : -1;
    }
    if(songLoaded) interupt_Command = playSong(&currentSong);
  }

  delay(20);
}
