#pragma once
#include <Arduino.h>

void smsConfigBegin();
void smsConfigTick();
bool smsConfigIsConfigured();
String smsConfigUserPhone();
String smsConfigTrackserverUrl();
String smsConfigApiKey();
String smsConfigTrakId();
String smsConfigDashboardUrl();
