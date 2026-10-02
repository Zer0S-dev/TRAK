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
