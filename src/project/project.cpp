// Glowing Square 2: the hooks the template core calls, the frame loop, and
// the routes and Status card the web interface shows for the matrix.
#include <Arduino.h>

#include "app/app_hooks.h"
#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "project/gs_mqtt.h"
#include "project/gs_state.h"
#include "project/matrix/matrix.h"
#include "project/modes/modes.h"
#include "project/party.h"
#include "project/render.h"
#include "services/ota_ui.h"
#include "services/setup_portal.h"
#include "web/web_access.h"
#include "web/web_jobs.h"
#include "web/web_pages.h"
#include "web/web_shell.h"
#include "lang.h"

// ---- brightness ----------------------------------------------------------------

// v1's fade: 3 s from wherever it is to the new level, in 20 ms steps. Here
// it runs the same in every mode, without stopping anything while it does.
#define FADE_MS       3000
#define FADE_STEP_MS  20

static uint8_t  s_brightness = 255;
static int16_t  s_fade_step = 0;
static uint8_t  s_fade_for = 255;
static uint32_t s_fade_next_ms = 0;

static void fadeTick(uint32_t now) {
  if (g_brightness_target != s_fade_for) {
    s_fade_for = g_brightness_target;
    const int change = (int)s_fade_for - (int)s_brightness;
    s_fade_step = (int16_t)(change / (FADE_MS / FADE_STEP_MS));
    if (s_fade_step == 0) s_fade_step = change > 0 ? 1 : -1;
    mqttStateChanged();
  }
  if (s_brightness == s_fade_for || (int32_t)(now - s_fade_next_ms) < 0) return;
  s_fade_next_ms = now + FADE_STEP_MS;
  int next = (int)s_brightness + s_fade_step;
  if ((s_fade_step > 0 && next > s_fade_for) || (s_fade_step < 0 && next < s_fade_for)) next = s_fade_for;
  const bool was_dark = s_brightness == 0;
  s_brightness = (uint8_t)next;
  matrixSetBrightness(s_brightness);
  // Coming back from dark: v1 fetched nothing while the display was off, so
  // the picture is stale. Fetch at once.
  if (was_dark && s_brightness > 0) fetchNow();
}

// ---- the setup WiFi on the matrix ------------------------------------------

static ScrollRow s_portal_row[3];
static bool s_portal_shown = false;

// Three rows that keep scrolling: the network, its password, the address.
// How long each line of the setup screen stands still before it moves, and
// how fast it then goes. Long enough to read a password off the panel.
#define PORTAL_HOLD_MS  4000
#define PORTAL_STEP_MS  70

static void portalRound() {
  char url[32];
  setupPortalUrl(url, sizeof(url));
  const uint16_t C_KEY = rgb565(255, 80, 0), C_VAL = rgb565(200, 200, 200);
  matrixCanvas().fillScreen(0);
  drawText(0, 0, "Setup", rgb565(0, 255, 255));
  // Lines that fit stay still; the rest hold, scroll once and snap back.
  s_portal_row[0].start(8, "W:", setupPortalSsid(), C_KEY, C_VAL, PORTAL_STEP_MS, PORTAL_HOLD_MS, true);
  s_portal_row[1].start(16, "P:", setupPortalPassword(), C_KEY, C_VAL, PORTAL_STEP_MS, PORTAL_HOLD_MS, true);
  s_portal_row[2].start(24, "", url + 7, C_KEY, C_VAL, PORTAL_STEP_MS, PORTAL_HOLD_MS, true);   // no "http://"
}

static void portalShow(bool open) {
  s_portal_shown = open;
  if (!open) {
    matrixCanvas().fillScreen(0);
    modeOps(g_mode).enter();
    return;
  }
  portalRound();
}

// A round ends when every moving line is back at its start; the next one
// holds them all still again. If nothing needs to move, nothing happens.
static bool portalFrame(uint32_t now) {
  bool drew = false, moving = false;
  for (ScrollRow &r : s_portal_row) {
    drew |= r.tick(now);
    moving |= !r.done();
  }
  if (!moving && drew) {
    portalRound();
    return true;
  }
  return drew;
}

// ---- the frame loop ----------------------------------------------------------

static bool s_matrix_ok = false;
static uint8_t s_shown_mode = 255;
static uint8_t s_shown_party = PARTY_OFF;
static bool s_shown_offline = false;

static void projectSetup() {
  s_matrix_ok = matrixBegin();
  logSDf("Matrix: %s %s", matrixDriverName(), s_matrix_ok ? "up" : "FAILED");
  renderBegin();
  g_brightness_target = (uint8_t)g_boot_brightness;
  s_brightness = s_fade_for = g_brightness_target;
  matrixSetBrightness(s_brightness);
  partyBegin();
  mqttBegin();
  matrixCanvas().fillScreen(0);
  matrixShow();
}

static void projectLoop() {
  const uint32_t now = millis();
  fadeTick(now);
  fetchTick();
  mqttTick();

  bool dirty = false;
  if (s_portal_shown) {
    dirty = portalFrame(now);
  } else if (g_party != PARTY_OFF) {
    dirty = partyFrame(now);
    s_shown_party = g_party;
  } else {
    // Back from party mode, a new mode, or the offline dot coming or going:
    // the mode draws from what it already has.
    if (g_mode_changed || s_shown_mode != g_mode || s_shown_party != PARTY_OFF) {
      if (g_mode_changed || s_shown_mode != g_mode) {
        logSDf("Mode: %s", modeKey(g_mode));
        mqttStateChanged();
      }
      // A new mode, or the end of party mode (which fetched nothing while it
      // ran): fresh data at once rather than at the next interval.
      fetchNow();
      // Drawn from scratch below, so the offline dot is decided again too.
      s_shown_offline = false;
      g_mode_changed = false;
      s_shown_mode = g_mode;
      s_shown_party = PARTY_OFF;
      partyFrame(now);   // lets party mode note that it ended
      modeOps(g_mode).enter();
      dirty = true;
    }
    if (fetchOffline() != s_shown_offline) {
      s_shown_offline = fetchOffline();
      if (s_shown_offline) drawOffline();
      else                 modeOps(g_mode).enter();
      dirty = true;
    }
    if (!s_shown_offline) dirty |= modeOps(g_mode).frame(now);
  }
  if (dirty && s_matrix_ok) matrixShow();
}

static void onPortal(bool open) { portalShow(open); if (s_matrix_ok) matrixShow(); }

// ---- web: the Status card and the live view -----------------------------------

static const char *partyName(uint8_t p) {
  return T(p == PARTY_ON ? STR_P_PARTY_ON : p == PARTY_CHILL ? STR_P_PARTY_CHILL : STR_P_PARTY_OFF);
}

static String lastUpdate() {
  const uint32_t ok = fetchLastOkMs();
  if (!ok) return fetchLastError()[0] ? String(fetchLastError()) : String(T(STR_P_NEVER));
  String s = String((millis() - ok) / 1000) + " s ago";
  if (fetchLastError()[0]) s += String(" (") + fetchLastError() + ")";
  return s;
}

static String mqttText() {
  if (!g_mqtt_on) return T(STR_P_MQTT_OFF);
  return T(mqttConnected() ? STR_P_MQTT_CONNECTED : STR_P_MQTT_DOWN);
}

static String row(int label, const char *key, const String &value) {
  String h = F("<div class='row'><span class='k'>");
  h += T(label);
  h += F("</span><span class='v' data-k='");
  h += key;
  h += F("'>");
  h += htmlEsc(value.c_str());
  h += F("</span></div>");
  return h;
}

// The live view: the canvas, fetched as hex once a second while the tab is
// in front, drawn 6x with a gap so it reads as LEDs.
static String statusCard() {
  String h;
  h.reserve(2600);
  h += F("<div class='card wide'><h2>");
  h += T(STR_P_CARD);
  h += F("</h2><canvas id='gsv' width='384' height='192' style='width:100%;max-width:576px;"
         "aspect-ratio:2/1;background:#000;border-radius:8px;display:block;margin:0 auto 14px'></canvas>"
         "<div class='rows'>");
  h += row(STR_P_CARD_MODE, "mode", T((StringID)(STR_P_MODE_TUBE + g_mode)));
  h += row(STR_P_CARD_PARTY, "party", partyName(g_party));
  h += row(STR_P_CARD_BRIGHT, "bright", String(g_brightness_target));
  h += row(STR_P_CARD_LAST, "last", lastUpdate());
  h += row(STR_P_CARD_MQTT, "mqtt", mqttText());
  h += F("</div>");
  if (webGateOpen(GATE_CONFIG)) {
    h += F("<div class='inrow' style='margin-top:14px'><input type='range' id='gsb' min='0' max='255' value='");
    h += String(g_brightness_target);
    h += F("'><select id='gsp'>");
    for (uint8_t p = 0; p < 3; p++) {
      h += F("<option value='");
      h += String(p);
      h += F("'");
      if (p == g_party) h += F(" selected");
      h += F(">");
      h += partyName(p);
      h += F("</option>");
    }
    h += F("</select></div>");
  }
  h += F("<span class='msg' id='gs-s'></span></div><script>"
         "(function(){var c=$('gsv'),x=c.getContext('2d');"
         "function draw(hex){x.fillStyle='#000';x.fillRect(0,0,384,192);"
         "for(var i=0;i<2048;i++){var v=parseInt(hex.substr(i*4,4),16);if(!v)continue;"
         "var r=(v>>11)<<3,g=((v>>5)&63)<<2,b=(v&31)<<3;x.fillStyle='rgb('+r+','+g+','+b+')';"
         "x.fillRect((i%64)*6+1,Math.floor(i/64)*6+1,5,5);}}"
         "function tick(){if(document.hidden)return;fetch('/api/gs/frame',{cache:'no-store'})"
         ".then(function(r){return r.ok?r.text():'';}).then(function(t){if(t.length===8192)draw(t);})"
         ".catch(function(){});}"
         "tick();setInterval(tick,1000);"
         "var b=$('gsb');if(b)b.addEventListener('change',function(){postFlash('/api/gs/brightness',b.value,'gs-s',3000);});"
         "var p=$('gsp');if(p)p.addEventListener('change',function(){postFlash('/api/gs/party',p.value,'gs-s',3000);});"
         "})();</script>");
  return h;
}

static void statusJson(String &j) {
  j += F("\"mode\":\"");
  j += jsonEsc(T((StringID)(STR_P_MODE_TUBE + g_mode)));
  j += F("\",\"party\":\"");
  j += jsonEsc(partyName(g_party));
  j += F("\",\"bright\":");
  j += String(g_brightness_target);
  j += F(",\"last\":\"");
  j += jsonEsc(lastUpdate().c_str());
  j += F("\",\"mqtt\":\"");
  j += jsonEsc(mqttText().c_str());
  j += '"';
}

static void statusLine(char *buf, size_t n) {
  snprintf(buf, n, "mode %s, matrix %s%s, mqtt %s", modeKey(g_mode), matrixDriverName(),
           s_matrix_ok ? "" : " FAILED", g_mqtt_on ? "on" : "off");
}

// ---- web: routes ----------------------------------------------------------------

// "Test now" on the Settings page: one fetch of that mode on the web job
// task, the answer collected by the page's poll.
static uint8_t s_test_mode = 0;
static void testJob(WebJobResult &res, const char *arg) {
  (void)arg;
  char out[sizeof(res.err)];
  res.ok = fetchTestMode(s_test_mode, out, sizeof(out));
  snprintf(res.err, sizeof(res.err), "%s", out);
}

static int modeFromName(const String &m) {
  for (uint8_t i = 0; i < MODE_COUNT; i++) if (m == modeKey(i)) return i;
  return -1;
}

static void registerRoutes(WebServer &srv) {
  // The canvas as 2048 RGB565 values in hex: what the panel shows.
  srv.on("/api/gs/frame", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_OPEN, "frame")) return;
    static const char HEX_DIGITS[] = "0123456789abcdef";
    const uint16_t *px = matrixCanvas().getBuffer();
    String out;
    out.reserve(MATRIX_W * MATRIX_H * 4);
    for (int i = 0; i < MATRIX_W * MATRIX_H; i++) {
      const uint16_t v = px[i];
      out += HEX_DIGITS[v >> 12]; out += HEX_DIGITS[(v >> 8) & 15];
      out += HEX_DIGITS[(v >> 4) & 15]; out += HEX_DIGITS[v & 15];
    }
    srv.sendHeader("Cache-Control", "no-store");
    srv.send(200, "text/plain", out);
  });

  srv.on("/api/gs/brightness", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, "brightness")) return;
    const long v = strtol(srv.arg("plain").c_str(), nullptr, 10);
    if (v < 0 || v > 255) { srv.send(400, "text/plain", T(STR_W_RANGE)); return; }
    g_brightness_target = (uint8_t)v;
    srv.send(200, "text/plain; charset=utf-8", T(STR_W_SAVED));
  });

  srv.on("/api/gs/party", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, "party")) return;
    const long v = strtol(srv.arg("plain").c_str(), nullptr, 10);
    if (v < PARTY_OFF || v > PARTY_ON) { srv.send(400, "text/plain", T(STR_W_RANGE)); return; }
    g_party = (uint8_t)v;
    mqttStateChanged();
    srv.send(200, "text/plain; charset=utf-8", T(STR_W_SAVED));
  });

  srv.on("/api/gs/test", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, "test")) return;
    const int m = modeFromName(srv.arg("plain"));
    if (m < 0) { srv.send(400, "text/plain", T(STR_W_ERROR)); return; }
    if (fetchBusy() || webJobState() != WJS_IDLE) { srv.send(409, "text/plain", "busy"); return; }
    s_test_mode = (uint8_t)m;
    if (!webJobStartFn(testJob, (uint8_t)(m + 1), nullptr, 75000)) {
      srv.send(503, "text/plain", "busy");
      return;
    }
    srv.send(202, "text/plain", "started");
  });

  srv.on("/api/gs/test", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, "test")) return;
    const int m = modeFromName(srv.arg("m"));
    if (webJobKind() != WJ_PROJECT || webJobProjectId() != (uint8_t)(m + 1)) {
      srv.send(200, "application/json", "{\"ok\":false,\"text\":\"no test running\"}");
      return;
    }
    if (webJobState() == WJS_RUNNING) {
      srv.send(200, "application/json", "{\"busy\":true}");
      return;
    }
    const WebJobResult &r = webJobResult();
    String j = String("{\"ok\":") + (r.ok ? "true" : "false") + ",\"text\":\"" + jsonEsc(r.err) + "\"}";
    webJobTake();
    srv.send(200, "application/json", j);
  });
}

// ---- the table the core reads ---------------------------------------------------

const AppHooks APP_HOOKS = {
  /* setup             */ projectSetup,
  /* loop              */ projectLoop,
  /* onWifiUp          */ fetchNow,
  /* onWifiDown        */ nullptr,
  /* onPortal          */ onPortal,
  /* registerWebRoutes */ registerRoutes,
  /* statusCard        */ statusCard,
  /* statusJson        */ statusJson,
  /* statusLine        */ statusLine,
  /* onMdnsService     */ nullptr,
  /* buildMainScreen   */ nullptr,
  /* openScreen        */ nullptr,
};

const WebPage* const PROJECT_WEB_PAGES[] = { nullptr };
const size_t         PROJECT_WEB_PAGE_COUNT = 0;

// ---- the panel during a firmware update -------------------------------------
// Writing flash pauses code running from flash, the panel refresh included,
// and a stalled refresh leaves one row lit: the panel strobes for the whole
// transfer. Dark is calmer, and says that something is happening. These
// replace the core's weak no-ops (services/ota_ui.h).

void otaUiShow() {
  matrixSetBrightness(0);
}

void otaUiHide() {
  matrixSetBrightness(s_brightness);
}

void otaUiFailed() {
  matrixSetBrightness(s_brightness);
  logSD("Update failed: display back on");
}
