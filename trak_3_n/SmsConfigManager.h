#pragma once
#include <Arduino.h>

void smsConfigBegin();
void smsConfigTick();
bool smsConfigIsConfigured();
String smsConfigUserPhone();
String smsConfigApiUrl();
String smsConfigApiKey();
String smsConfigTrakId();
