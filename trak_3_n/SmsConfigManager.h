#pragma once
#include <Arduino.h>

void smsConfigBegin();
void smsConfigTick();
bool smsConfigIsConfigured();
String smsConfigUserPhone();
String smsConfigTrackserverUrl();
String smsConfigDashboardUrl();
String smsConfigApiKey();
String smsConfigTrakId();

uint64_t smsConfigLastConfigTimestamp();
bool smsConfigApplyRemoteConfig(
    const String& trakId,
    const String& trakPhone,
    const String& userPhone,
    const String& apiKey,
    const String& trackserverUrl,
    const String& dashboardUrl,
    const String& wifiSsid1,
    const String& wifiPassword1,
    const String& wifiSsid2,
    const String& wifiPassword2,
    const String& wifiSsid3,
    const String& wifiPassword3,
    uint64_t configTimestamp);

uint64_t smsConfigPendingRemoteAckTimestamp();
void smsConfigClearPendingRemoteAck();
