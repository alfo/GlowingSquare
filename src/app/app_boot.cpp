#include "app_boot.h"

#include <Arduino.h>
#include <cstring>

#include "app_config.h"
#include "app/app_hooks.h"
#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/app_settings.h"
#include "services/breadcrumb.h"
#include "services/device_name.h"
#include "services/improv_serial.h"
#include "services/mdns_service.h"
#include "services/prefs_store.h"
#include "services/provisioning.h"
#include "services/time_service.h"
#include "services/update_check.h"
#include "services/wifi_manager.h"
#include "web/web_server.h"

#ifdef APP_UI_LVGL
#include "ui/ui_shell.h"
#endif

#include "lang.h"

void wifiOnConnected() {
  const bool first = !wifi_ok;
  wifi_ok = true;
  Serial.printf("WiFi OK! IP: %s\n", wifiManagerLocalIP().toString().c_str());
  noteBootFailed(false);
  if (cfg_first_boot) {
    // The first connection ends the first-time setup on a headless device.
    // A board with a screen ends it on its last setup screen instead.
#ifndef APP_UI_LVGL
    cfg_first_boot = false;
    prefsPutBool("first_boot", false);
#endif
  }
  webServerSyncState();
  mdnsSyncState();
  deviceNameTick();
  syncNTP();
  if (sd_available) cleanOldLogs();   // requires synced time
  if (first) writeBootBlock("Boot");
  logSDf("WiFi connected: %s | RSSI: %d dBm", cfg_wifi_ssid, wifiManagerRSSI());
#ifdef APP_UI_LVGL
  uiOnWifiUp();
#endif
  if (APP_HOOKS.onWifiUp) APP_HOOKS.onWifiUp();
}

void wifiOnDisconnected() {
  if (!wifi_ok) return;
  wifi_ok = false;
#ifdef APP_UI_LVGL
  uiOnWifiDown();
#endif
  if (APP_HOOKS.onWifiDown) APP_HOOKS.onWifiDown();
}

void wifiConnect() {
  Serial.printf("WiFi: %s\n", cfg_wifi_ssid);
  // A short wait at boot rather than the 10 s SpoolmanScale allowed: a
  // headless device has nothing better to show meanwhile, but the loop has
  // MQTT, the display and the button to look after, and the watchdog in
  // appLoop() keeps trying in the background anyway.
  if (wifiManagerConnect(cfg_wifi_ssid, cfg_wifi_password, 16, 500)) {
    wifiOnConnected();
    return;
  }
  Serial.println("WiFi: not connected yet, retrying in the background");
  logSD("WiFi connection at boot failed, retrying in the background");
}

void appSetup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // USB CDC blocks on a full buffer while no host reads it; a UART does not.
  Serial.setTxTimeoutMs(0);
#endif
  delay(200);
  crumbBegin();
  Serial.println("=== " APP_NAME " " FW_VERSION " (" APP_ENV ") ===");

  loadPrefs();
  initSD();

  // A boot that never gets an IP address is counted by appLoop() once it has
  // waited WIFI_FAILED_BOOT_MS, not here: a brownout or a crash in the first
  // seconds is a power or firmware problem, and counting those sent working
  // devices back to the setup portal. wifiOnConnected() clears the count.

#ifdef APP_UI_LVGL
  uiBegin();
#endif

  if (APP_HOOKS.setup) APP_HOOKS.setup();

#ifdef APP_UI_LVGL
  // The screen decides: language, welcome, WiFi setup, or the main screen
  // and a connect.
  if (uiBootFlow()) wifiConnect();
#else
  if (!provisioningBoot()) wifiConnect();
#endif

  updateCheckRestoreBadge();
  updateCheckScheduleFirstRun();
}
