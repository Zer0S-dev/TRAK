#pragma once

#include <Arduino.h>

/*
 * ModemUplink
 * -----------
 * Remplace ModemArbiter + les appels directs à sendToTrackserverCellular /
 * sendHttpPostCellular depuis TrackingEngine.cpp et TRAKConnect.cpp.
 *
 * Ancienne architecture : deux tâches indépendantes (TrackingSender et
 * TRAKConnect) se disputaient le modem via un mutex "arbitre" à courte
 * échéance (250 ms), alors qu'une transaction HTTP AT-commands peut durer
 * plusieurs secondes, voire plusieurs dizaines de secondes. Résultat :
 * l'arbitrage n'avait quasiment aucun effet utile, et les deux clients
 * pouvaient se bloquer mutuellement pendant de longues périodes.
 *
 * Nouvelle architecture : UNE seule tâche possède le modem pour tout le
 * trafic HTTP cellulaire. Il n'y a donc plus de contention à arbitrer :
 * la tâche traite, à chaque itération, au plus un échantillon TRAK
 * Connect (prioritaire, car sensible à la latence pour l'affichage "live")
 * puis au plus un point Trackserver du FIFO SD (débit du backlog).
 *
 * En WiFi, il n'y a pas de ressource matérielle partagée (l'ESP32 gère
 * plusieurs connexions TCP en parallèle) : TrackingEngine et TRAKConnect
 * continuent donc d'envoyer directement, sans passer par ce module.
 */

struct TrakConnectSample {
  float latitude = 0.0f;
  float longitude = 0.0f;
  uint32_t seq = 0;
};

void modemUplinkBegin();

// Appelé par TRAKConnect.cpp quand le transport actif est le cellulaire.
// Tampon circulaire borné (peu profond) : TRAK Connect n'a besoin que de
// la position la plus récente pour l'affichage live, donc en cas de
// tampon plein le plus ancien échantillon en attente est écrasé plutôt
// que de perdre le nouveau.
void modemUplinkEnqueueTrakConnect(const TrakConnectSample& sample);

// État du dernier envoi TRAK Connect effectué par ModemUplink (utilisé
// pour trakConnectIsConnected() quand le cellulaire est actif).
bool modemUplinkTrakConnectLastOk();
