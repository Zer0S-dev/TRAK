#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "Config.h"
#include "WiFiManager.h"

constexpr uint8_t MAX_WIFI_PROFILES=3;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS=8000UL;
constexpr uint32_t WIFI_LOSS_CONFIRM_MS=1500UL;
constexpr uint32_t WIFI_RETURN_SCAN_MS=30000UL;
constexpr uint32_t WIFI_SCAN_WATCHDOG_MS=8000UL;
constexpr char PREF_NS[]="trak_wifi";

struct Profile{String ssid;String password;};
Profile profiles[MAX_WIFI_PROFILES];
Preferences prefs;

int activeSlot=-1;
uint32_t wifiLostSince=0,lastInternetCheck=0,lastReturnScan=0,scanStartedAt=0;
bool scanRunning=false,active=false,lastInternetResult=false;

void loadProfiles(){
  prefs.begin(PREF_NS,false);
  for(uint8_t i=0;i<MAX_WIFI_PROFILES;++i){
    profiles[i].ssid=prefs.getString((String("s")+i).c_str(),"");
    profiles[i].password=prefs.getString((String("p")+i).c_str(),"");
    if(profiles[i].ssid.length()>0)
      Serial.printf("[WIFI] Profil #%u charge : %s\n",i+1,profiles[i].ssid.c_str());
  }
}

void saveProfile(uint8_t slot){
  if(slot<MAX_WIFI_PROFILES){
    prefs.putString((String("s")+slot).c_str(),profiles[slot].ssid);
    prefs.putString((String("p")+slot).c_str(),profiles[slot].password);
  }
}

bool validSlot(uint8_t slot){
  return slot<MAX_WIFI_PROFILES&&profiles[slot].ssid.length()>0;
}

int findVisibleSlot(int count){
  if(count<=0)return -1;
  for(uint8_t slot=0;slot<MAX_WIFI_PROFILES;++slot){
    if(!validSlot(slot))continue;
    for(int i=0;i<count;++i){
      if(WiFi.SSID(i)==profiles[slot].ssid){
        Serial.printf("[WIFI] Profil #%u visible : %s\n",slot+1,profiles[slot].ssid.c_str());
        return slot;
      }
    }
  }
  return -1;
}

bool connectSlot(uint8_t slot){
  if(!validSlot(slot))return false;

  Serial.printf("[WIFI] Tentative connexion profil #%u : %s\n",slot+1,profiles[slot].ssid.c_str());

  WiFi.disconnect(false,false);
  vTaskDelay(pdMS_TO_TICKS(50));
  WiFi.begin(profiles[slot].ssid.c_str(),profiles[slot].password.c_str());

  const uint32_t start=millis();
  while(WiFi.status()!=WL_CONNECTED&&millis()-start<WIFI_CONNECT_TIMEOUT_MS)
    vTaskDelay(pdMS_TO_TICKS(100));

  if(WiFi.status()!=WL_CONNECTED){
    Serial.printf("[WIFI] Echec connexion profil #%u : status=%d\n",slot+1,(int)WiFi.status());
    return false;
  }

  activeSlot=slot;
  active=true;
  wifiLostSince=0;
  lastInternetCheck=0;

  Serial.printf("[WIFI] Connecte : %s | IP %s\n",
                profiles[slot].ssid.c_str(),
                WiFi.localIP().toString().c_str());
  return true;
}

void wifiManagerBegin(){
  loadProfiles();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);

  activeSlot=-1;
  active=false;
  wifiLostSince=0;
  lastInternetCheck=0;
  lastReturnScan=millis();
  scanRunning=false;

  Serial.printf("[WIFI] Gestionnaire initialise | profils=%u\n",wifiProfileCount());
}

uint8_t wifiProfileCount(){
  uint8_t c=0;
  for(uint8_t i=0;i<MAX_WIFI_PROFILES;++i)
    if(validSlot(i))++c;
  return c;
}

bool wifiConnectBestSaved(){
  if(wifiProfileCount()==0){
    Serial.println("[WIFI] Aucun profil sauvegarde au demarrage.");
    return false;
  }

  WiFi.mode(WIFI_STA);

  Serial.println("[WIFI] Scan initial demarre...");
  const int count=WiFi.scanNetworks(false,true,false,300);

  if(count<0){
    Serial.printf("[WIFI] Scan initial impossible : resultat=%d\n",count);
    WiFi.scanDelete();
    return false;
  }

  Serial.printf("[WIFI] Scan initial termine : %d reseau(x)\n",count);

  const int slot=findVisibleSlot(count);
  WiFi.scanDelete();

  if(slot<0){
    Serial.println("[WIFI] Aucun profil du dashboard visible au demarrage.");
    return false;
  }

  if(!connectSlot((uint8_t)slot))return false;

  lastInternetResult=true;
  Serial.println("[NET] Wi-Fi prioritaire actif.");
  return true;
}

bool wifiInternetAvailable(){return wifiIsActive();}

bool wifiIsActive(){
  return active&&WiFi.status()==WL_CONNECTED;
}

void startReturnScan(){
  if(scanRunning||wifiProfileCount()==0)return;

  WiFi.mode(WIFI_AP_STA);

  Serial.println("[WIFI] Scan de retour demarre...");

  const int result=WiFi.scanNetworks(true,true,false,300);
  scanStartedAt=millis();

  if(result==WIFI_SCAN_RUNNING){
    scanRunning=true;
    Serial.println("[WIFI] Scan asynchrone en cours...");
    return;
  }

  if(result>=0){
    Serial.printf("[WIFI] Scan de retour termine immediatement : %d reseau(x)\n",result);

    const int slot=findVisibleSlot(result);
    WiFi.scanDelete();

    if(slot>=0){
      if(connectSlot((uint8_t)slot)){
        lastInternetResult=true;
        Serial.println("[NET] Wi-Fi retrouve -> prioritaire sur 4G.");
      }else{
        active=false;
        activeSlot=-1;
        Serial.println("[NET] Wi-Fi visible mais connexion impossible -> 4G conservee.");
      }
    }else{
      Serial.println("[WIFI] Aucun profil sauvegarde visible.");
    }
    return;
  }

  WiFi.scanDelete();
  Serial.printf("[WIFI] Echec lancement scan de retour : resultat=%d\n",result);
}

void finishReturnScan(){
  if(!scanRunning)return;

  const int result=WiFi.scanComplete();

  if(result==WIFI_SCAN_RUNNING){
    if(millis()-scanStartedAt>WIFI_SCAN_WATCHDOG_MS){
      Serial.println("[WIFI] Watchdog scan depasse -> abandon du scan.");
      WiFi.scanDelete();
      scanRunning=false;
    }
    return;
  }

  scanRunning=false;

  if(result<0){
    Serial.printf("[WIFI] Scan de retour termine en erreur : resultat=%d\n",result);
    WiFi.scanDelete();
    return;
  }

  Serial.printf("[WIFI] Scan de retour termine : %d reseau(x)\n",result);

  const int slot=findVisibleSlot(result);
  WiFi.scanDelete();

  if(slot<0){
    Serial.println("[WIFI] Aucun profil sauvegarde visible.");
    return;
  }

  if(connectSlot((uint8_t)slot)){
    active=true;
    lastInternetResult=true;
    Serial.println("[NET] Wi-Fi retrouve -> prioritaire sur 4G.");
    return;
  }

  active=false;
  activeSlot=-1;
  Serial.println("[NET] Wi-Fi visible mais connexion impossible -> 4G conservee.");
}

int wifiSignalPercent(){
  if(WiFi.status()!=WL_CONNECTED)return 0;
  const int rssi=WiFi.RSSI();
  if(rssi<=-100)return 0;
  if(rssi>=-50)return 100;
  return constrain(2*(rssi+100),0,100);
}

void wifiNetworkTick(bool){
  const uint32_t now=millis();

  if(active){
    if(!validSlot((uint8_t)activeSlot)){
      Serial.println("[NET] Profil Wi-Fi actif retire -> recherche Wi-Fi immediate.");
      active=false;
      activeSlot=-1;
      WiFi.disconnect(false,false);
      wifiLostSince=0;
      lastReturnScan=0;
      startReturnScan();
      return;
    }

    if(WiFi.status()!=WL_CONNECTED)wifiLostSince=now;
    else wifiLostSince=0;

    if(wifiLostSince!=0&&now-wifiLostSince>=WIFI_LOSS_CONFIRM_MS){
      Serial.println("[NET] Wi-Fi perdu -> recherche Wi-Fi immediate, sinon 4G.");
      active=false;
      activeSlot=-1;
      WiFi.disconnect(false,false);
      wifiLostSince=0;
      lastReturnScan=0;
      startReturnScan();
    }
    return;
  }

  finishReturnScan();

  if(active)return;

  if(!scanRunning&&now-lastReturnScan>=WIFI_RETURN_SCAN_MS){
    lastReturnScan=now;
    Serial.println("[NET] 4G active -> lancement recherche Wi-Fi periodique.");
    startReturnScan();
  }
}

void wifiSetProfile(uint8_t slot,const char* ssid,const char* password){
  if(slot>=MAX_WIFI_PROFILES)return;

  profiles[slot].ssid=ssid?ssid:"";
  profiles[slot].password=password?password:"";
  saveProfile(slot);

  lastReturnScan=0;

  Serial.printf("[WIFI] Profil #%u enregistre -> recherche Wi-Fi immediate : %s\n",
                slot+1,profiles[slot].ssid.c_str());

  if(!active&&!scanRunning)startReturnScan();
}

void wifiClearProfile(uint8_t slot){
  if(slot>=MAX_WIFI_PROFILES)return;

  profiles[slot].ssid="";
  profiles[slot].password="";
  saveProfile(slot);

  Serial.printf("[WIFI] Profil #%u efface.\n",slot+1);
}

void wifiResetProfiles(){
  for(uint8_t i=0;i<MAX_WIFI_PROFILES;++i){
    profiles[i].ssid="";
    profiles[i].password="";
  }

  // Factory reset: efface les profils dans notre namespace ET les
  // identifiants Wi-Fi que le driver ESP32 pourrait encore conserver.
  prefs.clear();
  prefs.end();

  activeSlot=-1;
  active=false;
  scanRunning=false;
  wifiLostSince=0;
  lastReturnScan=0;

  WiFi.setAutoReconnect(false);
  WiFi.scanDelete();
  WiFi.disconnect(true,true);
  WiFi.mode(WIFI_OFF);

  Serial.println("[WIFI] Profils Wi-Fi et credentials driver effaces.");
}
