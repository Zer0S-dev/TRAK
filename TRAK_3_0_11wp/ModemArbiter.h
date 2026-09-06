#ifndef MODEM_ARBITER_H
#define MODEM_ARBITER_H

#include <Arduino.h>

// Clients that share the A7670 HTTP transport.
enum class ModemArbiterClient : uint8_t {
  TRACKSERVER = 0,
  TRAK_CONNECT = 1
};

void modemArbiterBegin();
bool modemArbiterAcquire(ModemArbiterClient client, uint32_t timeoutMs = 30000);
void modemArbiterRelease(ModemArbiterClient client);

#endif
