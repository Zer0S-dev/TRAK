#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "Config.h"
#include "TrakConfig.h"
#include "WiFiManager.h"

namespace {
constexpr uint8_t MAX_WIFI_PROFILES=3;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS=8000UL;
constexpr uint32_t WIFI_LOSS_CONFIRM_MS=1500UL;
constexpr uint32_t WIFI_INTERNET_CHECK_MS=3000UL;
constexpr uint32_t WIFI_RETURN_SCAN_MS=30000UL;
constexpr uint32_t WIFI_SCAN_WATCHDOG_MS=8000UL;
constexpr char PREF_NS[]="trak_wifi";
constexpr char WIFI_API_PATH[]="api/wifi/";
struct Profile{String ssid;String password;};
Profile profiles[MAX_WIFI_PROFILES]; Preferences prefs;
int activeSlot=-1; uint32_t wifiLostSince=0,lastInternetCheck=0,lastReturnScan=0,scanStartedAt=0; bool scanRunning=false,active=false,lastInternetResult=false;
void loadProfiles(){prefs.begin(PREF_NS,false);for(uint8_t i=0;i<MAX_WIFI_PROFILES;++i){profiles[i].ssid=prefs.getString((String("s")+i).c_str(),"");profiles[i].password=prefs.getString((String("p")+i).c_str(),"");}}
void saveProfile(uint8_t slot){if(slot<MAX_WIFI_PROFILES){prefs.putString((String("s")+slot).c_str(),profiles[slot].ssid);prefs.putString((String("p")+slot).c_str(),profiles[slot].password);}}
bool validSlot(uint8_t slot){return slot<MAX_WIFI_PROFILES&&profiles[slot].ssid.length()>0;}
int findVisibleSlot(int count){if(count<=0)return -1;for(uint8_t slot=0;slot<MAX_WIFI_PROFILES;++slot){if(!validSlot(slot))continue;for(int i=0;i<count;++i)if(WiFi.SSID(i)==profiles[slot].ssid)return slot;}return -1;}
bool connectSlot(uint8_t slot){if(!validSlot(slot))return false;Serial.printf("[WIFI] Connexion profil #%u : %s\n",slot+1,profiles[slot].ssid.c_str());WiFi.disconnect(false,false);vTaskDelay(pdMS_TO_TICKS(50));WiFi.begin(profiles[slot].ssid.c_str(),profiles[slot].password.c_str());const uint32_t start=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-start<WIFI_CONNECT_TIMEOUT_MS)vTaskDelay(pdMS_TO_TICKS(100));if(WiFi.status()!=WL_CONNECTED){Serial.printf("[WIFI] Echec profil #%u.\n",slot+1);return false;}activeSlot=slot;active=true;wifiLostSince=0;lastInternetCheck=0;Serial.printf("[WIFI] Connecte : %s | IP %s\n",profiles[slot].ssid.c_str(),WiFi.localIP().toString().c_str());return true;}
bool httpReachable(){if(WiFi.status()!=WL_CONNECTED)return false;IPAddress ip;if(WiFi.hostByName("surlereservoir.fr",ip)!=1)return false;WiFiClient client;client.setTimeout(1200);bool ok=client.connect(ip,443);client.stop();return ok;}
void startReturnScan(){if(scanRunning||wifiProfileCount()==0)return;WiFi.mode(WIFI_AP_STA);const int result=WiFi.scanNetworks(true,true,false,300);scanStartedAt=millis();if(result==WIFI_SCAN_RUNNING)scanRunning=true;else if(result>=0){const int slot=findVisibleSlot(result);WiFi.scanDelete();if(slot>=0)connectSlot((uint8_t)slot);}}
void finishReturnScan(){if(!scanRunning)return;const int result=WiFi.scanComplete();if(result==WIFI_SCAN_RUNNING){if(millis()-scanStartedAt>WIFI_SCAN_WATCHDOG_MS){WiFi.scanDelete();scanRunning=false;}return;}scanRunning=false;if(result<0){WiFi.scanDelete();return;}const int slot=findVisibleSlot(result);WiFi.scanDelete();if(slot<0)return;if(connectSlot((uint8_t)slot)&&wifiInternetAvailable()){active=true;lastInternetResult=true;return;}active=false;activeSlot=-1;Serial.println("[NET] Wi-Fi visible mais Internet non valide -> 4G conservee.");}
}
void wifiManagerBegin(){loadProfiles();WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(false);activeSlot=-1;active=false;wifiLostSince=0;lastInternetCheck=0;lastReturnScan=millis();scanRunning=false;}
uint8_t wifiProfileCount(){uint8_t c=0;for(uint8_t i=0;i<MAX_WIFI_PROFILES;++i)if(validSlot(i))++c;return c;}
bool wifiConnectBestSaved(){if(wifiProfileCount()==0)return false;WiFi.mode(WIFI_STA);const int count=WiFi.scanNetworks(false,true,false,300);const int slot=findVisibleSlot(count);WiFi.scanDelete();if(slot<0){Serial.println("[WIFI] Aucun profil du dashboard visible au demarrage.");return false;}if(!connectSlot((uint8_t)slot))return false;if(!wifiInternetAvailable()){Serial.println("[WIFI] Wi-Fi connecte mais Internet indisponible.");WiFi.disconnect(false,false);active=false;activeSlot=-1;return false;}lastInternetResult=true;Serial.println("[NET] Wi-Fi prioritaire actif.");return true;}
bool wifiInternetAvailable(){return httpReachable();}
bool wifiIsActive(){return active&&WiFi.status()==WL_CONNECTED;}
int wifiSignalPercent(){if(WiFi.status()!=WL_CONNECTED)return 0;const int rssi=WiFi.RSSI();if(rssi<=-100)return 0;if(rssi>=-50)return 100;return constrain(2*(rssi+100),0,100);}
void wifiNetworkTick(bool){const uint32_t now=millis();if(active){if(!validSlot((uint8_t)activeSlot)){Serial.println("[NET] Profil Wi-Fi actif retire -> recherche Wi-Fi immediate.");active=false;activeSlot=-1;WiFi.disconnect(false,false);wifiLostSince=0;lastReturnScan=0;startReturnScan();return;}if(WiFi.status()!=WL_CONNECTED){wifiLostSince=now;}else if(now-lastInternetCheck>=WIFI_INTERNET_CHECK_MS){lastInternetCheck=now;lastInternetResult=httpReachable();if(!lastInternetResult){if(wifiLostSince==0)wifiLostSince=now;}else wifiLostSince=0;}if(wifiLostSince!=0&&now-wifiLostSince>=WIFI_LOSS_CONFIRM_MS){Serial.println("[NET] Internet Wi-Fi perdu -> recherche Wi-Fi immediate, sinon 4G.");active=false;activeSlot=-1;WiFi.disconnect(false,false);wifiLostSince=0;lastReturnScan=0;startReturnScan();}return;}finishReturnScan();if(active)return;if(!scanRunning&&now-lastReturnScan>=WIFI_RETURN_SCAN_MS){lastReturnScan=now;startReturnScan();}}
bool wifiPostJson(const String& json,int& httpStatus){httpStatus=0;if(!wifiIsActive())return false;WiFiClientSecure client;client.setInsecure();HTTPClient http;String url=trakWebAppUrl()+WIFI_API_PATH+"?api_key="+trakApiKey();if(!http.begin(client,url))return false;http.setTimeout(8000);http.addHeader("Content-Type","application/json");httpStatus=http.POST(json);http.end();return httpStatus>=200&&httpStatus<300;}
bool wifiSyncProfilesFromServer(){if(!wifiIsActive())return false;WiFiClientSecure client;client.setInsecure();HTTPClient http;String url=trakWebAppUrl()+WIFI_API_PATH+"?api_key="+trakApiKey();if(!http.begin(client,url))return false;http.setTimeout(6000);const int status=http.GET();if(status<200||status>=300){http.end();return false;}const String body=http.getString();http.end();for(uint8_t slot=0;slot<MAX_WIFI_PROFILES;++slot){const int pos=body.indexOf(String("\"slot\":")+slot);if(pos<0){wifiClearProfile(slot);continue;}const int ssidKey=body.indexOf("\"ssid\":\"",pos),pwdKey=body.indexOf("\"password\":\"",pos);if(ssidKey<0||pwdKey<0)continue;const int ss=ssidKey+8,se=body.indexOf('"',ss),ps=pwdKey+12,pe=body.indexOf('"',ps);if(se<0||pe<0)continue;profiles[slot].ssid=body.substring(ss,se);profiles[slot].password=body.substring(ps,pe);saveProfile(slot);}Serial.println("[WIFI] Profils dashboard synchronises.");return true;}
void wifiSetProfile(uint8_t slot,const char* ssid,const char* password){if(slot>=MAX_WIFI_PROFILES)return;profiles[slot].ssid=ssid?ssid:"";profiles[slot].password=password?password:"";saveProfile(slot);}
void wifiClearProfile(uint8_t slot){if(slot>=MAX_WIFI_PROFILES)return;profiles[slot].ssid="";profiles[slot].password="";saveProfile(slot);}
