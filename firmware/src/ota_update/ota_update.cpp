#include "ota_update.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <cstring>

#include "app_network.h"
#include "ui/ui.h"

namespace {
constexpr uint16_t kOtaPort = 3232;
constexpr unsigned long kIpv6LinkLocalWaitMs = 2000;

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

bool has_station_ipv6() {
  return WiFi.linkLocalIPv6() != IPAddress(IPv6);
}

void wait_for_station_ipv6() {
  if (WiFi.status() != WL_CONNECTED || has_station_ipv6()) {
    return;
  }

  const unsigned long startMs = millis();
  while (!has_station_ipv6() && millis() - startMs < kIpv6LinkLocalWaitMs) {
    delay(100);
  }
}

void set_ota_status(const char *title, const char *message) {
  Serial.printf("%s: %s\n", title, message);
  ui_set_status_message(title, message);
}
}  // namespace

void ota_update_init() {
  ArduinoOTA.setHostname(app_network::kHostname);
  ArduinoOTA.setPort(kOtaPort);

  if (std::strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    set_ota_status("OTA Update", "Starting firmware update");
  });

  ArduinoOTA.onEnd([]() {
    set_ota_status("OTA Update", "Update complete. Rebooting...");
  });

  ArduinoOTA.onError([](ota_error_t error) {
    const char *message = "Unknown error";

    switch (error) {
      case OTA_AUTH_ERROR:
        message = "Authentication failed";
        break;
      case OTA_BEGIN_ERROR:
        message = "Begin failed";
        break;
      case OTA_CONNECT_ERROR:
        message = "Connection failed";
        break;
      case OTA_RECEIVE_ERROR:
        message = "Receive failed";
        break;
      case OTA_END_ERROR:
        message = "End failed";
        break;
    }

    set_ota_status("OTA Error", message);
  });

  wait_for_station_ipv6();
  ArduinoOTA.begin();
  if (MDNS.addService("http", "tcp", 80)) {
    Serial.printf("mDNS HTTP service ready: http://%s.local/\n", app_network::kHostname);
  } else {
    Serial.println("mDNS HTTP service registration failed");
  }

  Serial.printf("OTA ready: %s.local:%u\n", app_network::kHostname, kOtaPort);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("OTA station IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("OTA station IPv6: %s\n", WiFi.linkLocalIPv6().toString(true).c_str());
  }
}

void ota_update_loop() {
  ArduinoOTA.handle();
}
