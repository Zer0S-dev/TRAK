#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "TrakConfig.h"
#include "WizardManager.h"

extern void trakCommunicationSuspendForWizard();

namespace {
WebServer server(80);
volatile bool active = false;
bool pendingResetConfirmation = false;
bool serverStarted = false;

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
  trakCommunicationSuspendForWizard();

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
  Serial.println("[WIZARD] MODE EXCLUSIF ACTIF. Les traitements TRAK normaux sont suspendus.");
}

void trakWizardCommand(const String& rawCommand) {
  String command = rawCommand;
  command.trim();

  // Serial commands deliberately mirror the agreed SMS flow for phase 2 testing.
  if (command == "HELLO TRAK") {
    if (trakUserPhone().isEmpty()) {
      Serial.println("[WIZARD] TRAK vierge -> ouverture directe du Wizard.");
      enterWizard(true);
    } else {
      pendingResetConfirmation = true;
      Serial.println("[WIZARD] TRAK deja configure.");
      Serial.println("[WIZARD] Confirmation requise : CONFIRM RESET");
      Serial.println("[WIZARD] Simulation SMS : aucune donnee n'est effacee pour l'instant.");
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

  if (command == "WIZARD") {
    // Explicit development shortcut. It never exists as a normal remote trigger.
    enterWizard(false);
  }
}

void trakWizardTask() {
  if (active && serverStarted) server.handleClient();
}

bool trakWizardActive() {
  return active;
}
