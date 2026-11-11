// Every setting Glowing Square has, declared once. The web Settings page
// renders from these two tables; the core loads every value from NVS at
// boot and writes it back when it changes.
#include "services/settings_registry.h"

#include "project/gs_state.h"
#include "project/gs_web_extra.h"
#include "project/matrix/matrix.h"
#include "project/modes/modes.h"
#include "lang.h"

enum Section : uint8_t { SEC_DISPLAY = 0, SEC_TUBE, SEC_FLIGHTS, SEC_UNIFI, SEC_MQTT, SEC_PINS };

static bool modeTube()    { return g_mode == MODE_TUBE; }
static bool modeFlights() { return g_mode == MODE_FLIGHTS; }
static bool modeUnifi()   { return g_mode == MODE_UNIFI; }
static bool mqttOn()      { return g_mqtt_on; }
static bool unifiKey()    { return g_unifi_auth == UNIFI_AUTH_KEY; }
static bool unifiLogin()  { return g_unifi_auth != UNIFI_AUTH_KEY; }
#ifdef MATRIX_DMA
static bool always()      { return true; }
#endif
#ifdef MATRIX_PXMATRIX
#define SEC_PANEL SEC_PINS   // the PxMatrix build has no pins section: same index
#endif

static void modeChanged() { g_mode_changed = true; }
// A changed stop, line or address shows on the next pass, not a minute later.
static void sourceChanged() { fetchNow(); }
#ifdef MATRIX_PXMATRIX
static void timingChanged() { matrixApplyTiming(); }
#endif

const SettingSection SETTING_SECTIONS[] = {
  { "display", STR_P_SEC_DISPLAY, STR_P_SEC_DISPLAY_INFO, nullptr,     nullptr },
  { "tube",    STR_P_SEC_TUBE,    STR_P_SEC_TUBE_INFO,    modeTube,    gsWebExtraTube },
  { "flights", STR_P_SEC_FLIGHTS, STR_P_SEC_FLIGHTS_INFO, modeFlights, gsWebExtraFlights },
  { "unifi",   STR_P_SEC_UNIFI,   STR_P_SEC_UNIFI_INFO,   modeUnifi,   gsWebExtraUnifi },
  { "mqtt",    STR_P_SEC_MQTT,    STR_P_SEC_MQTT_INFO,    nullptr,     nullptr },
#ifdef MATRIX_DMA
  { "pins",    STR_P_SEC_PINS,    STR_P_SEC_PINS_INFO,    always,      nullptr },
#endif
#ifdef MATRIX_PXMATRIX
  { "panel",   STR_P_SEC_PANEL,   STR_P_SEC_PANEL_INFO,   nullptr,     nullptr },
#endif
};
const size_t SETTING_SECTIONS_COUNT = sizeof(SETTING_SECTIONS) / sizeof(SETTING_SECTIONS[0]);

static const uint16_t OPT_MODE[]  = { STR_P_MODE_TUBE, STR_P_MODE_FLIGHTS, STR_P_MODE_UNIFI };
static const uint16_t OPT_DIR[]   = { STR_P_TFL_DIR_BOTH, STR_P_TFL_DIR_IN, STR_P_TFL_DIR_OUT };
static const uint16_t OPT_UAUTH[] = { STR_P_UNIFI_AUTH_KEY, STR_P_UNIFI_AUTH_OS, STR_P_UNIFI_AUTH_OLD };

// The DMA library's own defaults, which Brian Lough's I2S Matrix Shield and
// the ESP32 Trinity follow. An S3 has no GPIO 23-25 broken out on most
// boards, so it gets a block of its own that is free on a DevKitC-1.
#ifdef MATRIX_DMA
#if CONFIG_IDF_TARGET_ESP32S3
#define PIN_DEFAULTS { 4, 5, 6, 7, 15, 16, 18, 8, 3, 42, -1, 40, 2, 41 }
#else
#define PIN_DEFAULTS { 25, 26, 27, 14, 12, 13, 23, 19, 5, 17, -1, 4, 15, 16 }
#endif
static const int32_t PIN_DEF[14] = PIN_DEFAULTS;
#define PIN_ROW(i, key, str) \
  settingInt(key, SEC_PINS, &g_pin[i], str, PIN_DEF[i], -1, 48).flag(SF_REBOOT)
#endif

const SettingDesc SETTINGS[] = {
  // ---- Display ----
  SETTING_ENUM("mode", SEC_DISPLAY, &g_mode, STR_P_MODE, MODE_TUBE, OPT_MODE).onChange(modeChanged),
  settingInt("boot_bright", SEC_DISPLAY, &g_boot_brightness, STR_P_BOOT_BRIGHT, 255, 0, 255)
      .info(STR_P_BOOT_BRIGHT_INFO),
  settingInt("scroll_ms", SEC_DISPLAY, &g_scroll_ms, STR_P_SCROLL, 40, 10, 200)
      .unitLabel("ms").info(STR_P_SCROLL_INFO),

  // ---- Tube ----
  SETTING_TEXT("tfl_stop", SEC_TUBE, g_tfl_stop, STR_P_TFL_STOP, "940GZZLUOXC").info(STR_P_TFL_STOP_INFO).onChange(sourceChanged),
  SETTING_TEXT("tfl_line", SEC_TUBE, g_tfl_line, STR_P_TFL_LINE, "victoria").info(STR_P_TFL_LINE_INFO).onChange(sourceChanged),
  SETTING_ENUM("tfl_dir", SEC_TUBE, &g_tfl_direction, STR_P_TFL_DIR, 1, OPT_DIR).onChange(sourceChanged),
  SETTING_TEXT("tfl_platf", SEC_TUBE, g_tfl_plat_filter, STR_P_TFL_PLAT_FILTER, "")
      .info(STR_P_TFL_PLAT_FILTER_INFO).onChange(sourceChanged),
  settingBool("tfl_plat", SEC_TUBE, &g_tfl_platform, STR_P_TFL_PLATFORM, true),
  SETTING_SECRET("tfl_key", SEC_TUBE, g_tfl_app_key, STR_P_TFL_KEY).info(STR_P_TFL_KEY_INFO).onChange(sourceChanged),

  // ---- Flights ----
  SETTING_TEXT("fl_lat", SEC_FLIGHTS, g_fl_lat, STR_P_FL_LAT, "51.5074").info(STR_P_FL_LATLON_INFO).onChange(sourceChanged),
  SETTING_TEXT("fl_lon", SEC_FLIGHTS, g_fl_lon, STR_P_FL_LON, "-0.1278").onChange(sourceChanged),
  settingInt("fl_radius", SEC_FLIGHTS, &g_fl_radius_nm, STR_P_FL_RADIUS, 10, 1, 100).unitLabel("nm").onChange(sourceChanged),
  settingInt("fl_min_alt", SEC_FLIGHTS, &g_fl_min_alt_ft, STR_P_FL_MIN_ALT, 500, 0, 40000)
      .unitLabel("ft").info(STR_P_FL_MIN_ALT_INFO).onChange(sourceChanged),

  // ---- Unifi ----
  SETTING_TEXT("unifi_url", SEC_UNIFI, g_unifi_url, STR_P_UNIFI_URL, "https://192.168.1.1").info(STR_P_UNIFI_URL_INFO).onChange(sourceChanged),
  SETTING_TEXT("unifi_site", SEC_UNIFI, g_unifi_site, STR_P_UNIFI_SITE, "default").onChange(sourceChanged),
  SETTING_ENUM("unifi_auth", SEC_UNIFI, &g_unifi_auth, STR_P_UNIFI_AUTH, UNIFI_AUTH_KEY, OPT_UAUTH).onChange(sourceChanged),
  SETTING_SECRET("unifi_key", SEC_UNIFI, g_unifi_key, STR_P_UNIFI_KEY).info(STR_P_UNIFI_KEY_INFO).when(unifiKey).onChange(sourceChanged),
  SETTING_TEXT("unifi_user", SEC_UNIFI, g_unifi_user, STR_P_UNIFI_USER, "").info(STR_P_UNIFI_USER_INFO).when(unifiLogin).onChange(sourceChanged),
  SETTING_SECRET("unifi_pass", SEC_UNIFI, g_unifi_pass, STR_P_UNIFI_PASS).when(unifiLogin).onChange(sourceChanged),
  settingBool("unifi_tls", SEC_UNIFI, &g_unifi_verify_tls, STR_P_UNIFI_TLS, false).info(STR_P_UNIFI_TLS_INFO),

  // ---- MQTT ----
  settingBool("mqtt_on", SEC_MQTT, &g_mqtt_on, STR_P_MQTT_ON, false),
  SETTING_TEXT("mqtt_host", SEC_MQTT, g_mqtt_host, STR_P_MQTT_HOST, "").when(mqttOn),
  settingInt("mqtt_port", SEC_MQTT, &g_mqtt_port, STR_P_MQTT_PORT, 1883, 1, 65535).when(mqttOn),
  SETTING_TEXT("mqtt_user", SEC_MQTT, g_mqtt_user, STR_P_MQTT_USER, "").when(mqttOn),
  SETTING_SECRET("mqtt_pass", SEC_MQTT, g_mqtt_pass, STR_P_MQTT_PASS).when(mqttOn),
  SETTING_TEXT("mqtt_room", SEC_MQTT, g_mqtt_room, STR_P_MQTT_ROOM, "living_room").info(STR_P_MQTT_ROOM_INFO).when(mqttOn),
  SETTING_TEXT("mqtt_name", SEC_MQTT, g_mqtt_name, STR_P_MQTT_NAME, "").info(STR_P_MQTT_NAME_INFO).when(mqttOn),
  settingBool("mqtt_ha", SEC_MQTT, &g_mqtt_ha, STR_P_MQTT_HA, true).info(STR_P_MQTT_HA_INFO).when(mqttOn),

#ifdef MATRIX_PXMATRIX
  // ---- Panel ----
  settingInt("px_period", SEC_PANEL, &g_px_period_us, STR_P_PX_PERIOD, 4000, 1000, 8000)
      .unitLabel("µs").info(STR_P_PX_PERIOD_INFO).onChange(timingChanged),
  settingInt("px_on", SEC_PANEL, &g_px_on_us, STR_P_PX_ON, 70, 10, 200)
      .unitLabel("µs").info(STR_P_PX_ON_INFO),
#endif

#ifdef MATRIX_DMA
  // ---- Pins ----
  PIN_ROW(0, "pin_r1", STR_P_PIN_R1),  PIN_ROW(1, "pin_g1", STR_P_PIN_G1),
  PIN_ROW(2, "pin_b1", STR_P_PIN_B1),  PIN_ROW(3, "pin_r2", STR_P_PIN_R2),
  PIN_ROW(4, "pin_g2", STR_P_PIN_G2),  PIN_ROW(5, "pin_b2", STR_P_PIN_B2),
  PIN_ROW(6, "pin_a", STR_P_PIN_A),    PIN_ROW(7, "pin_b", STR_P_PIN_B),
  PIN_ROW(8, "pin_c", STR_P_PIN_C),    PIN_ROW(9, "pin_d", STR_P_PIN_D),
  PIN_ROW(10, "pin_e", STR_P_PIN_E),   PIN_ROW(11, "pin_lat", STR_P_PIN_LAT),
  PIN_ROW(12, "pin_oe", STR_P_PIN_OE), PIN_ROW(13, "pin_clk", STR_P_PIN_CLK),
#endif
};
const size_t SETTINGS_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);
