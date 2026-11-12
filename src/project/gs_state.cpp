// The state behind gs_state.h: settings the registry loads and writes,
// and the live values MQTT and the web change.
#include "project/gs_state.h"

#include "project/modes/modes.h"

uint8_t g_mode            = MODE_TUBE;
int32_t g_boot_brightness = 255;
int32_t g_scroll_ms       = 40;

char    g_tfl_stop[24]     = "";
char    g_tfl_line[24]     = "";
uint8_t g_tfl_direction    = 1;
char    g_tfl_app_key[48]  = "";
bool    g_tfl_platform     = true;
char    g_tfl_plat_filter[24] = "";
char    g_tfl_rename[96]   = "";

char    g_fl_lat[16]       = "";
char    g_fl_lon[16]       = "";
int32_t g_fl_radius_nm     = 10;
int32_t g_fl_min_alt_ft    = 500;

char    g_unifi_url[64]    = "";
char    g_unifi_site[24]   = "default";
uint8_t g_unifi_auth       = UNIFI_AUTH_KEY;
char    g_unifi_key[80]    = "";
char    g_unifi_user[40]   = "";
char    g_unifi_pass[64]   = "";
bool    g_unifi_verify_tls = false;

bool    g_mqtt_on          = false;
char    g_mqtt_host[64]    = "";
int32_t g_mqtt_port        = 1883;
char    g_mqtt_user[40]    = "";
char    g_mqtt_pass[64]    = "";
char    g_mqtt_room[30]    = "living_room";
char    g_mqtt_name[40]    = "";
bool    g_mqtt_ha          = true;

#ifdef MATRIX_PXMATRIX
int32_t g_px_period_us = 4000;
int32_t g_px_on_us     = 70;
int32_t g_px_mux_us    = 3;
#endif

#ifdef MATRIX_DMA
int32_t g_pin[14];
#endif

uint8_t g_party             = PARTY_OFF;
uint8_t g_brightness_target = 255;
bool    g_mode_changed      = false;

const char *modeKey(uint8_t mode) { return modeOps(mode).key; }

