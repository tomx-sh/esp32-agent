#include "wifi_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstring>
#include <ctime>
#include <lvgl.h>

#include "app_network.h"
#include "audio/audio.h"
#include "debug/debug_log.h"
#include "display_control.h"
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
constexpr uint16_t kMaxCodexResetCredits = 999;
constexpr char kPrimaryNtpServer[] = "pool.ntp.org";
constexpr char kSecondaryNtpServer[] = "time.nist.gov";
constexpr char kSpriteDir[] = "/sprites";
constexpr char kSpriteUploadPath[] = "/sprites/.upload.gif";
constexpr char kDefaultSpriteName[] = "idle";
constexpr char kPetPackPath[] = "/pet-pack.json";
constexpr char kPetPackBackupPath[] = "/pet-pack.backup.json";
constexpr char kPetPackUploadPath[] = "/pet-pack.upload.json";
constexpr size_t kPetStateCount = 9;
constexpr const char *kPetStates[kPetStateCount] = {
    "idle",
    "running-right",
    "running-left",
    "waving",
    "jumping",
    "failed",
    "waiting",
    "running",
    "review",
};

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
String activePetPackId = "";
String activePetPackDisplayName = "";
String activePetPackSourceHash = "";
uint8_t activePetPackSpriteVersion = 0;
String activePetPackSprites[kPetStateCount];

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

bool parse_uint8_arg(const String &value, uint8_t &result) {
  if (value.length() == 0) {
    return false;
  }

  for (size_t i = 0; i < value.length(); ++i) {
    if (!isDigit(value[i])) {
      return false;
    }
  }

  const int parsed = value.toInt();
  if (parsed < kDisplayBrightnessMin || parsed > kDisplayBrightnessMax) {
    return false;
  }

  result = static_cast<uint8_t>(parsed);
  return true;
}

bool parse_brightness_request(uint8_t &brightness, String &error) {
  if (server.hasArg("plain") && server.arg("plain").length() > 0) {
    JsonDocument doc;
    DeserializationError jsonError = deserializeJson(doc, server.arg("plain"));
    if (jsonError) {
      error = "Invalid JSON";
      return false;
    }

    JsonVariant value = doc["brightness"];
    if (value.isNull()) {
      value = doc["value"];
    }

    if (!value.is<int>()) {
      error = "Missing brightness";
      return false;
    }

    const int parsed = value.as<int>();
    if (parsed < kDisplayBrightnessMin || parsed > kDisplayBrightnessMax) {
      error = "Brightness must be between 0 and 255";
      return false;
    }

    brightness = static_cast<uint8_t>(parsed);
    return true;
  }

  if (server.hasArg("brightness") && parse_uint8_arg(server.arg("brightness"), brightness)) {
    return true;
  }

  if (server.hasArg("value") && parse_uint8_arg(server.arg("value"), brightness)) {
    return true;
  }

  error = "Brightness must be between 0 and 255";
  return false;
}

bool parse_codex_usage_request(
    uint8_t &remainingPercent,
    uint64_t &resetAt,
    uint16_t &resetCredits,
    String &error) {
  if (!server.hasArg("plain") || server.arg("plain").length() == 0) {
    error = "Expected a JSON body";
    return false;
  }

  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, server.arg("plain"));
  if (jsonError) {
    error = "Invalid JSON";
    return false;
  }

  JsonVariant remainingValue = doc["remainingPercent"];
  if (!remainingValue.is<int>()) {
    error = "Missing remainingPercent";
    return false;
  }
  const int parsedRemaining = remainingValue.as<int>();
  if (parsedRemaining < 0 || parsedRemaining > 100) {
    error = "remainingPercent must be between 0 and 100";
    return false;
  }

  JsonVariant resetValue = doc["resetAt"];
  if (!resetValue.is<uint64_t>()) {
    error = "Missing or invalid resetAt Unix timestamp";
    return false;
  }

  JsonVariant resetCreditsValue = doc["resetCredits"];
  if (!resetCreditsValue.is<int>()) {
    error = "Missing resetCredits";
    return false;
  }
  const int parsedResetCredits = resetCreditsValue.as<int>();
  if (parsedResetCredits < 0 || parsedResetCredits > kMaxCodexResetCredits) {
    error = "resetCredits must be between 0 and 999";
    return false;
  }

  remainingPercent = static_cast<uint8_t>(parsedRemaining);
  resetAt = resetValue.as<uint64_t>();
  resetCredits = static_cast<uint16_t>(parsedResetCredits);
  return true;
}

bool parse_codex_context_request(uint8_t &remainingPercent, String &error) {
  if (!server.hasArg("plain") || server.arg("plain").length() == 0) {
    error = "Expected a JSON body";
    return false;
  }

  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, server.arg("plain"));
  if (jsonError) {
    error = "Invalid JSON";
    return false;
  }

  JsonVariant value = doc["remainingPercent"];
  if (!value.is<int>()) {
    error = "Missing remainingPercent";
    return false;
  }

  const int parsed = value.as<int>();
  if (parsed < 0 || parsed > 100) {
    error = "remainingPercent must be between 0 and 100";
    return false;
  }

  remainingPercent = static_cast<uint8_t>(parsed);
  return true;
}

bool parse_non_negative_int_arg(const String &value, int &result) {
  if (value.length() == 0) {
    return false;
  }

  for (size_t i = 0; i < value.length(); ++i) {
    if (!isDigit(value[i])) {
      return false;
    }
  }

  result = value.toInt();
  return true;
}

bool parse_page_request(size_t &index, String &error) {
  int parsed = -1;

  if (server.hasArg("plain") && server.arg("plain").length() > 0) {
    JsonDocument doc;
    DeserializationError jsonError = deserializeJson(doc, server.arg("plain"));
    if (jsonError) {
      error = "Invalid JSON";
      return false;
    }

    JsonVariant value = doc["index"];
    if (value.isNull()) {
      value = doc["page"];
    }

    if (!value.is<int>()) {
      error = "Missing page index";
      return false;
    }

    parsed = value.as<int>();
  } else if (server.hasArg("index")) {
    if (!parse_non_negative_int_arg(server.arg("index"), parsed)) {
      error = "Invalid page index";
      return false;
    }
  } else if (server.hasArg("page")) {
    if (!parse_non_negative_int_arg(server.arg("page"), parsed)) {
      error = "Invalid page index";
      return false;
    }
  } else {
    error = "Missing page index";
    return false;
  }

  if (parsed < 0 || static_cast<size_t>(parsed) >= ui_get_page_count()) {
    error = "Page index out of range";
    return false;
  }

  index = static_cast<size_t>(parsed);
  return true;
}

String sprite_file_path(const String &name) {
  return String(kSpriteDir) + "/" + name + ".gif";
}

String sprite_lvgl_path(const String &name) {
  return "S:" + sprite_file_path(name);
}

int pet_state_index(const String &name) {
  for (size_t i = 0; i < kPetStateCount; ++i) {
    if (name == kPetStates[i]) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

String resolve_sprite_name(const String &name) {
  const int stateIndex = pet_state_index(name);
  if (stateIndex >= 0 && activePetPackSprites[stateIndex].length() > 0) {
    return activePetPackSprites[stateIndex];
  }
  return name;
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

void remove_legacy_pet_sprites() {
  constexpr const char *legacyNames[] = {
      "claude-code-notification",
      "claude-code-post-tool-use",
      "claude-code-pre-tool-use",
      "claude-code-session-start",
      "claude-code-stop",
      "claude-code-user-prompt-submit",
      "codex-failed",
      "codex-idle",
      "codex-jumping",
      "codex-review",
      "codex-running-left",
      "codex-running-right",
      "codex-thinking",
      "codex-waiting",
      "codex-waving",
  };
  for (const char *name : legacyNames) {
    const String path = sprite_file_path(name);
    if (LittleFS.exists(path)) {
      LittleFS.remove(path);
    }
  }
}

void clear_active_pet_pack() {
  activePetPackId = "";
  activePetPackDisplayName = "";
  activePetPackSourceHash = "";
  activePetPackSpriteVersion = 0;
  for (size_t i = 0; i < kPetStateCount; ++i) {
    activePetPackSprites[i] = "";
  }
}

bool validate_pet_pack(JsonObjectConst root, String &error) {
  const char *petId = root["petId"] | "";
  const char *displayName = root["displayName"] | "";
  const char *sourceHash = root["sourceHash"] | "";
  const int spriteVersion = root["spriteVersion"] | 0;
  JsonObjectConst sprites = root["sprites"].as<JsonObjectConst>();

  if (strlen(petId) == 0 || strlen(petId) > 96) {
    error = "Invalid petId";
    return false;
  }
  if (strlen(displayName) == 0 || strlen(displayName) > 96) {
    error = "Invalid displayName";
    return false;
  }
  if (strlen(sourceHash) != 64) {
    error = "Invalid sourceHash";
    return false;
  }
  if (spriteVersion != 1 && spriteVersion != 2) {
    error = "spriteVersion must be 1 or 2";
    return false;
  }
  if (sprites.isNull()) {
    error = "Missing sprites";
    return false;
  }
  for (size_t i = 0; i < kPetStateCount; ++i) {
    const char *spriteName = sprites[kPetStates[i]] | "";
    const String spriteNameValue(spriteName);
    if (!is_valid_sprite_name(spriteNameValue)) {
      error = "Invalid sprite name for state " + String(kPetStates[i]) + ": " + spriteNameValue;
      return false;
    }
    const String spritePath = sprite_file_path(spriteNameValue);
    if (!LittleFS.exists(spritePath)) {
      error = "Sprite file not found for state " + String(kPetStates[i]) + ": " + spritePath;
      return false;
    }
  }
  return true;
}

void apply_pet_pack(JsonObjectConst root) {
  activePetPackId = root["petId"].as<const char *>();
  activePetPackDisplayName = root["displayName"].as<const char *>();
  activePetPackSourceHash = root["sourceHash"].as<const char *>();
  activePetPackSpriteVersion = static_cast<uint8_t>(root["spriteVersion"].as<int>());
  JsonObjectConst sprites = root["sprites"].as<JsonObjectConst>();
  for (size_t i = 0; i < kPetStateCount; ++i) {
    activePetPackSprites[i] = sprites[kPetStates[i]].as<const char *>();
  }
}

bool persist_pet_pack(const String &json, String &error) {
  LittleFS.remove(kPetPackUploadPath);
  File upload = LittleFS.open(kPetPackUploadPath, FILE_WRITE);
  if (!upload) {
    error = "Could not create pet-pack manifest";
    return false;
  }
  const size_t written = upload.print(json);
  upload.close();
  if (written != json.length()) {
    LittleFS.remove(kPetPackUploadPath);
    error = "Could not write pet-pack manifest";
    return false;
  }

  LittleFS.remove(kPetPackBackupPath);
  const bool hadCurrent = LittleFS.exists(kPetPackPath);
  if (hadCurrent && !LittleFS.rename(kPetPackPath, kPetPackBackupPath)) {
    LittleFS.remove(kPetPackUploadPath);
    error = "Could not stage current pet-pack manifest";
    return false;
  }
  if (!LittleFS.rename(kPetPackUploadPath, kPetPackPath)) {
    if (hadCurrent) {
      LittleFS.rename(kPetPackBackupPath, kPetPackPath);
    }
    LittleFS.remove(kPetPackUploadPath);
    error = "Could not activate pet-pack manifest";
    return false;
  }
  LittleFS.remove(kPetPackBackupPath);
  return true;
}

void load_pet_pack() {
  clear_active_pet_pack();
  if (!LittleFS.exists(kPetPackPath) && LittleFS.exists(kPetPackBackupPath)) {
    LittleFS.rename(kPetPackBackupPath, kPetPackPath);
  }
  File file = LittleFS.open(kPetPackPath, FILE_READ);
  if (!file) {
    return;
  }
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, file);
  file.close();
  String error;
  if (jsonError || !validate_pet_pack(doc.as<JsonObjectConst>(), error)) {
    Serial.printf("Ignoring invalid pet-pack manifest: %s\n", jsonError ? jsonError.c_str() : error.c_str());
    return;
  }
  apply_pet_pack(doc.as<JsonObjectConst>());
  Serial.printf("Loaded pet pack: %s (%s)\n", activePetPackDisplayName.c_str(), activePetPackId.c_str());
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

  for (size_t i = 0; i < kPetStateCount; ++i) {
    if (activePetPackSprites[i] == name) {
      error = "Sprite belongs to the active pet pack";
      return false;
    }
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

  const String resolvedName = resolve_sprite_name(name);
  if (!is_valid_sprite_name(resolvedName)) {
    error = "Invalid sprite mapping";
    return false;
  }

  defaultSpriteLoadPending = false;

  const String filePath = sprite_file_path(resolvedName);
  if (!LittleFS.exists(filePath)) {
    error = "Sprite not found";
    return false;
  }

  debug_log_heap("sprite-show-before-size");
  uint16_t width = 0;
  uint16_t height = 0;
  const size_t fileSize = LittleFS.open(filePath, FILE_READ).size();
  const String lvglPath = sprite_lvgl_path(resolvedName);
  lv_gif_get_size(lvglPath.c_str(), &width, &height);
  Serial.printf(
      "%s sprite %s: path=%s size=%u dims=%ux%u internal=%u largest_internal=%u psram=%u largest_psram=%u\n",
      ui_is_pet_page_active() ? "Loading" : "Queued",
      resolvedName.c_str(),
      lvglPath.c_str(),
      static_cast<unsigned>(fileSize),
      width,
      height,
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      ESP.getFreePsram(),
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  if (!ui_show_pet_sprite(resolvedName.c_str(), lvglPath.c_str())) {
    error = "Sprite failed to load";
    Serial.printf(
        "Sprite load failed: %s internal=%u largest_internal=%u psram=%u largest_psram=%u\n",
        resolvedName.c_str(),
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        ESP.getFreePsram(),
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return false;
  }
  debug_log_heap("sprite-show-after-ui");

  if (ttlMs > 0) {
    temporarySpritePreviousName = pet_state_index(name) >= 0
        ? resolve_sprite_name(kDefaultSpriteName)
        : activeSpriteName;
    petSpriteExpiresAtMs = millis() + ttlMs;
    petSpriteExpires = true;
  } else {
    temporarySpritePreviousName = "";
    petSpriteExpires = false;
  }

  activeSpriteName = resolvedName;
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
  remove_legacy_pet_sprites();
  load_pet_pack();

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
    details = "\nDashboard:\nhttp://" + String(app_network::kHostname) + ".local/" +
              "\nor http://" + WiFi.localIP().toString() + "/";

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

void handleBrightnessGet() {
  JsonDocument doc;
  doc["brightness"] = display_control_get_brightness();
  doc["min"] = kDisplayBrightnessMin;
  doc["max"] = kDisplayBrightnessMax;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleBrightnessPost() {
  uint8_t brightness = 0;
  String error;
  if (!parse_brightness_request(brightness, error)) {
    server.send(400, "text/plain", error);
    return;
  }

  display_control_set_brightness(brightness, true);
  server.send(200, "text/plain", "Brightness set to " + String(brightness));
}

void handleCodexUsageGet() {
  JsonDocument doc;
  doc["remainingPercent"] = ui_get_codex_quota_remaining_percent();
  doc["resetAt"] = ui_get_codex_reset_at();
  doc["resetCredits"] = ui_get_codex_reset_credits();
  doc["min"] = 0;
  doc["max"] = 100;
  doc["resetCreditsMax"] = kMaxCodexResetCredits;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCodexUsagePost() {
  uint8_t remainingPercent = 0;
  uint64_t resetAt = 0;
  uint16_t resetCredits = 0;
  String error;
  if (!parse_codex_usage_request(remainingPercent, resetAt, resetCredits, error)) {
    server.send(400, "text/plain", error);
    return;
  }

  ui_set_codex_quota_remaining_percent(remainingPercent);
  ui_set_codex_reset_at(resetAt);
  ui_set_codex_reset_credits(resetCredits);
  char response[144];
  snprintf(
      response,
      sizeof(response),
      "Codex quota set to %u%% left (resetAt %llu, %u reset credits)",
      static_cast<unsigned int>(remainingPercent),
      static_cast<unsigned long long>(resetAt),
      static_cast<unsigned int>(resetCredits));
  server.send(
      200,
      "text/plain",
      response);
}

void handleCodexMessageGet() {
  JsonDocument doc;
  doc["message"] = ui_get_codex_message();
  doc["muted"] = ui_get_codex_message_muted();
  doc["maxLength"] = kMaxCodexMessageLength;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCodexMessagePost() {
  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, server.arg("plain"));
  if (jsonError) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  JsonVariant messageValue = doc["message"];
  if (!messageValue.is<const char *>()) {
    server.send(400, "text/plain", "Missing message");
    return;
  }

  const char *message = messageValue.as<const char *>();
  const size_t messageLength = strlen(message);
  if (messageLength > kMaxCodexMessageLength) {
    server.send(400, "text/plain", "Message is too long");
    return;
  }

  if (strchr(message, '\n') != nullptr || strchr(message, '\r') != nullptr) {
    server.send(400, "text/plain", "Message must be one line");
    return;
  }

  bool muted = ui_get_codex_message_muted();
  JsonVariant mutedValue = doc["muted"];
  if (!mutedValue.isNull()) {
    if (!mutedValue.is<bool>()) {
      server.send(400, "text/plain", "Muted must be a boolean");
      return;
    }
    muted = mutedValue.as<bool>();
  }

  ui_set_codex_message(message, muted);
  server.send(
      200,
      "text/plain",
      messageLength == 0 ? "Codex message cleared" : "Codex message set");
}

void handleCodexContextGet() {
  JsonDocument doc;
  doc["remainingPercent"] = ui_get_codex_context_remaining_percent();
  doc["min"] = 0;
  doc["max"] = 100;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCodexContextPost() {
  uint8_t remainingPercent = 0;
  String error;
  if (!parse_codex_context_request(remainingPercent, error)) {
    server.send(400, "text/plain", error);
    return;
  }

  ui_set_codex_context_remaining_percent(remainingPercent);
  server.send(
      200,
      "text/plain",
      "Codex context remaining set to " + String(remainingPercent) + "%");
}

void handlePagesGet() {
  JsonDocument doc;
  const size_t pageCount = ui_get_page_count();
  const int32_t activeIndex = ui_get_active_page_index();

  doc["count"] = pageCount;
  doc["activeIndex"] = activeIndex;
  JsonArray pages = doc["pages"].to<JsonArray>();

  for (size_t i = 0; i < pageCount; ++i) {
    JsonObject page = pages.add<JsonObject>();
    page["index"] = i;
    page["name"] = ui_get_page_name(i);
    page["active"] = activeIndex == static_cast<int32_t>(i);
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handlePagePost() {
  size_t index = 0;
  String error;
  if (!parse_page_request(index, error)) {
    server.send(400, "text/plain", error);
    return;
  }

  if (!ui_set_active_page_index(index, true)) {
    server.send(400, "text/plain", "Could not change page");
    return;
  }

  server.send(
      200,
      "text/plain",
      "Showing page " + String(index) + ": " + String(ui_get_page_name(index)));
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

void handlePetPackGet() {
  JsonDocument doc;
  doc["petId"] = activePetPackId;
  doc["displayName"] = activePetPackDisplayName;
  doc["sourceHash"] = activePetPackSourceHash;
  doc["spriteVersion"] = activePetPackSpriteVersion;
  JsonObject sprites = doc["sprites"].to<JsonObject>();
  for (size_t i = 0; i < kPetStateCount; ++i) {
    if (activePetPackSprites[i].length() > 0) {
      sprites[kPetStates[i]] = activePetPackSprites[i];
    }
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handlePetPackPost() {
  if (!spriteStorageReady) {
    server.send(503, "text/plain", "Sprite storage unavailable");
    return;
  }
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, server.arg("plain"));
  if (jsonError) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  String error;
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  if (!validate_pet_pack(root, error)) {
    server.send(400, "text/plain", error);
    return;
  }
  String manifest;
  serializeJson(doc, manifest);
  if (!persist_pet_pack(manifest, error)) {
    server.send(500, "text/plain", error);
    return;
  }
  apply_pet_pack(root);
  temporarySpritePreviousName = "";
  petSpriteExpires = false;
  show_default_pet_sprite();
  server.send(200, "text/plain", "Activated pet pack: " + activePetPackDisplayName);
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

  WiFi.setHostname(app_network::kHostname);
  WiFi.mode(WIFI_AP_STA);
  WiFi.enableIPv6();
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

  WiFi.setHostname(app_network::kHostname);
  WiFi.mode(WIFI_AP_STA);
  WiFi.enableIPv6();
  WiFi.persistent(true);
  WiFi.begin();
  configTime(0, 0, kPrimaryNtpServer, kSecondaryNtpServer);

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
  server.on("/brightness", HTTP_GET, handleBrightnessGet);
  server.on("/brightness", HTTP_POST, handleBrightnessPost);
  server.on("/codex/usage", HTTP_GET, handleCodexUsageGet);
  server.on("/codex/usage", HTTP_POST, handleCodexUsagePost);
  server.on("/codex/message", HTTP_GET, handleCodexMessageGet);
  server.on("/codex/message", HTTP_POST, handleCodexMessagePost);
  server.on("/codex/context", HTTP_GET, handleCodexContextGet);
  server.on("/codex/context", HTTP_POST, handleCodexContextPost);
  server.on("/pages", HTTP_GET, handlePagesGet);
  server.on("/page", HTTP_POST, handlePagePost);
  server.on("/beep", HTTP_POST, handleBeep);
  server.on("/sprites", HTTP_GET, handleSpritesList);
  server.on("/sprites", HTTP_DELETE, handleSpriteDelete);
  server.on("/sprites/upload", HTTP_POST, handleSpriteUploadComplete, handleSpriteUploadData);
  server.on("/pet", HTTP_POST, handlePetCommand);
  server.on("/pet-pack", HTTP_GET, handlePetPackGet);
  server.on("/pet-pack", HTTP_POST, handlePetPackPost);
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
