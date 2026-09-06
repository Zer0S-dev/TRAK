#pragma once
#include <Arduino.h>

void networkManagerBegin();
void networkTask(void* parameter);

bool connectWiFi();
bool activateCellularAndPublish();

int getWiFiSignalPercent();

// Résultat des envois réels à Trackserver.
// La décision de bascule reste exclusivement dans NetworkTask (Core 1).
void networkReportTrackserverResult(bool success);
