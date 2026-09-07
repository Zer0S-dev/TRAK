#pragma once

#include <Arduino.h>
#include "ModemManager.h"
#include "MotionManager.h"

// TrackingEngine v2 : séparation production / transport.
// Le point GNSS est d'abord persisté dans le FIFO SD, puis un task
// indépendant se charge de l'expédier vers Trackserver.
void trackingEngineBegin();
void trackingEngineUpdate(uint32_t now, const GnssData& gps, const MotionState& motion);
uint32_t trackingEngineTxCount();
uint32_t trackingEngineLastTxAt();

// Envoie au plus UN point du FIFO SD vers Trackserver en cellulaire.
// Appelé exclusivement par ModemUplink (seul propriétaire du modem 4G) :
// aucun arbitrage n'est nécessaire ici, l'exclusivité est garantie par
// construction (une seule tâche appelle cette fonction).
// Retourne true si un point a été envoyé avec succès et retiré du FIFO.
bool trackingEngineSendOneCellular();
