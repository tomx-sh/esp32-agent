#include "wifi_config.h"

#include <WebServer.h>
#include <WiFi.h>

#include "audio/audio.h"
#include "ui/ui.h"
#include "web/config_page.h"

namespace {
constexpr char kAccessPointSsid[] = "ESP32-AMOLED-Setup";
constexpr char kAccessPointPassword[] = "esp32setup";
const IPAddress kAccessPointIp(192, 168, 4, 1);
const IPAddress kGatewayIp(192, 168, 4, 1);
const IPAddress kSubnetMask(255, 255, 255, 0);
constexpr unsigned long kStationConnectTimeoutMs = 10000;
constexpr unsigned long kForgetDelayMs = 1000;

WebServer server(80);
String stationStatus = "Not connected";
String stationSsid = "";
bool accessPointEnabled = false;
bool forgetWifiPending = false;
unsigned long forgetWifiAtMs = 0;
bool stationConnectPending = false;
unsigned long stationConnectStartedMs = 0;

void refresh_ui() {
  const bool stationConnected = WiFi.status() == WL_CONNECTED;
  const bool apActive = accessPointEnabled;

  String headline;
  String details;

  if (stationConnected) {
    headline = "WiFi:\nConnected to " + stationSsid;
    details = "\nDashboard:\nhttp://" + WiFi.localIP().toString() + "/";

    if (apActive) {
      details += "\n\nHotspot:\n" + String(kAccessPointSsid) +
                 "\nPassword:\n" + String(kAccessPointPassword) +
                 "\nDashboard:\nhttp://" + WiFi.softAPIP().toString() + "/";
    }
  } else if (apActive) {
    headline = "WiFi:\nNot connected\n\nHotspot:\n" + String(kAccessPointSsid);
    details = "Password:\n" + String(kAccessPointPassword) +
              "\nDashboard:\nhttp://" + WiFi.softAPIP().toString() + "/";
  } else {
    headline = "Wi-Fi not connected";
    details = "No hotspot active";
  }

  ui_set_connection_overview(headline.c_str(), details.c_str());
}

bool connect_station() {
  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < kStationConnectTimeoutMs) {
    delay(250);
  }

  return WiFi.status() == WL_CONNECTED;
}

bool start_access_point() {
  if (!WiFi.softAPConfig(kAccessPointIp, kGatewayIp, kSubnetMask)) {
    Serial.println("Wi-Fi softAPConfig failed");
    ui_set_status_message("Wi-Fi Error", "AP config failed");
    return false;
  }

  if (!WiFi.softAP(kAccessPointSsid, kAccessPointPassword)) {
    Serial.println("Wi-Fi softAP start failed");
    ui_set_status_message("Wi-Fi Error", "AP start failed");
    return false;
  }

  const IPAddress ip = WiFi.softAPIP();
  if (ip == IPAddress((uint32_t)0)) {
    Serial.println("Wi-Fi softAP has no IP");
    ui_set_status_message("Wi-Fi Error", "AP has no IP");
    return false;
  }

  const String url = "http://" + ip.toString() + "/";

  accessPointEnabled = true;
  refresh_ui();

  Serial.printf("Wi-Fi AP started: %s\n", kAccessPointSsid);
  Serial.printf("Wi-Fi URL: %s\n", url.c_str());
  return true;
}

void handleRoot() {
  const String baseUrl = "http://" + server.hostHeader();
  Serial.printf(
      "HTTP GET / from %s, heap=%u\n",
      server.client().remoteIP().toString().c_str(),
      ESP.getFreeHeap());
  server.send(200, "text/html", render_config_page(stationStatus, baseUrl));
}

void handleStatus() {
  server.send(200, "text/plain", stationStatus);
}

void handleBeep() {
  if (audio_play_beep()) {
    server.send(200, "text/plain", "ok");
    return;
  }

  server.send(500, "text/plain", "beep failed");
}

void handleConnect() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Missing ssid");
    return;
  }

  const String ssid = server.arg("ssid");
  const String password = server.arg("password");

  stationSsid = ssid;
  stationStatus = "Connecting to " + ssid;
  stationConnectPending = true;
  stationConnectStartedMs = millis();
  refresh_ui();

  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(true);
  WiFi.disconnect(false, true);
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.printf("Wi-Fi STA connection started: %s\n", ssid.c_str());
  server.send(202, "text/plain", stationStatus);
}

void handleForget() {
  stationStatus = "Forgetting saved WiFi credentials";
  stationSsid = "";
  stationConnectPending = false;
  forgetWifiPending = true;
  forgetWifiAtMs = millis() + kForgetDelayMs;
  server.send(200, "text/plain", stationStatus);
}

void process_pending_station_connect() {
  if (!stationConnectPending) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    stationConnectPending = false;
    stationStatus = "Connected to " + stationSsid + " (" + WiFi.localIP().toString() + ")";
    refresh_ui();
    Serial.printf("Wi-Fi STA connected: %s\n", stationStatus.c_str());
    return;
  }

  if (millis() - stationConnectStartedMs < kStationConnectTimeoutMs) {
    return;
  }

  stationConnectPending = false;
  stationStatus = "Connection failed for " + stationSsid;
  refresh_ui();
  Serial.printf("Wi-Fi STA connection failed: %s\n", stationSsid.c_str());
}

void process_pending_forget() {
  if (!forgetWifiPending || millis() < forgetWifiAtMs) {
    return;
  }

  forgetWifiPending = false;
  WiFi.persistent(true);
  WiFi.disconnect(false, true);
  stationStatus = "Not connected";
  stationSsid = "";

  if (!accessPointEnabled) {
    start_access_point();
  } else {
    refresh_ui();
  }

  Serial.println("Saved Wi-Fi credentials forgotten");
}
}  // namespace

void wifi_config_init() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(true);
  WiFi.begin();

  if (connect_station()) {
    stationSsid = WiFi.SSID();
    stationStatus = "Connected to saved Wi-Fi (" + WiFi.localIP().toString() + ")";
    Serial.printf("Wi-Fi STA auto-connected: %s\n", stationStatus.c_str());
    refresh_ui();
  } else {
    stationStatus = "Not connected";
    start_access_point();
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/beep", HTTP_POST, handleBeep);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/favicon.ico", HTTP_GET, []() {
    server.send(204);
  });
  server.begin();
}

void wifi_config_loop() {
  server.handleClient();
  process_pending_station_connect();
  process_pending_forget();
}
