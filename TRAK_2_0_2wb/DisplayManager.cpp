#include "DisplayManager.h"
#include "RuntimeConfig.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R2,
    /* reset=*/ U8X8_PIN_NONE);

void initDisplay()
{
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB18_tf);
  u8g2.setCursor(20, 32);
  u8g2.print("TRAK");

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(28, 48);
  u8g2.print(FIRMWARE_VERSION);
  u8g2.sendBuffer();
  delay(2000); 
}

int getWiFiSignalPercent()
{
  if (WiFi.status() != WL_CONNECTED) {
    return 0;
  }

  const int rssi = WiFi.RSSI();

  if (rssi <= -100) return 0;
  if (rssi >= -50)  return 100;

  return constrain(2 * (rssi + 100), 0, 100);
}

void updateOLED(
    const GnssData& gps,
    int signalPercent,
    bool httpSuccess,
    uint32_t txCount,
    uint32_t lastTxAgeMs,
    bool constellationPage,
    bool wifiActive,
    int8_t activeWiFiSlot)
{
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.setCursor(0, 8);
  if (wifiActive) {
    u8g2.print("WF");
    if (activeWiFiSlot >= 0 && activeWiFiSlot < 3) {
      u8g2.print(activeWiFiSlot + 1);
    } else {
      u8g2.print("-");
    }
  } else {
    u8g2.print("4G");
  }

  u8g2.print(httpSuccess ? ":OK" : ":--");

  u8g2.setCursor(32, 8);
  u8g2.print(signalPercent);
  u8g2.print("%");

  u8g2.setCursor(61, 8);
  u8g2.print("TX:");
  u8g2.print(txCount);

  u8g2.setCursor(101, 8);
  if (lastTxAgeMs == UINT32_MAX) {
    u8g2.print("--");
  } else {
    u8g2.print(lastTxAgeMs / 1000);
    u8g2.print("s");
  }

  u8g2.drawLine(0, 10, 127, 10);

  u8g2.setFont(u8g2_font_6x10_tf);

  if (constellationPage) {
    u8g2.setCursor(0, 23);
    u8g2.print("GPS : ");
    u8g2.print(gps.gpsSatellites);

    u8g2.setCursor(0, 35);
    u8g2.print("GLO : ");
    u8g2.print(gps.glonassSatellites);

    u8g2.setCursor(64, 23);
    u8g2.print("BDS : ");
    u8g2.print(gps.beidouSatellites);

    u8g2.setCursor(64, 35);
    u8g2.print("GAL : ");
    u8g2.print(gps.galileoSatellites);

    u8g2.setCursor(0, 51);
    u8g2.print("TOTAL: ");
    u8g2.print(gps.satellites);

    u8g2.setCursor(0, 62);
    u8g2.print("MODE: ");
    u8g2.print(gps.fixMode);
  } else if (gps.hasFix) {
    u8g2.setCursor(0, 22);
    u8g2.print("Lat ");
    u8g2.print(gps.latitude, 5);

    u8g2.setCursor(0, 33);
    u8g2.print("Lon ");
    u8g2.print(gps.longitude, 5);

    u8g2.setCursor(0, 44);
    u8g2.print("V ");
    u8g2.print(gps.speedKmh, 1);
    u8g2.print(" km/h");

    u8g2.setCursor(72, 44);
    u8g2.print("S:");
    u8g2.print(gps.satellites);

    u8g2.setCursor(0, 55);
    u8g2.print("Alt ");
    u8g2.print(gps.altitude, 0);
    u8g2.print(" m");
  } else {
    u8g2.setCursor(0, 28);
    u8g2.print("Recherche fix...");

    u8g2.setCursor(0, 44);
    u8g2.print("Sats: ");
    u8g2.print(gps.satellites);

    u8g2.setCursor(0, 58);
    u8g2.print("Mode: ");
    u8g2.print(gps.fixMode);
  }

  u8g2.sendBuffer();
}
