#pragma once

#include <Arduino.h>
#include "ModemManager.h"

// TRAK Connect 3.0.11 - phase 1 test.
// Sends only the current latitude/longitude as JSON over WebSocket.
void trakConnectBegin();
void trakConnectService(const GnssData& gps);
bool trakConnectIsConnected();
