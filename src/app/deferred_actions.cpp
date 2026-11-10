#include "app/deferred_actions.h"

#include <Arduino.h>

bool restart_pending       = false;
unsigned long restart_at_ms = 0;
bool factory_reset_pending = false;
bool portal_open_pending   = false;

bool gh_check_pending      = false;
bool gh_downgrade_pending  = false;
bool gh_flash_pending      = false;
unsigned long gh_check_wait_since = 0;

bool skip_setup_pending    = false;
bool finish_setup_pending  = false;
bool setup_done_pending    = false;
bool show_reboot_pending   = false;
bool show_timezone_pending = false;
bool show_language_pending = false;
bool show_welcome_pending  = false;
bool show_system_pending   = false;
bool show_ota_pending      = false;
bool show_info_pending     = false;
bool show_wifi_menu_pending = false;
bool show_display_pending  = false;
bool show_settings_pending = false;
uint8_t open_project_screen_pending = 0;

void requestRestart(unsigned long delay_ms) {
  // millis() + 0 would be skipped if the counter is exactly 0; 1 ms later is
  // indistinguishable.
  restart_at_ms = millis() + (delay_ms ? delay_ms : 1);
  restart_pending = true;
}
