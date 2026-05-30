#include "wifi_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>
#include <lvgl.h>

#include "audio/audio.h"
#include "debug/debug_log.h"
#include "generated/default_pet_sprites.h"
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
constexpr size_t kMaxSpriteUploadBytes = 512 * 1024;
constexpr uint16_t kMaxSpriteWidth = 240;
constexpr uint16_t kMaxSpriteHeight = 240;
constexpr size_t kMaxPetMessageLength = 240;
constexpr char kSpriteDir[] = "/sprites";
constexpr char kSpriteUploadPath[] = "/sprites/.upload.gif";
constexpr char kDefaultSpriteName[] = "idle";

WebServer server(80);
String stationStatus = "Not connected";
String stationSsid = "";
String activeSpriteName = "";
String temporarySpritePreviousName = "";
bool accessPointEnabled = false;
bool forgetWifiPending = false;
unsigned long forgetWifiAtMs = 0;
bool stationConnectPending = false;
unsigned long stationConnectStartedMs = 0;
bool spriteUploadRejected = false;
bool spriteUploadComplete = false;
size_t spriteUploadBytes = 0;
String spriteUploadName = "";
String spriteUploadError = "";
File spriteUploadFile;
unsigned long petSpriteExpiresAtMs = 0;
bool petSpriteExpires = false;
unsigned long petMessageExpiresAtMs = 0;
bool petMessageExpires = false;
bool spriteStorageReady = false;
bool defaultSpriteLoadPending = false;
unsigned long defaultSpriteLoadAtMs = 0;

void show_default_pet_sprite();

bool is_valid_sprite_name(const String &name) {
  if (name.length() == 0 || name.length() > 32) {
    return false;
  }

  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name[i];
    const bool valid =
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '_' ||
        c == '-';
    if (!valid) {
      return false;
    }
  }

  return true;
}

String sprite_file_path(const String &name) {
  return String(kSpriteDir) + "/" + name + ".gif";
}

String sprite_lvgl_path(const String &name) {
  return "S:" + sprite_file_path(name);
}

bool ensure_sprite_dir() {
  if (!spriteStorageReady) {
    return false;
  }

  if (LittleFS.exists(kSpriteDir)) {
    return true;
  }

  return LittleFS.mkdir(kSpriteDir);
}

void seed_default_pet_sprites() {
  for (size_t i = 0; i < default_pet_sprites::kSpriteCount; ++i) {
    const default_pet_sprites::Sprite &sprite = default_pet_sprites::kSprites[i];
    const String path = sprite_file_path(sprite.name);
    if (LittleFS.exists(path)) {
      continue;
    }

    File file = LittleFS.open(path, FILE_WRITE);
    if (!file) {
      Serial.printf("Could not create default sprite: %s\n", path.c_str());
      continue;
    }

    const size_t written = file.write(sprite.data, sprite.size);
    file.close();

    if (written != sprite.size) {
      LittleFS.remove(path);
      Serial.printf(
          "Default sprite write failed: %s wrote=%u expected=%u\n",
          path.c_str(),
          static_cast<unsigned>(written),
          static_cast<unsigned>(sprite.size));
      continue;
    }

    Serial.printf(
        "Default sprite installed: %s (%u bytes)\n",
        path.c_str(),
        static_cast<unsigned>(sprite.size));
  }
}

bool file_has_gif_header(const char *path) {
  if (!spriteStorageReady) {
    return false;
  }

  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    return false;
  }

  char header[6] = {};
  const size_t bytesRead = file.readBytes(header, sizeof(header));
  file.close();

  return bytesRead == sizeof(header) &&
         (memcmp(header, "GIF87a", sizeof(header)) == 0 ||
          memcmp(header, "GIF89a", sizeof(header)) == 0);
}

bool validate_gif_dimensions(const String &name, String &error) {
  uint16_t width = 0;
  uint16_t height = 0;
  const String path = sprite_lvgl_path(name);

  if (!lv_gif_get_size(path.c_str(), &width, &height)) {
    error = "Could not read GIF size";
    return false;
  }

  if (width == 0 || height == 0 || width > kMaxSpriteWidth || height > kMaxSpriteHeight) {
    error = "GIF must be between 1x1 and " + String(kMaxSpriteWidth) + "x" +
            String(kMaxSpriteHeight);
    return false;
  }

  return true;
}

bool delete_sprite(const String &name, String &error) {
  if (!spriteStorageReady) {
    error = "Sprite storage unavailable";
    return false;
  }

  if (!is_valid_sprite_name(name)) {
    error = "Invalid sprite name";
    return false;
  }

  const String filePath = sprite_file_path(name);
  if (!LittleFS.exists(filePath)) {
    error = "Sprite not found";
    return false;
  }

  if (!LittleFS.remove(filePath)) {
    error = "Could not delete sprite";
    return false;
  }

  if (spriteUploadName == name) {
    spriteUploadName = "";
  }

  if (temporarySpritePreviousName == name) {
    temporarySpritePreviousName = "";
  }

  if (activeSpriteName == name) {
    activeSpriteName = "";
    petSpriteExpires = false;
    petSpriteExpiresAtMs = 0;
    show_default_pet_sprite();
  }

  return true;
}

bool show_pet_sprite(const String &name, unsigned long ttlMs, String &error) {
  if (!spriteStorageReady) {
    error = "Sprite storage unavailable";
    return false;
  }

  if (!is_valid_sprite_name(name)) {
    error = "Invalid sprite name";
    return false;
  }

  defaultSpriteLoadPending = false;

  const String filePath = sprite_file_path(name);
  if (!LittleFS.exists(filePath)) {
    error = "Sprite not found";
    return false;
  }

  debug_log_heap("sprite-show-before-size");
  uint16_t width = 0;
  uint16_t height = 0;
  const size_t fileSize = LittleFS.open(filePath, FILE_READ).size();
  const String lvglPath = sprite_lvgl_path(name);
  lv_gif_get_size(lvglPath.c_str(), &width, &height);
  Serial.printf(
      "%s sprite %s: path=%s size=%u dims=%ux%u internal=%u largest_internal=%u psram=%u largest_psram=%u\n",
      ui_is_pet_page_active() ? "Loading" : "Queued",
      name.c_str(),
      lvglPath.c_str(),
      static_cast<unsigned>(fileSize),
      width,
      height,
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      ESP.getFreePsram(),
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  if (!ui_show_pet_sprite(name.c_str(), lvglPath.c_str())) {
    error = "Sprite failed to load";
    Serial.printf(
        "Sprite load failed: %s internal=%u largest_internal=%u psram=%u largest_psram=%u\n",
        name.c_str(),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        ESP.getFreePsram(),
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return false;
  }
  debug_log_heap("sprite-show-after-ui");

  if (ttlMs > 0) {
    temporarySpritePreviousName = activeSpriteName;
    petSpriteExpiresAtMs = millis() + ttlMs;
    petSpriteExpires = true;
  } else {
    temporarySpritePreviousName = "";
    petSpriteExpires = false;
  }

  activeSpriteName = name;
  return true;
}

void show_default_pet_sprite() {
  String error;
  if (!spriteStorageReady) {
    activeSpriteName = "";
    petSpriteExpires = false;
    ui_clear_pet_sprite("Sprite storage unavailable");
    return;
  }

  if (LittleFS.exists(sprite_file_path(kDefaultSpriteName))) {
    show_pet_sprite(kDefaultSpriteName, 0, error);
    return;
  }

  activeSpriteName = "";
  petSpriteExpires = false;
  ui_clear_pet_sprite("Upload idle.gif or show a sprite");
}

bool init_sprite_storage() {
  spriteStorageReady = LittleFS.begin(false);
  if (!spriteStorageReady) {
    Serial.println("LittleFS mount failed; refusing to auto-format so uploaded sprites are not erased");
    return false;
  }

  if (!ensure_sprite_dir()) {
    spriteStorageReady = false;
    Serial.println("Could not create sprite directory");
    return false;
  }

  seed_default_pet_sprites();

  Serial.printf(
      "LittleFS mounted: used=%u total=%u\n",
      static_cast<unsigned>(LittleFS.usedBytes()),
      static_cast<unsigned>(LittleFS.totalBytes()));
  return true;
}

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

void handleSpritesList() {
  JsonDocument doc;
  doc["storageAvailable"] = spriteStorageReady;
  doc["activeSprite"] = activeSpriteName;
  doc["defaultSpriteName"] = kDefaultSpriteName;
  JsonArray sprites = doc["sprites"].to<JsonArray>();

  File dir = spriteStorageReady ? LittleFS.open(kSpriteDir) : File();
  if (dir && dir.isDirectory()) {
    File file = dir.openNextFile();
    while (file) {
      const String path = file.path();
      if (!file.isDirectory() && path.endsWith(".gif") && !path.startsWith(String(kSpriteDir) + "/.")) {
        JsonObject sprite = sprites.add<JsonObject>();
        String name = path.substring(String(kSpriteDir).length() + 1, path.length() - 4);
        sprite["name"] = name;
        sprite["size"] = file.size();
        sprite["active"] = name == activeSpriteName;
        sprite["isDefault"] = name == kDefaultSpriteName;
      }

      file = dir.openNextFile();
    }
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSpriteDelete() {
  const String name = server.arg("name");

  String error;
  if (!delete_sprite(name, error)) {
    server.send(400, "text/plain", error);
    return;
  }

  server.send(200, "text/plain", "Sprite deleted: " + name);
}

void handleSpriteUploadData() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    spriteUploadRejected = false;
    spriteUploadComplete = false;
    spriteUploadBytes = 0;
    spriteUploadError = "";
    spriteUploadName = server.arg("name");

    if (!spriteStorageReady) {
      spriteUploadRejected = true;
      spriteUploadError = "Sprite storage unavailable";
      return;
    }

    if (!is_valid_sprite_name(spriteUploadName)) {
      spriteUploadRejected = true;
      spriteUploadError = "Invalid sprite name";
      return;
    }

    if (!ensure_sprite_dir()) {
      spriteUploadRejected = true;
      spriteUploadError = "Could not create sprite directory";
      return;
    }

    LittleFS.remove(kSpriteUploadPath);
    spriteUploadFile = LittleFS.open(kSpriteUploadPath, FILE_WRITE);
    if (!spriteUploadFile) {
      spriteUploadRejected = true;
      spriteUploadError = "Could not open upload file";
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (spriteUploadRejected) {
      return;
    }

    spriteUploadBytes += upload.currentSize;
    if (spriteUploadBytes > kMaxSpriteUploadBytes) {
      spriteUploadRejected = true;
      spriteUploadError = "GIF is too large";
      if (spriteUploadFile) {
        spriteUploadFile.close();
      }
      LittleFS.remove(kSpriteUploadPath);
      return;
    }

    if (!spriteUploadFile || spriteUploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
      spriteUploadRejected = true;
      spriteUploadError = "Could not write upload";
      if (spriteUploadFile) {
        spriteUploadFile.close();
      }
      LittleFS.remove(kSpriteUploadPath);
    }
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (spriteUploadFile) {
      spriteUploadFile.close();
    }

    if (spriteUploadRejected) {
      LittleFS.remove(kSpriteUploadPath);
      return;
    }

    if (spriteUploadBytes == 0 || !file_has_gif_header(kSpriteUploadPath)) {
      spriteUploadRejected = true;
      spriteUploadError = "Upload must be a GIF";
      LittleFS.remove(kSpriteUploadPath);
      return;
    }

    const String targetPath = sprite_file_path(spriteUploadName);
    LittleFS.remove(targetPath);
    if (!LittleFS.rename(kSpriteUploadPath, targetPath)) {
      spriteUploadRejected = true;
      spriteUploadError = "Could not save sprite";
      LittleFS.remove(kSpriteUploadPath);
      return;
    }

    String error;
    if (!validate_gif_dimensions(spriteUploadName, error)) {
      spriteUploadRejected = true;
      spriteUploadError = error;
      LittleFS.remove(targetPath);
      return;
    }

    spriteUploadComplete = true;
  }
}

void handleSpriteUploadComplete() {
  if (spriteUploadRejected) {
    server.send(400, "text/plain", spriteUploadError.length() == 0 ? "Upload failed" : spriteUploadError);
    return;
  }

  if (!spriteUploadComplete) {
    server.send(400, "text/plain", "No GIF uploaded");
    return;
  }

  if (activeSpriteName.length() == 0 || spriteUploadName == kDefaultSpriteName) {
    String error;
    show_pet_sprite(spriteUploadName, 0, error);
  }

  server.send(200, "text/plain", "Sprite uploaded: " + spriteUploadName);
}

void handlePetCommand() {
  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, server.arg("plain"));
  if (jsonError) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  const char *name = doc["name"] | doc["sprite"] | "";
  const unsigned long ttlMs = doc["ttlMs"] | 0;

  String error;
  if (!show_pet_sprite(name, ttlMs, error)) {
    server.send(400, "text/plain", error);
    return;
  }

  server.send(200, "text/plain", "Showing sprite: " + String(name));
}

void handlePetMessageCommand() {
  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, server.arg("plain"));
  if (jsonError) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  const char *message = doc["message"] | doc["text"] | "";
  const unsigned long ttlMs = doc["ttlMs"] | 0;
  const size_t messageLength = strlen(message);

  if (messageLength == 0) {
    server.send(400, "text/plain", "Missing message");
    return;
  }

  if (messageLength > kMaxPetMessageLength) {
    server.send(400, "text/plain", "Message is too long");
    return;
  }

  if (!ui_show_pet_message(message)) {
    server.send(500, "text/plain", "Could not show message");
    return;
  }

  if (ttlMs > 0) {
    petMessageExpiresAtMs = millis() + ttlMs;
    petMessageExpires = true;
  } else {
    petMessageExpiresAtMs = 0;
    petMessageExpires = false;
  }

  server.send(200, "text/plain", "Showing message");
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

void process_pending_pet_expiry() {
  if (!petSpriteExpires || static_cast<long>(millis() - petSpriteExpiresAtMs) < 0) {
    return;
  }

  petSpriteExpires = false;

  String error;
  if (temporarySpritePreviousName.length() > 0 &&
      show_pet_sprite(temporarySpritePreviousName, 0, error)) {
    temporarySpritePreviousName = "";
    return;
  }

  temporarySpritePreviousName = "";
  show_default_pet_sprite();
}

void process_pending_pet_message_expiry() {
  if (!petMessageExpires || static_cast<long>(millis() - petMessageExpiresAtMs) < 0) {
    return;
  }

  petMessageExpires = false;
  petMessageExpiresAtMs = 0;
  ui_clear_pet_message();
}

void process_pending_default_sprite_load() {
  if (!defaultSpriteLoadPending ||
      !ui_is_pet_page_active() ||
      static_cast<long>(millis() - defaultSpriteLoadAtMs) < 0) {
    return;
  }

  defaultSpriteLoadPending = false;
  show_default_pet_sprite();
}
}  // namespace

void wifi_config_init() {
  init_sprite_storage();
  defaultSpriteLoadPending = true;
  defaultSpriteLoadAtMs = millis() + 250;

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
  server.on("/sprites", HTTP_GET, handleSpritesList);
  server.on("/sprites", HTTP_DELETE, handleSpriteDelete);
  server.on("/sprites/upload", HTTP_POST, handleSpriteUploadComplete, handleSpriteUploadData);
  server.on("/pet", HTTP_POST, handlePetCommand);
  server.on("/pet/message", HTTP_POST, handlePetMessageCommand);
  server.on("/connect", HTTP_POST, handleConnect);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/favicon.ico", HTTP_GET, []() {
    server.send(204);
  });
  server.begin();
}

void wifi_config_loop() {
  server.handleClient();
  process_pending_default_sprite_load();
  process_pending_station_connect();
  process_pending_forget();
  process_pending_pet_expiry();
  process_pending_pet_message_expiry();
}
