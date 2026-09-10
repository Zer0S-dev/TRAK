#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <HardwareSerial.h>
#include "TrakConfig.h"
#include "WizardManager.h"

extern HardwareSerial modem;
extern volatile bool modemReady;
extern String at(const String& command, uint32_t timeoutMs);

namespace {
WebServer server(80);
volatile bool active = false;
bool pendingResetConfirmation = false;
bool serverStarted = false;
uint32_t lastSmsPoll = 0;
constexpr uint32_t SMS_POLL_MS = 5000;

String normalizePhoneLocal(String phone) {
  phone.trim();
  String out;
  out.reserve(phone.length());
  for (size_t i = 0; i < phone.length(); ++i) {
    const char c = phone[i];
    if ((c >= '0' && c <= '9') || (c == '+' && out.length() == 0)) out += c;
  }
  return out;
}

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '\\' || c == '"') out += '\\';
    if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

String configJson() {
  String json;
  json.reserve(360);
  json += "{\"server_url\":\""; json += jsonEscape(trakWebAppUrl());
  json += "\",\"user_phone\":\""; json += jsonEscape(trakUserPhone());
  json += "\",\"trak_phone\":\""; json += jsonEscape(trakPhone());
  json += "\",\"api_key\":\""; json += jsonEscape(trakApiKey());
  json += "\",\"provisioned\":"; json += trakConfigProvisioned() ? "true" : "false";
  json += "}";
  return json;
}

void handleConfigGet() {
  if (!active) { server.send(403, "application/json", "{\"error\":\"wizard_inactive\"}"); return; }
  server.send(200, "application/json", configJson());
}

void handleConfigSave() {
  if (!active) { server.send(403, "application/json", "{\"error\":\"wizard_inactive\"}"); return; }
  if (!server.hasArg("server_url") || !server.hasArg("user_phone") || !server.hasArg("trak_phone")) {
    server.send(400, "application/json", "{\"error\":\"missing_fields\"}");
    return;
  }

  const String url = server.arg("server_url");
  const String userPhone = server.arg("user_phone");
  const String trakPhone = server.arg("trak_phone");

  if (!trakConfigSetServerUrl(url) || !trakConfigSetUserPhone(userPhone) || !trakConfigSetTrakPhone(trakPhone)) {
    server.send(400, "application/json", "{\"error\":\"invalid_configuration\"}");
    return;
  }
  trakConfigSetProvisioned(true);

  Serial.println("[WIZARD] Configuration enregistree dans NVS.");
  Serial.println("[WIZARD] trak_cfg mis a jour; trak_wifi conserve.");
  server.send(200, "application/json", configJson());
}

void handleReboot() {
  if (!active) { server.send(403, "application/json", "{\"error\":\"wizard_inactive\"}"); return; }
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Reboot TRAK\"}");
  delay(250);
  ESP.restart();
}

void startServer() {
  if (!LittleFS.begin(false)) {
    Serial.println("[WIZARD] ERREUR: LittleFS indisponible. Uploade le dossier data puis relance.");
  }

  WiFi.mode(WIFI_AP);
  WiFi.disconnect(false, false);
  const bool apOk = WiFi.softAP("TRAK-DIRECT");
  if (!apOk) {
    Serial.println("[WIZARD] ERREUR: impossible de demarrer TRAK-DIRECT.");
    return;
  }

  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigSave);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.serveStatic("/", LittleFS, "/");
  server.onNotFound([]() {
    if (LittleFS.exists("/wizard.html")) {
      server.sendHeader("Location", "/wizard.html", true);
      server.send(302, "text/plain", "Redirect");
    } else {
      server.send(404, "text/plain", "Wizard files absents de LittleFS");
    }
  });
  server.begin();
  serverStarted = true;

  Serial.print("[WIZARD] Wi-Fi Direct SSID : TRAK-DIRECT | IP : ");
  Serial.println(WiFi.softAPIP());
  Serial.println("[WIZARD] Ouvre http://192.168.4.1/wizard.html");
}

void enterWizard(bool resetProvisioning) {
  if (active) return;
  if (resetProvisioning) {
    trakConfigResetProvisioning();
    if (!trakConfigRegenerateApiKey()) {
      Serial.println("[WIZARD] ERREUR: generation API key impossible.");
      return;
    }
  }
  active = true;
  pendingResetConfirmation = false;
  startServer();
  Serial.println("[WIZARD] MODE EXCLUSIF ACTIF. La communication normale est suspendue par le runtime.");
}

bool sendSms(const String& destination, const String& text) {
  if (!modemReady || destination.isEmpty()) return false;
  at("AT+CMGF=1", 2000);
  while (modem.available()) modem.read();
  modem.print(String("AT+CMGS=\"") + destination + "\"\r\n");
  String prompt;
  const uint32_t start = millis();
  while (millis() - start < 5000) {
    while (modem.available()) {
      prompt += (char)modem.read();
      if (prompt.indexOf('>') >= 0) {
        modem.print(text);
        modem.write(26);
        String result;
        const uint32_t sendStart = millis();
        while (millis() - sendStart < 15000) {
          while (modem.available()) {
            result += (char)modem.read();
            if (result.indexOf("OK") >= 0) {
              Serial.print("[SMS] Envoye a "); Serial.println(destination);
              return true;
            }
            if (result.indexOf("ERROR") >= 0 || result.indexOf("+CMS ERROR") >= 0) return false;
          }
          vTaskDelay(pdMS_TO_TICKS(10));
        }
        return false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  Serial.println("[SMS] Pas de prompt CMGS.");
  return false;
}

bool processSmsBody(const String& sender, const String& body) {
  String message = body;
  message.trim();
  const String normalizedSender = normalizePhoneLocal(sender);

  if (message == "HELLO TRAK") {
    if (trakUserPhone().isEmpty()) {
      Serial.println("[SMS] HELLO TRAK -> TRAK vierge, ouverture Wizard.");
      enterWizard(true);
      return true;
    }

    Serial.println("[SMS] HELLO TRAK -> TRAK deja configure, confirmation requise.");
    pendingResetConfirmation = true;
    sendSms(trakUserPhone(), "TRAK: demande de reset recue. Repondre exactement: CONFIRM RESET");
    return true;
  }

  if (message == "CONFIRM RESET" && pendingResetConfirmation) {
    if (normalizedSender == normalizePhoneLocal(trakUserPhone())) {
      Serial.println("[SMS] CONFIRM RESET valide depuis USER_PHONE -> ouverture Wizard.");
      enterWizard(true);
    } else {
      Serial.println("[SMS] CONFIRM RESET ignore: expediteur non autorise.");
    }
    return true;
  }
  return false;
}

void pollSms() {
  if (active || !modemReady) return;
  const uint32_t now = millis();
  if (now - lastSmsPoll < SMS_POLL_MS) return;
  lastSmsPoll = now;

  at("AT+CMGF=1", 2000);
  const String response = at("AT+CMGL=\"REC UNREAD\"", 5000);
  int cursor = 0;
  while (true) {
    const int marker = response.indexOf("+CMGL:", cursor);
    if (marker < 0) break;
    const int lineEnd = response.indexOf('\n', marker);
    if (lineEnd < 0) break;
    const String header = response.substring(marker, lineEnd);

    int q1 = header.indexOf('"');
    int q2 = q1 >= 0 ? header.indexOf('"', q1 + 1) : -1;
    int q3 = q2 >= 0 ? header.indexOf('"', q2 + 1) : -1;
    int q4 = q3 >= 0 ? header.indexOf('"', q3 + 1) : -1;
    if (q1 < 0 || q2 < 0 || q3 < 0 || q4 < 0) { cursor = lineEnd + 1; continue; }

    const int commaAfterIndex = header.indexOf(',');
    const int smsIndex = commaAfterIndex > 0 ? header.substring(6, commaAfterIndex).toInt() : -1;
    const String sender = header.substring(q3 + 1, q4);
    int bodyEnd = response.indexOf("\r\n+CMGL:", lineEnd + 1);
    if (bodyEnd < 0) bodyEnd = response.indexOf("\n+CMGL:", lineEnd + 1);
    if (bodyEnd < 0) bodyEnd = response.indexOf("\r\nOK", lineEnd + 1);
    if (bodyEnd < 0) bodyEnd = response.length();
    String body = response.substring(lineEnd + 1, bodyEnd);
    body.trim();

    if (smsIndex >= 0) {
      processSmsBody(sender, body);
      at(String("AT+CMGD=") + String(smsIndex), 3000);
    }
    cursor = bodyEnd + 1;
    if (active) break;
  }
}
}

void trakWizardSmsTick() {
  pollSms();
}

void trakWizardCommand(const String& rawCommand) {
  String command = rawCommand;
  command.trim();

  if (command == "HELLO TRAK") {
    if (trakUserPhone().isEmpty()) {
      Serial.println("[WIZARD] TRAK vierge -> ouverture directe du Wizard.");
      enterWizard(true);
    } else {
      pendingResetConfirmation = true;
      Serial.println("[WIZARD] TRAK deja configure.");
      Serial.println("[WIZARD] Confirmation requise : CONFIRM RESET");
      Serial.println("[WIZARD] Simulation SMS: confirmation depuis USER_PHONE requise.");
    }
    return;
  }

  if (command == "CONFIRM RESET") {
    if (!pendingResetConfirmation) {
      Serial.println("[WIZARD] Aucune demande de reset en attente.");
      return;
    }
    enterWizard(true);
    return;
  }

  if (command == "WIZARD") enterWizard(false);
}

void trakWizardTask() {
  if (active && serverStarted) server.handleClient();
}

bool trakWizardActive() {
  return active;
}
