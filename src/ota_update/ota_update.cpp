#include "ota_update.h"

#include <ArduinoOTA.h>
#include <WiFi.h>

#include <cstring>

#include "ui/ui.h"

namespace {
constexpr char kOtaHostname[] = "esp32-agent";
constexpr uint16_t kOtaPort = 3232;

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

void set_ota_status(const char *title, const char *message) {
  Serial.printf("%s: %s\n", title, message);
  ui_set_status_message(title, message);
}
}  // namespace

void ota_update_init() {
  ArduinoOTA.setHostname(kOtaHostname);
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

  ArduinoOTA.begin();

  Serial.printf("OTA ready: %s.local:%u\n", kOtaHostname, kOtaPort);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("OTA station IP: %s\n", WiFi.localIP().toString().c_str());
  }
}

void ota_update_loop() {
  ArduinoOTA.handle();
}
