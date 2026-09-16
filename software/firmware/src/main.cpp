#include <Arduino.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#if defined(BARDBOX_WIFI_SSID) && defined(BARDBOX_WIFI_PASSWORD)
#include "BardBoxWiFiRecovery.h"
static bardbox::WiFiRecovery wifiRecovery;
#endif

static const char *DEVICE_UID = "bb-prj-air-001";
static const char *FW_VERSION = "1.0.0";

static float readTemperatureC() {
  return 21.5f;
}

static float readRelativeHumidityPct() {
  return 43.0f;
}

static long readPressurePa() {
  return 100980L;
}

static void sendInfo() {
  Serial.print("OK INFO uid=");
  Serial.print(DEVICE_UID);
  Serial.print(" fw=");
  Serial.print(FW_VERSION);
#if defined(BARDBOX_WIFI_SSID) && defined(BARDBOX_WIFI_PASSWORD)
  Serial.print(" wifi=");
  Serial.print(wifiRecovery.offline() ? 0 : (WiFi.status() == WL_CONNECTED ? 1 : 0));
  Serial.print(" wifi_offline_ms=");
  Serial.print(wifiRecovery.offlineMs(millis()));
#endif
  Serial.println(" sensors=EXAMPLE");
}

static void sendHeader() {
  Serial.println("HDR,v1,temp_c,rh_pct,press_pa");
}

static void sendReading() {
  Serial.print("DAT,");
  Serial.print(readTemperatureC(), 2);
  Serial.print(",");
  Serial.print(readRelativeHumidityPct(), 2);
  Serial.print(",");
  Serial.println(readPressurePa());
}

static void handleCommand(String command) {
  command.trim();
  command.toUpperCase();

  if (command == "INFO") {
    sendInfo();
  } else if (command == "HEADER") {
    sendHeader();
  } else if (command == "READ") {
    sendReading();
  } else if (command == "PING") {
    Serial.println("PONG");
  } else {
    Serial.println("ERR UNKNOWN_CMD");
  }
}

void setup() {
  Serial.begin(115200);
}

void loop() {
#if defined(BARDBOX_WIFI_SSID) && defined(BARDBOX_WIFI_PASSWORD)
  wifiRecovery.service(BARDBOX_WIFI_SSID, BARDBOX_WIFI_PASSWORD, millis(),
                       []() {},  // Close project TCP clients here.
                       []() {}); // Flush durable queued records here.
#endif
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    handleCommand(command);
  }
}
