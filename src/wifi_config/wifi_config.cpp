#include "wifi_config.h"

#include <WebServer.h>
#include <WiFi.h>

#include "audio/audio.h"
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
      "content=\"width=device-width, initial-scale=1\"><title>ESP32 Config</title>"
      "<style>body{font-family:system-ui,sans-serif;padding:24px;line-height:1.4}"
      "button{font:inherit;padding:12px 16px}#status{margin-top:12px;color:#444}"
      "code{display:block;margin-top:12px;padding:12px;background:#f4f4f4;"
      "border-radius:8px;overflow:auto}</style>"
      "</head><body><h1>ESP32 Config</h1><p>Device is online.</p>"
      "<button id=\"beep\">Play beep</button><p id=\"status\"></p>"
      "<p>Programmatic trigger:</p><code>curl -X POST http://192.168.4.1/beep</code>"
      "<script>const button=document.getElementById('beep');const status=document.getElementById('status');"
      "button.addEventListener('click',async()=>{button.disabled=true;status.textContent='Playing...';"
      "try{const response=await fetch('/beep',{method:'POST'});"
      "status.textContent=response.ok?'Beep played':'Beep failed';}"
      "catch(e){status.textContent='Request failed';}"
      "button.disabled=false;});</script></body></html>");
}

void handleBeep() {
  if (audio_play_beep()) {
    server.send(200, "text/plain", "ok");
    return;
  }

  server.send(500, "text/plain", "beep failed");
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
  server.on("/beep", HTTP_POST, handleBeep);
  server.begin();

  ui_set_network_info(kAccessPointSsid, url.c_str());

  Serial.printf("Wi-Fi AP started: %s\n", kAccessPointSsid);
  Serial.printf("Wi-Fi URL: %s\n", url.c_str());
}

void wifi_config_loop() {
  server.handleClient();
}
