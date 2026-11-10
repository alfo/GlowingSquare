#pragma once

#include <stdint.h>

// ============================================================
//  Deferred actions
//
//  An LVGL event handler or a web route never does the work itself: no
//  HTTP, no blocking, no deleting its own object. It sets one of these
//  flags and the loop does the work on its next pass (app_loop.cpp).
//  A project adds its own flags in project/, the same way.
// ============================================================

// ---- Restart / reset (any build) -------------------------------------------
extern bool restart_pending;         // restart after the current pass
extern unsigned long restart_at_ms;  // 0 = now, else not before millis() >= this
extern bool factory_reset_pending;   // wipe NVS, then restart
extern bool portal_open_pending;     // open the AP portal (button, web, MQTT)

// ---- Firmware update -------------------------------------------------------
extern bool gh_check_pending;
extern bool gh_downgrade_pending;
extern bool gh_flash_pending;
extern unsigned long gh_check_wait_since;
#define GH_CHECK_WAIT_MS 10000

// ---- Screens (boards with a display) ---------------------------------------
extern bool skip_setup_pending;
extern bool finish_setup_pending;
extern bool setup_done_pending;
extern bool show_reboot_pending;
extern bool show_timezone_pending;
extern bool show_language_pending;
extern bool show_welcome_pending;
extern bool show_system_pending;
extern bool show_ota_pending;
extern bool show_info_pending;
extern bool show_wifi_menu_pending;
extern bool show_display_pending;
extern bool show_settings_pending;
// A SUBMENU row in the registry asked for its screen. The project's
// appHooks.openScreen() is called with this id; 0 = none.
extern uint8_t open_project_screen_pending;

// Arms a restart `delay_ms` from now, so a web answer can still be sent.
void requestRestart(unsigned long delay_ms);
