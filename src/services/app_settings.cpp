#include "app_settings.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <cstring>

#include "app_config.h"
#include "lang.h"
#include "device_name.h"
#include "ota_state.h"
#include "prefs_store.h"
#include "settings_registry.h"
#include "time_service.h"
#include "web/web_access.h"
#include "web/web_shell.h"

#ifdef APP_UI_LVGL
#include "hardware/display.h"
#endif

static void copyBounded(char *dst, size_t n, const char *src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = '\0';
}

void loadPrefs() {
  copyBounded(cfg_wifi_ssid, sizeof(cfg_wifi_ssid),
              prefsGetString("wifi_ssid", cfg_wifi_ssid).c_str());
  copyBounded(cfg_wifi_password, sizeof(cfg_wifi_password),
              prefsGetString("wifi_pass", cfg_wifi_password).c_str());

  webAccessLoad();
  webThemeLoad();
  deviceNameLoad();

  // Range checked: a stored byte outside the enum (a corrupt cell, or a
  // language a newer build wrote) would index a column that does not exist.
  { const uint8_t v = prefsGetUChar("lang", LANG_EN);
    g_lang = (v < LANG_COUNT) ? (Lang)v : LANG_EN; }
  g_date_fmt = prefsGetUChar("date_fmt", 0);
  // Before anything formats a time. The zone falls back to the language.
  timeZoneApply();

  cfg_lang_set     = prefsGetBool("lang_set", LANG_COUNT == 1);
  cfg_first_boot   = prefsGetBool("first_boot", true);
  cfg_setup_resume = prefsGetBool("setup_resume", false);
  cfg_failed_boots = prefsGetUChar("boot_fail", 0);

  gh_prerelease    = prefsGetBool("gh_prerelease", false);
  g_upd_autocheck  = prefsGetBool("upd_check", true);
  g_upd_last_epoch = prefsGetUInt("upd_last", 0);

#ifdef APP_UI_LVGL
  bright_normal = prefsGetUChar("bright", BRIGHT_NORMAL_DEFAULT);
  // Safe this early: displaySetUiGain() only builds its lookup tables and
  // skips the LVGL repaint until a display exists.
  displaySetUiGain((uint16_t)prefsGetUInt("ui_gain", 100));
  dim_timeout_ms   = (int)prefsGetUInt("dim_min", DIM_TIMEOUT_DEFAULT / 60000) * 60000;
  off_timeout_ms   = (int)prefsGetUInt("off_min", OFF_TIMEOUT_DEFAULT / 60000) * 60000;
  sleep_timeout_ms = (int)prefsGetUInt("sleep_min", SLEEP_TIMEOUT_DEFAULT / 60000) * 60000;
#endif

  // Every setting the project declares, key, type and default read off its
  // one table.
  settingsLoadAll();

  Serial.printf("Prefs: SSID=%s lang=%d first_boot=%d\n",
                cfg_wifi_ssid, (int)g_lang, (int)cfg_first_boot);
}

void saveWifiCredentials(const char* ssid, const char* pass) {
  copyBounded(cfg_wifi_ssid, sizeof(cfg_wifi_ssid), ssid);
  copyBounded(cfg_wifi_password, sizeof(cfg_wifi_password), pass);
  prefsPutString("wifi_ssid", cfg_wifi_ssid);
  prefsPutString("wifi_pass", cfg_wifi_password);
  Serial.printf("WiFi saved: SSID=%s\n", cfg_wifi_ssid);
}

void forgetWifiCredentials() {
  cfg_wifi_ssid[0] = '\0';
  cfg_wifi_password[0] = '\0';
  prefsRemove("wifi_ssid");
  prefsRemove("wifi_pass");
}

void noteBootFailed(bool failed) {
  const uint8_t next = failed ? (uint8_t)min(255, cfg_failed_boots + 1) : 0;
  if (next == cfg_failed_boots) return;
  cfg_failed_boots = next;
  prefsPutUChar("boot_fail", next);
}
