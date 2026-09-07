#pragma once

#include <Arduino.h>
#include "ModemManager.h"

// TRAK Connect 3.0.11 - phase 1 test.
// Sends only the current latitude/longitude as JSON over WebSocket.
void trakConnectBegin();
void trakConnectService(const GnssData& gps);
bool trakConnectIsConnected();

// Envoie un échantillon (lat/lon/seq déjà calculés) via le transport
// cellulaire. Appelé exclusivement par ModemUplink (seul propriétaire du
// modem 4G) : aucun arbitrage n'est nécessaire ici.
bool trakConnectSendOneCellular(float latitude, float longitude, uint32_t seq);
