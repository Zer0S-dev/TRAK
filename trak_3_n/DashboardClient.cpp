#include "DashboardClient.h"
#include "SmsConfigManager.h"

void dashboardBegin() {}
DashboardResult dashboardSendPosition(const GnssPosition&) { return DashboardResult::NotReady; }
