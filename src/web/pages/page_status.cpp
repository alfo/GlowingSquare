// The landing page. It used to be the firmware upload, which meant browsing
// the device address dropped the visitor straight into a file picker with no
// way to reach anything else.
#include "web/web_pages.h"

#include <Arduino.h>

#include "app/app_state.h"
#include "app_config.h"
#include "app/deferred_actions.h"
#include "hardware/sd_logger.h"
#include "services/device_name.h"
#include "services/mdns_service.h"
#include "services/partition_layout.h"
#include "hardware/flash_log.h"
#include "services/wifi_manager.h"
#include "app/app_hooks.h"
#include "web/web_access.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

static const char* label() { return T(STR_W_NAV_STATUS); }


static String uptimeStr() {
  uint32_t s = millis() / 1000UL;
  uint32_t d = s / 86400UL; s %= 86400UL;
  uint32_t h = s / 3600UL;  s %= 3600UL;
  uint32_t m = s / 60UL;
  char b[32];
  if (d)      snprintf(b, sizeof(b), "%lud %luh %lum", (unsigned long)d, (unsigned long)h, (unsigned long)m);
  else if (h) snprintf(b, sizeof(b), "%luh %lum", (unsigned long)h, (unsigned long)m);
  else        snprintf(b, sizeof(b), "%lum", (unsigned long)m);
  return String(b);
}

static const char* rssiWord(int r) {
  if (r >= SIGNAL_BARS_4_DBM) return "excellent";
  if (r >= SIGNAL_BARS_3_DBM) return "good";
  if (r >= SIGNAL_BARS_2_DBM) return "fair";
  return "weak";
}

static String statusJson() {
  String j = "{";
  j += "\"version\":\"" + jsonEsc(FW_VERSION) + "\"";
  j += ",\"uptime\":\"" + uptimeStr() + "\"";
  j += ",\"wifi\":" + String(wifi_ok ? "true" : "false");
  j += ",\"ssid\":\"" + jsonEsc(cfg_wifi_ssid) + "\"";
  if (wifi_ok) {
    const int r = wifiManagerRSSI();
    j += ",\"rssi\":" + String(r);
    j += ",\"rssiWord\":\"" + String(rssiWord(r)) + "\"";
    j += ",\"ip\":\"" + wifiManagerLocalIP().toString() + "\"";
    j += ",\"name\":\"" + jsonEsc(deviceFqdn()) + "\"";
    j += ",\"gw\":\"" + wifiManagerGatewayIP().toString() + "\"";
    j += ",\"dns\":\"" + wifiManagerDNSIP().toString() + "\"";
  }
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"heapMin\":" + String(ESP.getMinFreeHeap());
  j += ",\"psram\":" + String(ESP.getFreePsram());
  j += ",\"sd\":" + String(sd_available ? "true" : "false");
  j += ",\"config\":" + String(webConfigEnabled() ? "true" : "false");
  j += ",\"maint\":" + String(webMaintenanceEnabled() ? "true" : "false");
  // The project's own live values, as one object. The page fills every
  // element with data-k="<key>" from it on each poll, so a project's status
  // card needs no script of its own.
  j += ",\"app\":{";
  if (APP_HOOKS.statusJson) APP_HOOKS.statusJson(j);
  j += "}}";
  return j;
}

// A pill rather than the word on its own: state reads as a shape before it
// reads as text, which is what a status page is for.
static String pill(bool good, int good_id, int bad_id, bool warn_when_bad = false) {
  String s = F("<span class='pill ");
  s += good ? F("ok'>") : (warn_when_bad ? F("wr'>") : F("bd'>"));
  s += T(good ? good_id : bad_id);
  s += F("</span>");
  return s;
}

static String row(const String &k, const String &v, bool mono = false) {
  String s = F("<div class='row'><span class='k'>");
  s += k;
  s += mono ? F("</span><span class='v mono'>") : F("</span><span class='v'>");
  s += v;
  s += F("</span></div>");
  return s;
}

static String uptimeText() {
  uint32_t s = millis() / 1000UL;
  uint32_t d = s / 86400UL; s %= 86400UL;
  uint32_t h = s / 3600UL;  s %= 3600UL;
  uint32_t m = s / 60UL;
  char b[40];
  if (d)      snprintf(b, sizeof(b), "%lu d %lu h", (unsigned long)d, (unsigned long)h);
  else if (h) snprintf(b, sizeof(b), "%lu h %lu min", (unsigned long)h, (unsigned long)m);
  else        snprintf(b, sizeof(b), "%lu min", (unsigned long)m);
  return String(b);
}

// Four bars instead of a number nobody converts in their head. The number is
// still there, quietly, for whoever wants it.
static String signalBars() {
  const int r = wifiManagerRSSI();
  const int bars = (r >= SIGNAL_BARS_4_DBM) ? 4 : (r >= SIGNAL_BARS_3_DBM) ? 3
                 : (r >= SIGNAL_BARS_2_DBM) ? 2 : 1;
  String s = F("<span class='sig'>");
  for (int i = 1; i <= 4; i++) {
    s += F("<i style='background:");
    s += (i <= bars) ? (bars >= 3 ? F("var(--accent)") : F("var(--warn)")) : F("var(--ink-4)");
    s += F("'></i>");
  }
  s += F("</span><em>");
  s += String(r);
  s += F(" dBm</em>");
  return s;
}

static String body() {
  String h;
  h.reserve(6600);
  h += F("<div class='grid g3'>");

  // ---- network ----------------------------------------------------------
  h += F("<div class='card'><h2>");
  h += T(STR_W_C_NETWORK);
  h += F("</h2><div class='rows'>");
  if (wifi_ok) {
    h += row(T(STR_W_R_WIFI), htmlEsc(cfg_wifi_ssid) + signalBars());
    h += row(T(STR_W_R_ADDRESS), wifiManagerLocalIP().toString(), true);
    // Both names when both apply: the DNS name is the one the network
    // serves, the mDNS name the one that works without it.
    if (deviceDomain()[0] || mdnsRunning()) h += row(T(STR_W_R_NAME), deviceFqdn(), true);
    if (deviceDomain()[0] && mdnsRunning()) {
      char m[DEVICE_FQDN_MAX + 1];
      deviceMdnsName(m, sizeof(m));
      h += row(T(STR_W_R_MDNS), m, true);
    }
    h += row(T(STR_W_R_GATEWAY), wifiManagerGatewayIP().toString(), true);
  } else {
    h += row(T(STR_W_R_WIFI), String(F("<span class='pill bd'>")) + T(STR_W_S_NOWIFI) + "</span>");
  }
  // With the network, not the hardware: it is how long this connection has
  // been standing, and it fills a card that had four rows beside eight.
  h += row(T(STR_W_R_UPTIME), uptimeText());
  h += F("</div></div>");

  // ---- the project's own card -----------------------------------------
  // Values that change carry data-k="<key>" and are kept current by the poll
  // from the "app" object of /status.json.
  if (APP_HOOKS.statusCard) h += APP_HOOKS.statusCard();

  // ---- system -----------------------------------------------------------
  h += F("<div class='card'><h2>");
  h += T(STR_W_C_HARDWARE);
  h += F("</h2><div class='rows'>");
  h += row(T(STR_W_R_CHIP), String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) +
                            " &middot; " APP_ENV);
  {
    char a[48];
    snprintf(a, sizeof(a), "%u kB (%u kB)", (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getMinFreeHeap() / 1024));
    h += F("<div class='row'><span class='k'>");
    h += T(STR_W_R_HEAP);
    h += F("</span><span class='v mono' id='heap'>");
    h += a;
    h += F("</span></div>");
    if (ESP.getPsramSize()) {
      snprintf(a, sizeof(a), "%u / %u kB", (unsigned)(ESP.getFreePsram() / 1024),
               (unsigned)(ESP.getPsramSize() / 1024));
      h += row(T(STR_W_R_PSRAM), a, true);
    }
  }
#ifdef APP_SD_CS
  h += row(T(STR_W_R_SD),    pill(sd_available, STR_W_S_READY, STR_W_S_MISSING, true));
#endif
  // The flash, in plain words: whether this device still runs on the old
  // partition table. Rows in this card rather than a card of their own, which
  // would leave one card alone in the second row on a three column screen.
  {
    const PartitionLayout &pl = partitionLayout();
    char a[64], b[16], c[16];
    // The pill alone: the slot size stands in the firmware row right below.
    h += row(T(STR_W_R_LAYOUT), pill(pl.current, STR_W_S_LAYOUT_NEW, STR_W_S_LAYOUT_OLD, true));
    snprintf(b, sizeof(b), "%.1f", pl.app_used_bytes / 1048576.0);
    snprintf(c, sizeof(c), "%.1f", pl.app_slot_bytes / 1048576.0);
    snprintf(a, sizeof(a), T(STR_W_S_MB_OF), b, c);
    h += row(T(STR_W_R_FIRMWARE), htmlEsc(a));
    if (pl.data_bytes && flashLogAvailable()) {
      // What the log uses of the whole area. Its own capacity (0.5 MB) read
      // as the size of the data area, which is 3.9 MB on the current table.
      snprintf(b, sizeof(b), "%.1f", flashLogUsedBytes() / 1048576.0);
      snprintf(c, sizeof(c), "%.1f", pl.data_bytes / 1048576.0);
      snprintf(a, sizeof(a), T(STR_W_S_MB_OF), b, c);
      h += row(T(STR_W_R_DATA_AREA), htmlEsc(a));
    } else if (pl.data_bytes) {
      snprintf(b, sizeof(b), "%.1f", pl.data_bytes / 1048576.0);
      snprintf(a, sizeof(a), T(STR_W_S_DATA_UNUSED), b);
      h += row(T(STR_W_R_DATA_AREA), htmlEsc(a));
    } else {
      h += row(T(STR_W_R_DATA_AREA), htmlEsc(T(STR_W_S_NONE)));
    }
    h += row(T(STR_W_R_COREDUMP), pill(pl.has_coredump, STR_W_S_YES, STR_W_S_NO));
    if (!pl.current && FLASHER_HAS_CURRENT_LAYOUT) {
      // A block of its own with air around it: inline, it sat on the last row
      // and on the rescan button below.
      h += F("<p class='hint' style='margin:14px 0 18px;line-height:1.6'>");
      h += T(STR_W_S_LAYOUT_OLD_HINT);
      h += F(" <a href='" FLASHER_URL "' target='_blank' rel='noopener' style='color:var(--accent)'>");
      h += T(STR_PART_HINT_QR);
      h += F(" &#8599;</a></p>");
    }
  }
  h += F("</div></div>");

  // ---- access -----------------------------------------------------------
  // Not a second navigation - the tabs already do that. This says which
  // sections are actually being served, which the tabs cannot show.
  h += F("<div class='card wide'><h2>");
  h += T(STR_W_C_ACCESS);
  h += F("</h2><div class='rows'>");
  h += row(T(STR_W_NAV_SETTINGS), pill(webConfigEnabled(), STR_W_S_ON, STR_W_S_OFF, true));
  // Named for what it opens. "Device" said nothing about firmware, logs and
  // tags being behind this one switch.
  h += row(T(STR_W_R_MAINT_GATE), pill(webMaintenanceEnabled(), STR_W_S_ON, STR_W_S_OFF, true));
  h += row(T(STR_W_R_PASSWORD),   pill(webHasPassword(), STR_W_S_SET, STR_W_S_NOTSET, true));
  h += F("</div><p class='note'>");
  h += T(STR_W_ACCESS_NOTE);
  h += F(" <b>");
  h += T(STR_W_OFF_PATH);
  h += F("</b>.</p>");
  if (!webHasPassword()) {
    // Said where the switches are shown, because that is the moment someone
    // turns a writing gate on and should know what it opens.
    h += F("<p class='note'>");
    h += T(STR_W_PASS_NOTE);
    h += F(" <b>");
    h += T(STR_W_OFF_PATH);
    h += F("</b>.</p>");
  }
  h += F("</div>");

  // ---- device -----------------------------------------------------------
  h += F("<div class='card wide'><h2>");
  h += T(STR_W_C_DEVICE);
  h += F("</h2><div class='inrow'><button class='quiet' id='rstbtn'>");
  h += T(STR_W_RESTART);
  h += F("</button>");
  if (webGateOpen(GATE_MAINT)) {
    h += F("<button class='quiet' id='portbtn'>");
    h += T(STR_W_OPEN_PORTAL);
    h += F("</button><button class='danger' id='facbtn'>");
    h += T(STR_W_FACTORY_RESET);
    h += F("</button>");
  }
  h += F("</div><p class='note'>");
  h += T(STR_W_RESTART_NOTE);
  h += F("</p><p class='msg' id='devmsg'></p></div></div>");

  h += webShellRestartUi();

  // Names are prefixed so nothing collides with the restart overlay above:
  // a duplicate const kills both blocks at parse time.
  h += F("<script>");
  h += webShellJsStrings();
  h += F("var SQ={portal:");
  h += jsStr(T(STR_W_PORTAL_Q));
  h += F(",factory:");
  h += jsStr(T(STR_W_FACTORY_Q));
  h += F(",reset:");
  h += jsStr(T(STR_W_FACTORY_RESET));
  h += F(",done:");
  h += jsStr(T(STR_W_FACTORY_DONE));
  h += F("};"
         // Two seconds, and only while the tab is in front: a status page
         // left open in a background tab would otherwise poll all day.
         "function stTick(){if(document.hidden)return;"
         "getJson('/status.json').then(function(d){if(!d)return;"
         "var e=$('heap');if(e)e.textContent=Math.round(d.heap/1024)+' kB ('+Math.round(d.heapMin/1024)+' kB)';"
         "var a=d.app||{};document.querySelectorAll('[data-k]').forEach(function(n){"
         "var k=n.getAttribute('data-k');if(k in a)n.textContent=a[k];});});}"
         "setInterval(stTick,2000);"
         "document.addEventListener('visibilitychange',stTick);"
         "$('rstbtn').addEventListener('click',function(){doRestart();});"
         "var pb=$('portbtn');if(pb)pb.addEventListener('click',function(){"
         "ask(SQ.portal).then(function(y){if(y)postFlash('/api/portal','1','devmsg',8000);});});"
         "var fb=$('facbtn');if(fb)fb.addEventListener('click',function(){"
         "ask(SQ.factory,SQ.reset,true).then(function(y){if(!y)return;"
         "post('/api/factoryreset','1').then(function(r){flash('devmsg',r.ok?SQ.done:WS.err,!r.ok);});});});"
         "</script>");
  return h;
}

static void routes(WebServer &srv) {
  // Restarting is not destructive - settings live in NVS - but it is still a
  // remote action, so it sits behind a gate rather than being exposed
  // whenever the landing page is reachable.
  srv.on("/api/restart", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_C_DEVICE))) return;
    logSD("Reboot: requested from web UI");
    srv.send(200, "application/json", "{\"ok\":true}");
    requestRestart(400);   // let the response actually leave before the reset
  });

  // Opens the AP portal as if the BOOT button had been held: for a device
  // that should move to another network. The station link drops with it, so
  // this page loses the device; the answer has to leave first.
  srv.on("/api/portal", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_C_DEVICE))) return;
    logSD("Portal: opened from the web UI");
    srv.send(200, "text/plain; charset=utf-8", T(STR_W_PORTAL_OPENING));
    portal_open_pending = true;
  });

  // Every setting, WiFi included, then a restart into the first-time setup.
  srv.on("/api/factoryreset", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_MAINT, T(STR_W_C_DEVICE))) return;
    logSD("Factory reset: requested from web UI");
    srv.send(200, "application/json", "{\"ok\":true}");
    factory_reset_pending = true;
    requestRestart(400);
  });

  // Polled by the restart overlay, which needs an answer the moment the
  // device is back. Kept as JSON rather than folded into the page.
  srv.on("/status.json", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_OPEN, T(STR_W_NAV_STATUS))) return;
    srv.send(200, "application/json", statusJson());
  });
}

extern const WebPage PAGE_STATUS;
const WebPage PAGE_STATUS = {
  "/", label, GATE_OPEN, nullptr,
  body, routes
};
