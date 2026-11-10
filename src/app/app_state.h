#pragma once

// ============================================================
//  Core state shared across the template.
//
//  Only what the core itself needs lives here. A project keeps its own
//  state in project/, never in this file: that is what kept SpoolmanScale's
//  version of this header growing to 250 lines of unrelated globals.
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#include "app_config.h"

#ifdef APP_UI_LVGL
#include <lvgl.h>
#endif

// ---- WiFi and setup -------------------------------------------------------

extern char cfg_wifi_ssid[33];
extern char cfg_wifi_password[65];
extern bool cfg_lang_set;
extern bool cfg_first_boot;
// Set when the browser supplied WiFi while the setup was running. The
// language step restarts the device, and without this the restart would
// find an SSID and boot past the rest of the setup.
extern bool cfg_setup_resume;
// Boots in a row that never got an IP address (see noteBootFailed()).
extern uint8_t cfg_failed_boots;

// True while the first-time setup chain is running. Screens that appear in
// both the setup and the settings menu use it to decide where "back" goes.
extern bool setup_active;

// The station has an IP address.
extern bool wifi_ok;

// ---- Display power (boards with a screen) --------------------------------

extern int bright_normal;
extern int dim_timeout_ms;
extern int off_timeout_ms;
extern int sleep_timeout_ms;

#ifdef APP_UI_LVGL
// The screens the core builds. A project registers its own with the overlay
// registry in ui/navigation.h instead of adding them here.
extern lv_obj_t *scr_main;
extern lv_obj_t *scr_settings;
extern lv_obj_t *scr_wifi;
extern lv_obj_t *scr_timezone;
extern lv_obj_t *scr_welcome;
extern lv_obj_t *scr_first_boot;
extern lv_obj_t *scr_setup_done;
extern lv_obj_t *scr_wifi_setup;
extern lv_obj_t *scr_wifi_menu;
extern lv_obj_t *scr_wifi_pass;
extern lv_obj_t *scr_wifi_connecting;
extern lv_obj_t *scr_wifi_portal;
extern lv_obj_t *scr_display;
extern lv_obj_t *scr_system;
extern lv_obj_t *scr_ota;
extern lv_obj_t *scr_ota_browser;
extern lv_obj_t *scr_ota_github;

extern lv_obj_t *lbl_ota_status;
extern lv_obj_t *lbl_burger_badge;
extern lv_obj_t *lbl_system_badge;
extern lv_obj_t *lbl_fw_badge;
extern lv_obj_t *lbl_gh_btn_badge;
extern lv_obj_t *lbl_wifi_info;
#endif
