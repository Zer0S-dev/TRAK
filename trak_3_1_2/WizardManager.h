#pragma once
#include <Arduino.h>

// Real modem SMS provisioning flow.
// HELLO TRAK opens the Wizard on a virgin TRAK. A configured TRAK requires
// CONFIRM RESET from the stored user phone before provisioning is opened.
void trakWizardSmsTick();

// Development/SMS command entry point retained for serial testing.
void trakWizardCommand(const String& command);

void trakWizardTask();
bool trakWizardActive();
