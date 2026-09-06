#include "ModemArbiter.h"

#include "DevLog.h"

namespace {

// Static FreeRTOS mutex: no race during lazy initialization and no heap allocation.
StaticSemaphore_t arbiterMutexBuffer;
SemaphoreHandle_t arbiterMutex = nullptr;

bool active = false;
bool waitingTrackserver = false;
bool waitingTrakConnect = false;

// When both clients are waiting, ownership alternates.
ModemArbiterClient nextTurn = ModemArbiterClient::TRACKSERVER;

bool isWaiting(ModemArbiterClient client)
{
  return client == ModemArbiterClient::TRACKSERVER
             ? waitingTrackserver
             : waitingTrakConnect;
}

void setWaiting(ModemArbiterClient client, bool value)
{
  if (client == ModemArbiterClient::TRACKSERVER) {
    waitingTrackserver = value;
  } else {
    waitingTrakConnect = value;
  }
}

ModemArbiterClient otherClient(ModemArbiterClient client)
{
  return client == ModemArbiterClient::TRACKSERVER
             ? ModemArbiterClient::TRAK_CONNECT
             : ModemArbiterClient::TRACKSERVER;
}

const char* clientName(ModemArbiterClient client)
{
  return client == ModemArbiterClient::TRACKSERVER
             ? "TRACKSERVER"
             : "TRAK-CONNECT";
}

} // namespace

void modemArbiterBegin()
{
  if (arbiterMutex != nullptr) return;

  arbiterMutex = xSemaphoreCreateMutexStatic(&arbiterMutexBuffer);

  if (arbiterMutex == nullptr) {
    DevSerial.println("[4G-ARB] ERREUR : mutex arbitre impossible.");
  } else {
    DevSerial.println("[4G-ARB] Arbitre A7670 pret.");
  }
}

bool modemArbiterAcquire(ModemArbiterClient client, uint32_t timeoutMs)
{
  if (arbiterMutex == nullptr) {
    modemArbiterBegin();
  }

  if (arbiterMutex == nullptr) return false;

  // The arbiter is deliberately short-waiting. A cellular HTTP transaction
  // can take several seconds, so holding a caller for 30 s only creates
  // unnecessary stalls. The caller keeps its data and retries later.
  const uint32_t start = millis();
  bool announcedWait = false;

  if (xSemaphoreTake(arbiterMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    setWaiting(client, true);
    xSemaphoreGive(arbiterMutex);
  } else {
    return false;
  }

  for (;;) {
    if (xSemaphoreTake(arbiterMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
      const ModemArbiterClient other = otherClient(client);
      const bool otherWaiting = isWaiting(other);

      // If nobody is using the modem, grant it when:
      //   - it is our turn, or
      //   - the other client is not waiting.
      // When both are waiting, nextTurn guarantees fairness.
      if (!active && (nextTurn == client || !otherWaiting)) {
        active = true;
        setWaiting(client, false);
        xSemaphoreGive(arbiterMutex);

        if (announcedWait) {
          DevSerial.print("[4G-ARB] ");
          DevSerial.print(clientName(client));
          DevSerial.println(" prend le modem.");
        }

        return true;
      }

      xSemaphoreGive(arbiterMutex);
    }

    const uint32_t elapsed = millis() - start;
    if (elapsed >= timeoutMs) {
      if (xSemaphoreTake(arbiterMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        setWaiting(client, false);
        xSemaphoreGive(arbiterMutex);
      }

      DevSerial.print("[4G-ARB] Modem occupe : ");
      DevSerial.print(clientName(client));
      DevSerial.println(" reporte son envoi.");
      return false;
    }

    if (!announcedWait && elapsed >= 100) {
      announcedWait = true;
      DevSerial.print("[4G-ARB] ");
      DevSerial.print(clientName(client));
      DevSerial.println(" attend le modem...");
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void modemArbiterRelease(ModemArbiterClient client)
{
  if (arbiterMutex == nullptr) return;

  if (xSemaphoreTake(arbiterMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return;
  }

  active = false;
  nextTurn = otherClient(client);

  xSemaphoreGive(arbiterMutex);
}
