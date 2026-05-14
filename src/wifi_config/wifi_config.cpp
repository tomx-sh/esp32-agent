#include "wifi_config.h"

#include <WebServer.h>
#include <WiFi.h>

#include "ui/ui.h"

namespace {
constexpr char kAccessPointSsid[] = "ESP32-AMOLED-Setup";
constexpr char kAccessPointPassword[] = "esp32setup";
const IPAddress kAccessPointIp(192, 168, 4, 1);
const IPAddress kGatewayIp(192, 168, 4, 1);
const IPAddress kSubnetMask(255, 255, 255, 0);

WebServer server(80);

void handleRoot() {
  server.send(
      200,
      "text/html",
      "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
      "content=\"width=device-width, initial-scale=1\"></head><body><h1>Hello "
      "from ESP32</h1><p>Config panel scaffold is running.</p></body></html>");
}
}  // namespace

void wifi_config_init() {
  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(kAccessPointIp, kGatewayIp, kSubnetMask)) {
    Serial.println("Wi-Fi softAPConfig failed");
    ui_set_status_message("Wi-Fi Error", "AP config failed");
    return;
  }

  if (!WiFi.softAP(kAccessPointSsid, kAccessPointPassword)) {
    Serial.println("Wi-Fi softAP start failed");
    ui_set_status_message("Wi-Fi Error", "AP start failed");
    return;
  }

  const IPAddress ip = WiFi.softAPIP();
  if (ip == IPAddress((uint32_t)0)) {
    Serial.println("Wi-Fi softAP has no IP");
    ui_set_status_message("Wi-Fi Error", "AP has no IP");
    return;
  }

  String url = "http://" + ip.toString() + "/";

  server.on("/", HTTP_GET, handleRoot);
  server.begin();

  ui_set_network_info(kAccessPointSsid, url.c_str());

  Serial.printf("Wi-Fi AP started: %s\n", kAccessPointSsid);
  Serial.printf("Wi-Fi URL: %s\n", url.c_str());
}

void wifi_config_loop() {
  server.handleClient();
}
