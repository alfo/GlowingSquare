#include "app_loop.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "app_config.h"
#include "app/app_boot.h"
#include "app/app_hooks.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "hardware/flash_log.h"
#include "hardware/sd_logger.h"
#include "services/breadcrumb.h"
#include "services/device_name.h"
#include "services/github_release.h"
#include "services/improv_serial.h"
#include "services/mdns_service.h"
#include "services/ota_state.h"
#include "services/prefs_store.h"
#include "services/provisioning.h"
#include "services/setup_portal.h"
#include "services/update_check.h"
#include "services/wifi_manager.h"
#include "services/wifi_roam.h"
#include "web/web_access.h"
#include "web/web_jobs.h"
#include "web/web_server.h"

#ifdef APP_UI_LVGL
#include <lvgl.h>
#include "ui/ui_shell.h"
#endif

// While the link is down the retries go on every WIFI_RETRY_INTERVAL_MS, but
// the log gets the loss, then a line this often, then the reconnect. One line
// per retry filled the session ring in 40 minutes with nothing else.
constexpr unsigned long WIFI_OUTAGE_LOG_MS = 5UL * 60UL * 1000UL;
// What counts as nobody using the device, for the roaming check: no browser
// on the web interface for this long (and, with a screen, the screen idle).
constexpr unsigned long WIFI_ROAM_WEB_QUIET_MS = 2UL * 60UL * 1000UL;

// Below this many free bytes on the loop task's stack, the log says so.
constexpr uint32_t STACK_WARN_BYTES = 3072;

// ---------------------------------------------------------------------------
//  WiFi watchdog
// ---------------------------------------------------------------------------

static bool          wifi_link_up         = false;
static bool          wifi_outage          = false;
static unsigned long wifi_outage_start_ms = 0;
static unsigned long wifi_outage_log_ms   = 0;
static uint32_t      wifi_outage_attempts = 0;

// One line when the link comes up: which access point, on which channel, how
// strong. With several access points on one SSID that is the first thing to
// know about any WiFi trouble.
static void noteWifiUp() {
  if (wifi_link_up) return;
  wifi_link_up = true;
  char line[64];
  wifiManagerLinkLine(line, sizeof(line));
  if (wifi_outage) {
    logSDf("WiFi: reconnected to %s after %lus and %u attempts, %s", cfg_wifi_ssid,
           (millis() - wifi_outage_start_ms) / 1000, (unsigned)wifi_outage_attempts, line);
  } else {
    logSDf("WiFi: connected to %s, %s", cfg_wifi_ssid, line);
  }
  wifi_outage = false;
}

static void noteWifiDown() {
  if (wifi_outage) return;
  wifi_link_up = false;
  wifi_outage = true;
  wifi_outage_start_ms = millis();
  wifi_outage_log_ms = millis();
  wifi_outage_attempts = 0;
  const uint8_t reason = wifiManagerLastDisconnectReason();
  logSDf("WiFi: connection lost (reason %u, %s), reconnecting to %s",
         reason, wifiManagerReasonName(reason), cfg_wifi_ssid);
  wifiOnDisconnected();
}

// Someone else is setting up the network right now; a begin() with the
// stored one would cancel what they are doing.
static bool wifiSomeoneElseBusy() {
  if (improvSerialBusy()) return true;      // the browser over USB
  if (setupPortalActive()) return true;     // the AP is the network
  if (provisioningActive()) return true;    // new credentials being tried
  if (wifiRoamSwitching()) return true;     // joining a better access point
#ifdef APP_UI_LVGL
  if (uiWifiScreenVisible()) return true;   // the user is on the WiFi screens
#endif
  return false;
}

static void handleWifiReconnect() {
  if (cfg_wifi_ssid[0] == '\0') return;
  if (wifiSomeoneElseBusy()) return;

  if (WiFi.status() == WL_CONNECTED) {
    noteWifiUp();
    // Connected, but boot gave up before the network answered (or a portal
    // or Improv brought it up), so nothing a connection starts has run yet.
    if (!wifi_ok) {
      logSD("WiFi: connected, starting network services");
      wifiOnConnected();
    }
    return;
  }

  noteWifiDown();

  static unsigned long last_retry_ms = 0;
  if (last_retry_ms != 0 && millis() - last_retry_ms < WIFI_RETRY_INTERVAL_MS) return;
  last_retry_ms = millis();
  wifi_outage_attempts++;

  if (millis() - wifi_outage_log_ms >= WIFI_OUTAGE_LOG_MS) {
    wifi_outage_log_ms = millis();
    const uint8_t reason = wifiManagerLastDisconnectReason();
    logSDf("WiFi: still offline after %lu min, %u attempts, last reason %u (%s)",
           (millis() - wifi_outage_start_ms) / 60000, (unsigned)wifi_outage_attempts,
           reason, wifiManagerReasonName(reason));
  }
  // Through the manager rather than WiFi.begin(): the scan over every channel
  // that joins the strongest access point is set there.
  wifiManagerBegin(cfg_wifi_ssid, cfg_wifi_password);
}

// Nobody is using the device, so a scan for a better access point cannot get
// in anyone's way. See services/wifi_roam.h.
static bool wifiRoamQuiet() {
#ifdef APP_UI_LVGL
  if (!uiIdle()) return false;
#endif
  if (webBrowserSeenWithin(WIFI_ROAM_WEB_QUIET_MS)) return false;
  if (wifiSomeoneElseBusy()) return false;
  if (gh_flash_active || otaWebUploadActive() || updateCheckBusy()) return false;
  if (webJobState() == WJS_RUNNING) return false;
  return true;
}

// ---------------------------------------------------------------------------
//  Health: stack watermark and heartbeat
// ---------------------------------------------------------------------------

static uint32_t s_stack_min_bytes = 0xFFFFFFFF;

// The deepest moment happens inside a callback, and by the time any logging
// runs the stack is shallow again; the watermark still remembers it. Logged
// outside the verbose guard, because running out of stack is a panic reboot,
// and once per new low so it cannot flood the file.
static void stackWatermarkTick() {
  static unsigned long last_ms = 0;
  if (millis() - last_ms < 2000) return;
  last_ms = millis();
  const uint32_t free_stack = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
  if (free_stack >= s_stack_min_bytes) return;
  const bool was_ok = (s_stack_min_bytes >= STACK_WARN_BYTES);
  s_stack_min_bytes = free_stack;
  if (free_stack < STACK_WARN_BYTES) {
    logSDf("%s loop task stack down to %u bytes free",
           was_ok ? "STACK WARNING:" : "STACK:", (unsigned)free_stack);
  }
}

// Verbose only. Checked every 5 s, but written only when something moved by
// HEARTBEAT_QUIET_DELTA_B, plus one line per HEARTBEAT_KEEPALIVE_MS so the
// last timestamp still marks how far the loop got before it stopped.
static void heartbeatTick() {
  static unsigned long last_check_ms = 0, last_logged_ms = 0;
  static uint32_t count = 0, prev_heap = 0, prev_stack = 0;
  static bool have_prev = false, prev_wifi = false;
  if (!sd_verbose || millis() - last_check_ms < 5000) return;
  last_check_ms = millis();
  count++;

  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
  const bool     wifi_up = (WiFi.status() == WL_CONNECTED);
  const uint32_t heap    = (uint32_t)ESP.getFreeHeap();
  auto moved = [](uint32_t a, uint32_t b) {
    return (a > b ? a - b : b - a) >= HEARTBEAT_QUIET_DELTA_B;
  };
  const bool changed = !have_prev || moved(heap, prev_heap) ||
                       s_stack_min_bytes < prev_stack || wifi_up != prev_wifi;
  const bool due = (millis() - last_logged_ms) >= HEARTBEAT_KEEPALIVE_MS;
  if (!changed && !due) return;

  last_logged_ms = millis();
  have_prev = true;
  prev_heap = heap;
  prev_stack = s_stack_min_bytes;
  prev_wifi = wifi_up;
  logSDf("[verbose] heartbeat #%u heap=%u PSRAM=%u uptime=%lus heap_big=%u "
         "heap_holes=%u stack_min=%u wifi=%s rssi=%d",
         (unsigned)count, (unsigned)heap, (unsigned)ESP.getFreePsram(), millis() / 1000,
         (unsigned)info.largest_free_block, (unsigned)info.free_blocks,
         (unsigned)s_stack_min_bytes, wifi_up ? "up" : "DOWN", wifi_up ? WiFi.RSSI() : 0);
}

// ---------------------------------------------------------------------------
//  Firmware update flags
// ---------------------------------------------------------------------------

static void otaTick() {
  firmwareStampTick();
  otaWebGithubTick();
#ifdef APP_UI_LVGL
  uiOtaTick();
#endif
}

// ---------------------------------------------------------------------------
//  Restart and factory reset
// ---------------------------------------------------------------------------

static void restartTick() {
  if (!restart_pending) return;
  if (restart_at_ms && (long)(millis() - restart_at_ms) < 0) return;
  if (factory_reset_pending) {
    logSD("Factory reset: clearing NVS");
    prefsDiscardWrites();
    prefsClearAll();
  }
  logSD("Reboot");
  sdLoggerTick();       // the line above has to reach the log first
  delay(100);
  ESP.restart();
}

// ---------------------------------------------------------------------------
//  The loop
// ---------------------------------------------------------------------------

void appLoop() {
  // Overwritten every pass, so a crumb from a marked section only stands while
  // that section runs.
  crumbSet("loop");

  // Settings changed during this pass (an LVGL callback, a web route) are
  // parked and written once the pass is done, so no flash write runs inside
  // an event callback.
  prefsDeferWrites(true);
#ifdef APP_UI_LVGL
  crumbSet("ui");
  uiLoop();
#endif

  stackWatermarkTick();
  heartbeatTick();

  // Before the watchdog, so a connect attempt from the browser is already
  // known to be running when the watchdog asks.
  crumbSet("improv");
  improvSerialTick();
  crumbSet("portal");
  setupPortalTick();
  provisioningTick();
  crumbSet("wifi");
  handleWifiReconnect();
  wifiRoamTick(wifiRoamQuiet(), cfg_wifi_ssid, cfg_wifi_password);

  crumbSet("web");
  handleOtaServerClient();
  webJobsTick();

  crumbSet("log");
  sdLoggerTick();
  flashLogTick();

  crumbSet("update");
  updateCheckTick();
  otaTick();

  // The one owner of port 80. Derived from the conditions once a second, so
  // a flipped switch or a returning WiFi takes effect without extra wiring.
  {
    static unsigned long last_web_sync_ms = 0;
    if (millis() - last_web_sync_ms >= 1000) {
      last_web_sync_ms = millis();
      webServerSyncState();
      // Right after: the advertised _http._tcp service follows whether the
      // socket is actually listening.
      mdnsSyncState();
      // Last: the address the interface prints depends on whether the
      // responder came up, and the DNS check on the address.
      deviceNameTick();
    }
  }

  crumbSet("project");
  if (APP_HOOKS.loop) APP_HOOKS.loop();

  prefsDeferWrites(false);
  prefsFlush();

  restartTick();
}
