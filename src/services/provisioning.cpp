#include "services/provisioning.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app/app_hooks.h"
#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "app_config.h"
#include "hardware/sd_logger.h"
#include "services/app_settings.h"
#include "services/improv_serial.h"
#include "services/setup_portal.h"
#include "services/wifi_manager.h"
#include "web/web_server.h"

#ifdef APP_PORTAL_SECURE
#define PORTAL_WITH_PASSWORD true
#else
#define PORTAL_WITH_PASSWORD false
#endif

// How long new credentials from the portal get to produce an address before
// the portal comes back with an error.
#define PROV_CONNECT_TIMEOUT_MS  20000UL

enum ProvState : uint8_t { PS_IDLE, PS_PORTAL, PS_CONNECTING };

static ProvState     s_state       = PS_IDLE;
static unsigned long s_since_ms    = 0;
static bool          s_had_wifi    = false;   // an SSID was stored when the portal opened

#if !defined(APP_UI_LVGL)
static void notifyPortal(bool open) {
  if (APP_HOOKS.onPortal) APP_HOOKS.onPortal(open);
}

static void openPortal(const char *why) {
  if (setupPortalActive()) return;
  s_had_wifi = cfg_wifi_ssid[0] != '\0';
  if (!setupPortalStart(PORTAL_WITH_PASSWORD)) {
    logSDf("Portal: could not open (%s)", why);
    return;
  }
  s_state = PS_PORTAL;
  s_since_ms = millis();
  webServerSyncState();
  logSDf("Portal: open (%s)", why);
  Serial.printf("\n*** Setup: join WiFi \"%s\"%s%s%s, then open http://%s/ ***\n\n",
                setupPortalSsid(),
                setupPortalPassword()[0] ? " with password \"" : "",
                setupPortalPassword(),
                setupPortalPassword()[0] ? "\"" : "",
                setupPortalIP().toString().c_str());
  notifyPortal(true);
}

static void closePortal() {
  if (!setupPortalActive()) return;
  setupPortalStop();
  webServerSyncState();
  notifyPortal(false);
}
#endif

void provisioningOpen(const char *why) {
#if defined(APP_UI_LVGL)
  // The screen owns the portal on these boards.
  (void)why;
  show_wifi_menu_pending = true;
#else
  openPortal(why);
#endif
}

bool provisioningActive() { return s_state != PS_IDLE; }

bool provisioningBoot() {
#if defined(APP_UI_LVGL)
  return false;
#else
  if (cfg_wifi_ssid[0] == '\0') {
    openPortal("no WiFi stored");
    return true;
  }
  if (cfg_failed_boots >= PORTAL_AFTER_FAILED_BOOTS) {
    char why[48];
    snprintf(why, sizeof(why), "%u boots without a connection", (unsigned)cfg_failed_boots);
    openPortal(why);
    return true;
  }
  return false;
#endif
}

// The BOOT button, held. Read every pass; a press shorter than the hold is
// ignored, so a project may use the same button for something of its own.
static void buttonTick() {
#if APP_PORTAL_BUTTON_PIN >= 0
  static bool          configured = false;
  static unsigned long down_since = 0;
  static bool          fired      = false;
  if (!configured) {
    pinMode(APP_PORTAL_BUTTON_PIN, INPUT_PULLUP);
    configured = true;
  }
  const bool down = digitalRead(APP_PORTAL_BUTTON_PIN) == LOW;
  if (!down) { down_since = 0; fired = false; return; }
  if (!down_since) down_since = millis() ? millis() : 1;
  if (!fired && millis() - down_since >= APP_PORTAL_HOLD_MS) {
    fired = true;
    portal_open_pending = true;
  }
#endif
}

void provisioningTick() {
  buttonTick();
  if (portal_open_pending) {
    portal_open_pending = false;
    provisioningOpen("asked for");
  }

#if !defined(APP_UI_LVGL)
  switch (s_state) {
    case PS_IDLE:
      return;

    case PS_PORTAL: {
      char ssid[33], pass[65];
      if (setupPortalTakeCredentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        closePortal();
        saveWifiCredentials(ssid, pass);
        wifiManagerBegin(cfg_wifi_ssid, cfg_wifi_password);
        s_state = PS_CONNECTING;
        s_since_ms = millis();
        logSDf("Portal: trying %s", cfg_wifi_ssid);
        return;
      }
      // Improv got there first: the browser that flashed the device handed it
      // a network while the portal stood open.
      if (improvSerialBusy() || wifi_ok) {
        closePortal();
        s_state = PS_IDLE;
        return;
      }
      // Opened with a network stored (a router outage at boot, say): close
      // after a while and let the watchdog try the stored one again. A device
      // with nothing stored waits for as long as it takes.
      if (s_had_wifi && millis() - s_since_ms >= PORTAL_IDLE_TIMEOUT_MS) {
        logSD("Portal: nobody came, back to the stored network");
        closePortal();
        s_state = PS_IDLE;
        wifiManagerBegin(cfg_wifi_ssid, cfg_wifi_password);
      }
      return;
    }

    case PS_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        s_state = PS_IDLE;
        logSDf("Portal: connected to %s", cfg_wifi_ssid);
        return;   // the watchdog in appLoop() starts the network services
      }
      if (millis() - s_since_ms >= PROV_CONNECT_TIMEOUT_MS) {
        logSDf("Portal: %s did not connect (reason %u), opening again", cfg_wifi_ssid,
               (unsigned)wifiManagerLastDisconnectReason());
        s_state = PS_IDLE;
        openPortal("the new network did not connect");
      }
      return;
  }
#endif
}
