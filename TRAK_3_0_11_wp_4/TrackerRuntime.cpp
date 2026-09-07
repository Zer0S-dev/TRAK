#include "TrackerRuntime.h"
#include "TrackerState.h"
#include "Config.h"
#include "ModemManager.h"
#include "MotionManager.h"
#include "PositionBuffer.h"
#include "SentinelManager.h"
#include "NetworkManager.h"

bool getWebTrackerData(WebTrackerData& out)
{
  SharedState snapshot;
  if (!copyState(snapshot)) return false;

  out.hasFix = snapshot.gps.hasFix;
  out.satellites = snapshot.gps.satellites;
  out.gpsSatellites = snapshot.gps.gpsSatellites;
  out.glonassSatellites = snapshot.gps.glonassSatellites;
  out.beidouSatellites = snapshot.gps.beidouSatellites;
  out.galileoSatellites = snapshot.gps.galileoSatellites;
  out.fixMode = snapshot.gps.fixMode;
  out.latitude = snapshot.gps.latitude;
  out.longitude = snapshot.gps.longitude;
  out.altitude = snapshot.gps.altitude;
  out.speedKmh = snapshot.gps.speedKmh;
  out.rawSpeedKmh = snapshot.gps.rawSpeedKmh;
  out.filteredSpeedKmh = snapshot.gps.filteredSpeedKmh;
  out.rawAltitude = snapshot.gps.rawAltitude;
  out.filteredAltitude = snapshot.gps.filteredAltitude;

  out.motionXDps = snapshot.motion.xDps;
  out.motionYDps = snapshot.motion.yDps;
  out.motionZDps = snapshot.motion.zDps;
  out.motionDps = snapshot.motion.motionDps;
  out.moving = snapshot.motion.moving;
  out.stationaryConfirmed = snapshot.motion.stationaryConfirmed;
  out.motionStationaryConfirmationRemainingMs = motionStationaryConfirmationRemainingMs();
  out.motionCalibrated = snapshot.motion.calibrated;
  out.motionSensitivityLevel = motionSensitivityLevel();
  out.motionSensitivityThresholdDps = motionSensitivityThresholdDps();
  out.sendIntervalLevel = sendIntervalLevel();
  out.sendIntervalMovingSec = sendIntervalMovingSec();

  out.signalPercent = snapshot.signalPercent;
  out.httpSuccess = snapshot.httpSuccess;
  out.wifiConnected = snapshot.wifiConnected;
  out.communicationReady = snapshot.communicationReady;
  out.activeWiFi = snapshot.activeWiFi;
  out.activeWiFiSlot = snapshot.activeWiFiSlot;
  out.internetAvailable = snapshot.internetAvailable;
  out.activeNetworkName = activeNetworkName(snapshot.activeWiFi ? ActiveNetwork::WIFI :
                                             (snapshot.internetAvailable ? ActiveNetwork::CELLULAR : ActiveNetwork::NONE));
  out.txCount = snapshot.txCount;
  out.lastTxAt = snapshot.lastTxAt;
  out.bufferCount = snapshot.bufferCount;
  out.bufferCapacity = positionBufferCapacity();
  out.sentinelPhoneConfigured = sentinelHasUserPhone();
  out.sentinelState = sentinelStateName();
  out.sentinelConfirmationRemainingMs = sentinelConfirmationRemainingMs();
  return true;
}

void trackerRuntimeBegin()
{
  // Intentionally kept as a small extension point for future runtime services.
}
