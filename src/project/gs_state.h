#pragma once

#include <stdint.h>

// ============================================================
//  Glowing Square's own state. The settings registry writes these
//  (project_settings.cpp); everything else reads them.
// ============================================================

enum DisplayModeId : uint8_t { MODE_TUBE = 0, MODE_FLIGHTS = 1, MODE_UNIFI = 2, MODE_COUNT };
enum PartyMode : uint8_t { PARTY_OFF = 0, PARTY_CHILL = 1, PARTY_ON = 2 };

// ---- Display ---------------------------------------------------------------
extern uint8_t g_mode;             // DisplayModeId
extern int32_t g_boot_brightness;  // 0..255, the level a power-on starts at
extern int32_t g_scroll_ms;        // ms per pixel of scrolling text

// ---- Tube ------------------------------------------------------------------
extern char    g_tfl_stop[24];     // NaPTAN id, e.g. 940GZZLUACY
extern char    g_tfl_line[24];     // line or bus route; empty = every line at the stop
extern uint8_t g_tfl_direction;    // 0 both, 1 inbound, 2 outbound
extern char    g_tfl_app_key[48];  // optional
extern bool    g_tfl_platform;     // append the platform to the destination
extern char    g_tfl_plat_filter[24]; // only platforms containing this; empty = all

// ---- Flights ---------------------------------------------------------------
extern char    g_fl_lat[16];
extern char    g_fl_lon[16];
extern int32_t g_fl_radius_nm;
extern int32_t g_fl_min_alt_ft;    // ignore aircraft below this (taxiing, helicopters)

// ---- Unifi -----------------------------------------------------------------
enum UnifiAuth : uint8_t { UNIFI_AUTH_KEY = 0, UNIFI_AUTH_OS_LOGIN = 1, UNIFI_AUTH_CLASSIC = 2 };
extern char    g_unifi_url[64];    // https://192.168.1.1
extern char    g_unifi_site[24];   // "default"
extern uint8_t g_unifi_auth;       // UnifiAuth
extern char    g_unifi_key[80];
extern char    g_unifi_user[40];
extern char    g_unifi_pass[64];
extern bool    g_unifi_verify_tls;

// ---- MQTT ------------------------------------------------------------------
extern bool    g_mqtt_on;
extern char    g_mqtt_host[64];
extern int32_t g_mqtt_port;
extern char    g_mqtt_user[40];
extern char    g_mqtt_pass[64];
extern char    g_mqtt_room[30];    // v1: "living_room"
extern char    g_mqtt_name[40];    // empty = "glowingsquare/<mode>", as v1
extern bool    g_mqtt_ha;          // Home Assistant discovery

// ---- Panel timing (PxMatrix builds) ----------------------------------------
#ifdef MATRIX_PXMATRIX
extern int32_t g_px_period_us;     // one refresh every this long (v1: 4000)
extern int32_t g_px_on_us;         // a row's on-time per refresh (v1: 70)
extern int32_t g_px_mux_us;        // settle time after each row address line (default 3; v1: 0, B 2)
#endif

// ---- HUB75 pins (DMA builds) -----------------------------------------------
#ifdef MATRIX_DMA
extern int32_t g_pin[14];          // R1 G1 B1 R2 G2 B2 A B C D E LAT OE CLK
#endif

// ---- Live state (not settings) ---------------------------------------------
extern uint8_t  g_party;           // PartyMode, from MQTT or the web
extern uint8_t  g_brightness_target;
extern bool     g_mode_changed;    // set by the registry, consumed by the loop

const char* modeKey(uint8_t mode);  // "tube", "flights", "unifi"
