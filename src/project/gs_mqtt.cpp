#include "project/gs_mqtt.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "project/gs_state.h"
#include "project/matrix/matrix.h"
#include "project/modes/modes.h"
#include "services/device_name.h"
#include "services/ota_state.h"
#include "services/settings_registry.h"
#include "services/wifi_manager.h"
#include "app_config.h"

#define MQTT_RETRY_MS       5000    // v1: a connect attempt every 5 s
#define GS_MQTT_TIMEOUT_S 2       // s; connect() blocks the loop for at most this
#define MQTT_BUFFER         1024    // discovery configs are ~600 bytes
#define STATE_REFRESH_MS    60000   // RSSI moves; republish now and then anyway

static WiFiClient   s_net;
static PubSubClient s_mqtt(s_net);

static char s_will[96], s_in[96], s_room_state[64];
static char s_ha[48];               // glowingsquare/<device id>
static uint32_t s_last_try_ms = 0, s_last_state_ms = 0;
static bool s_state_dirty = true;
static uint8_t s_last_on_brightness = 255;

static const char *MODE_NAMES[] = { "Tube", "Flights", "Unifi" };
static const char *PARTY_NAMES[] = { "off", "chill", "on" };

bool mqttConnected() { return s_mqtt.connected(); }
void mqttStateChanged() { s_state_dirty = true; }

static void buildTopics() {
  const char *name = g_mqtt_name[0] ? g_mqtt_name : modeOps(g_mode).mqtt_name;
  snprintf(s_will, sizeof(s_will), "%s/%s/mqtt", g_mqtt_room, name);
  snprintf(s_in, sizeof(s_in), "%s/%s/in", g_mqtt_room, name);
  snprintf(s_room_state, sizeof(s_room_state), "%s/state", g_mqtt_room);
  snprintf(s_ha, sizeof(s_ha), APP_SLUG "/%s", wifiManagerDeviceId());
}

// ---- incoming -------------------------------------------------------------

static void setBrightness(int v) {
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  g_brightness_target = (uint8_t)v;
  if (v > 0) s_last_on_brightness = (uint8_t)v;
  s_state_dirty = true;
}

static bool setParty(const char *p) {
  for (uint8_t i = 0; i < 3; i++) {
    if (strcmp(p, PARTY_NAMES[i]) != 0) continue;
    if (g_party != i) logSDf("Party mode: %s", p);
    g_party = i;
    s_state_dirty = true;
    return true;
  }
  return false;
}

static void onMessage(char *topic, uint8_t *payload, unsigned int len) {
  char text[128];
  len = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
  memcpy(text, payload, len);
  text[len] = '\0';
  JsonDocument doc;
  const bool is_json = !deserializeJson(doc, text);

  if (!strcmp(topic, s_room_state)) {
    if (is_json && doc["party"].is<const char *>()) setParty(doc["party"]);
    return;
  }
  if (!strcmp(topic, s_in)) {
    if (is_json && !doc["brightness"].isNull()) setBrightness(doc["brightness"] | 0);
    return;
  }
  char t[96];
  snprintf(t, sizeof(t), "%s/light/set", s_ha);
  if (!strcmp(topic, t) && is_json) {
    const char *state = doc["state"] | "";
    if (!doc["brightness"].isNull()) setBrightness(doc["brightness"] | 0);
    else if (!strcmp(state, "ON"))   setBrightness(s_last_on_brightness ? s_last_on_brightness : 255);
    if (!strcmp(state, "OFF"))       setBrightness(0);
    return;
  }
  snprintf(t, sizeof(t), "%s/mode/set", s_ha);
  if (!strcmp(topic, t)) {
    for (uint8_t i = 0; i < MODE_COUNT; i++) {
      if (strcasecmp(text, MODE_NAMES[i]) != 0) continue;
      const SettingDesc *s = settingById("mode");
      if (s) settingSet(*s, i);   // the same path as the web: NVS, on_change
    }
    s_state_dirty = true;
    return;
  }
  snprintf(t, sizeof(t), "%s/party/set", s_ha);
  if (!strcmp(topic, t)) setParty(text);
}

// ---- Home Assistant discovery ----------------------------------------------

static void addDevice(JsonDocument &d) {
  JsonObject dev = d["device"].to<JsonObject>();
  dev["identifiers"][0] = wifiManagerDeviceId();
  dev["name"] = deviceLabel();
  dev["manufacturer"] = "Glowing Square";
  dev["model"] = String("64x32 matrix (") + matrixDriverName() + ")";
  dev["sw_version"] = FW_VERSION;
  dev["configuration_url"] = String("http://") + WiFi.localIP().toString() + "/";
  d["availability_topic"] = s_will;
  d["payload_available"] = "connected";
  d["payload_not_available"] = "disconnected";
}

static void publishConfig(const char *component, const char *object, JsonDocument &d) {
  char topic[128], uid[64];
  snprintf(uid, sizeof(uid), "%s_%s", wifiManagerDeviceId(), object);
  d["unique_id"] = uid;
  d["object_id"] = uid;
  addDevice(d);
  snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config", component, wifiManagerDeviceId(), object);
  String out;
  serializeJson(d, out);
  s_mqtt.publish(topic, out.c_str(), true);
}

static void publishDiscovery() {
  char state[80], cmd[80];
  snprintf(state, sizeof(state), "%s/state", s_ha);
  { JsonDocument d;
    snprintf(cmd, sizeof(cmd), "%s/light/set", s_ha);
    d["name"] = nullptr;   // the device's own name
    d["schema"] = "json";
    d["state_topic"] = state;
    d["command_topic"] = cmd;
    d["brightness"] = true;
    d["brightness_scale"] = 255;
    publishConfig("light", "light", d); }
  { JsonDocument d;
    snprintf(cmd, sizeof(cmd), "%s/mode/set", s_ha);
    d["name"] = "Mode";
    d["icon"] = "mdi:view-dashboard";
    d["state_topic"] = state;
    d["value_template"] = "{{ value_json.mode }}";
    d["command_topic"] = cmd;
    for (const char *m : MODE_NAMES) d["options"].add(m);
    publishConfig("select", "mode", d); }
  { JsonDocument d;
    snprintf(cmd, sizeof(cmd), "%s/party/set", s_ha);
    d["name"] = "Party mode";
    d["icon"] = "mdi:party-popper";
    d["state_topic"] = state;
    d["value_template"] = "{{ value_json.party }}";
    d["command_topic"] = cmd;
    for (const char *p : PARTY_NAMES) d["options"].add(p);
    publishConfig("select", "party", d); }
  { JsonDocument d;
    d["name"] = "WiFi signal";
    d["state_topic"] = state;
    d["value_template"] = "{{ value_json.rssi }}";
    d["device_class"] = "signal_strength";
    d["unit_of_measurement"] = "dBm";
    d["entity_category"] = "diagnostic";
    publishConfig("sensor", "rssi", d); }
  { JsonDocument d;
    char upd[80];
    snprintf(upd, sizeof(upd), "%s/update", s_ha);
    d["name"] = "Firmware";
    d["device_class"] = "firmware";
    d["state_topic"] = upd;
    d["entity_category"] = "diagnostic";
    publishConfig("update", "firmware", d); }
}

// ---- state -------------------------------------------------------------------

static void publishState() {
  s_state_dirty = false;
  s_last_state_ms = millis();
  if (!g_mqtt_ha) return;
  JsonDocument d;
  d["state"] = g_brightness_target ? "ON" : "OFF";
  d["brightness"] = g_brightness_target ? g_brightness_target : s_last_on_brightness;
  d["mode"] = MODE_NAMES[g_mode < MODE_COUNT ? g_mode : 0];
  d["party"] = PARTY_NAMES[g_party < 3 ? g_party : 0];
  d["rssi"] = WiFi.RSSI();
  char topic[80];
  snprintf(topic, sizeof(topic), "%s/state", s_ha);
  String out;
  serializeJson(d, out);
  s_mqtt.publish(topic, out.c_str(), true);

  JsonDocument u;
  u["installed_version"] = FW_VERSION;
  u["latest_version"] = gh_latest_version[0] ? gh_latest_version : FW_VERSION;
  u["release_url"] = "https://github.com/" APP_GH_REPO "/releases";
  snprintf(topic, sizeof(topic), "%s/update", s_ha);
  out = "";
  serializeJson(u, out);
  s_mqtt.publish(topic, out.c_str(), true);
}

// ---- connection --------------------------------------------------------------

static bool connect() {
  buildTopics();
  s_mqtt.setServer(g_mqtt_host, (uint16_t)g_mqtt_port);
  // The device id, as v1 used the hostname: stable, and unique on a broker
  // with several of these.
  const bool ok = s_mqtt.connect(wifiManagerDeviceId(),
                                 g_mqtt_user[0] ? g_mqtt_user : nullptr,
                                 g_mqtt_pass[0] ? g_mqtt_pass : nullptr,
                                 s_will, 0, true, "disconnected");
  if (!ok) return false;
  s_mqtt.publish(s_will, "connected", true);
  s_mqtt.subscribe(s_in);
  s_mqtt.subscribe(s_room_state);
  if (g_mqtt_ha) {
    char t[96];
    snprintf(t, sizeof(t), "%s/+/set", s_ha);
    s_mqtt.subscribe(t);
    publishDiscovery();
  }
  logSDf("MQTT: connected to %s:%d as %s", g_mqtt_host, (int)g_mqtt_port, s_will);
  s_state_dirty = true;
  return true;
}

void mqttBegin() {
  s_mqtt.setCallback(onMessage);
  s_mqtt.setBufferSize(MQTT_BUFFER);
  s_mqtt.setSocketTimeout(GS_MQTT_TIMEOUT_S);
  s_net.setTimeout(GS_MQTT_TIMEOUT_S * 1000);
  s_last_on_brightness = g_brightness_target ? g_brightness_target : 255;
}

void mqttTick() {
  static char s_topics_for[200] = "";
  if (!g_mqtt_on || !g_mqtt_host[0] || !wifi_ok) {
    if (s_mqtt.connected()) s_mqtt.disconnect();
    return;
  }
  // A new room, device name or mode moves the topics: reconnect under them.
  char key[200];
  snprintf(key, sizeof(key), "%s|%s|%s|%u|%s|%ld|%d", g_mqtt_room, g_mqtt_name, g_mqtt_host,
           (unsigned)g_mode, g_mqtt_user, (long)g_mqtt_port, (int)g_mqtt_ha);
  if (strcmp(key, s_topics_for) != 0) {
    snprintf(s_topics_for, sizeof(s_topics_for), "%s", key);
    if (s_mqtt.connected()) {
      s_mqtt.publish(s_will, "disconnected", true);
      s_mqtt.disconnect();
    }
    s_last_try_ms = 0;
  }
  if (!s_mqtt.connected()) {
    if (s_last_try_ms && millis() - s_last_try_ms < MQTT_RETRY_MS) return;
    s_last_try_ms = millis() ? millis() : 1;
    if (!connect()) return;
  }
  s_mqtt.loop();
  if (s_state_dirty || millis() - s_last_state_ms >= STATE_REFRESH_MS) publishState();
}
