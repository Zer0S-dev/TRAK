#pragma once
#include <Arduino.h>

// Development/SMS entry point. The current phase uses the serial console to
// simulate the SMS workflow until the modem SMS handler is connected.
void trakWizardCommand(const String& command);

void trakWizardTask();
bool trakWizardActive();
