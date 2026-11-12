#include "web/web_access.h"

#include "services/ota_state.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include "mbedtls/base64.h"

#include "app_config.h"
#include "hardware/sd_logger.h"
#include "services/device_name.h"
#include "services/prefs_store.h"
#include "services/wifi_manager.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

// Master defaults ON so browsing the device address gives you something
// rather than a dead port. On a board with a screen the two writing gates
// default OFF: everything behind them changes how the device behaves or
// writes firmware, and the screen is where the owner turns them on. A
// headless board has no such place, so there they default ON and the
// password (set from the Network page) is what locks them.
static void sendTinyPage(WebServer &srv, int code, const char *title, const String &body);

static bool master_on      = true;
#ifdef APP_UI_LVGL
#define WEB_WRITE_GATES_DEFAULT false
#else
#define WEB_WRITE_GATES_DEFAULT true
#endif

static bool config_on      = WEB_WRITE_GATES_DEFAULT;
static bool maintenance_on = WEB_WRITE_GATES_DEFAULT;
static char s_pass[WEB_PASS_MAX + 1] = "";

bool webMasterEnabled()      { return master_on; }
bool webConfigEnabled()      { return config_on; }
bool webMaintenanceEnabled() { return maintenance_on; }
bool webHasPassword()        { return s_pass[0] != '\0'; }

// Keeps printable ASCII, drops the rest, and treats anything shorter than
// the minimum as no password: a two character lock would only look like one.
// The device's numpad can only enter digits; the web can enter any.
static void passFromString(const char *in) {
  size_t n = 0;
  for (const char *p = in ? in : ""; *p && n < WEB_PASS_MAX; p++) {
    if (*p > ' ' && *p < 0x7f) s_pass[n++] = *p;
  }
  s_pass[n] = '\0';
  if (n < WEB_PASS_MIN) s_pass[0] = '\0';
}

void webSetPassword(const char *pass) {
  passFromString(pass);
  prefsPutString("web_pass", s_pass);
  logSDf("Web: password %s", s_pass[0] ? "set" : "removed");
}

void webAccessLoad() {
  master_on      = prefsGetBool("web_master", true);
  config_on      = prefsGetBool("web_config", WEB_WRITE_GATES_DEFAULT);
  maintenance_on = prefsGetBool("web_maint",  WEB_WRITE_GATES_DEFAULT);
  passFromString(prefsGetString("web_pass", "").c_str());
}

// Nothing here touches the socket. Whether the server listens is derived
// once a second in webServerSyncState() from these switches, so there is
// exactly one place that opens and closes port 80.
void webSetMasterEnabled(bool on) {
  master_on = on;
  prefsPutBool("web_master", on);
}

void webSetConfigEnabled(bool on) {
  config_on = on;
  prefsPutBool("web_config", on);
}

void webSetMaintenanceEnabled(bool on) {
  maintenance_on = on;
  prefsPutBool("web_maint", on);
}

// What the current visit switched on, so ending it switches off exactly that
// and leaves alone what was on before.
static bool s_visit_master = false;
static bool s_visit_config = false;
static bool s_visit_maint  = false;

bool webVisitOpen(WebGate g) {
  if (!master_on) { master_on = true; s_visit_master = true; }
  if (g == GATE_CONFIG && !config_on)      { config_on = true;      s_visit_config = true; }
  if (g == GATE_MAINT  && !maintenance_on) { maintenance_on = true; s_visit_maint  = true; }
  return s_visit_master || s_visit_config || s_visit_maint;
}

bool webVisitEnd() {
  if (!s_visit_master && !s_visit_config && !s_visit_maint) return false;
  if (s_visit_master) master_on      = false;
  if (s_visit_config) config_on      = false;
  if (s_visit_maint)  maintenance_on = false;
  logSDf("Web: visit over, closed%s%s%s", s_visit_master ? " master" : "",
         s_visit_config ? " config" : "", s_visit_maint ? " maintenance" : "");
  s_visit_master = s_visit_config = s_visit_maint = false;
  return true;
}

bool webGateOpen(WebGate g) {
  switch (g) {
    case GATE_ALWAYS: return true;
    case GATE_OPEN:   return master_on;
    case GATE_CONFIG: return master_on && config_on;
    case GATE_MAINT:  return master_on && maintenance_on;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Who is asking. The gates say whether a feature is on; none of them ever
// said who was allowed to use it. Three questions are asked before the gate:
//
//  - Is the Host header one of our own names? A page on attacker.example can
//    have its DNS flip to this device's LAN address (DNS rebinding) and then
//    read every reply as if it were same-origin. Its requests still carry
//    Host: attacker.example, which is the one thing it cannot change.
//  - For a request that changes something: does the Origin header, when the
//    browser sends one, name us? Every cross-site POST a browser makes
//    carries the origin of the page that made it, and /app.js posts
//    text/plain, which needs no preflight - so without this check any web
//    page the owner visits could change a setting, restart the device or
//    submit a firmware image through a hidden form.
//  - For the two writing gates: the password, when one is set.
// ---------------------------------------------------------------------------

enum WebVerdict : uint8_t {
  WEB_OK = 0,
  WEB_FLASHING,      // an image is being written, nothing else is served
  WEB_BAD_HOST,      // Host header is not one of our names
  WEB_BAD_ORIGIN,    // a browser's cross-site POST
  WEB_GATE_SHUT,     // the switch on the device is off
  WEB_NEED_AUTH      // password set, none or a wrong one sent
};

static void lowerTrim(String &s) {
  s.trim();
  s.toLowerCase();
  while (s.endsWith(".")) s.remove(s.length() - 1);
}

// The names this device answers to: its address, its full name, its bare
// label and its mDNS name. A port suffix is ignored, the case too.
static bool hostIsOurs(const String &host_in) {
  // No Host header at all is not a browser. Nothing that rebinding or a
  // cross-site form can produce comes without one.
  if (host_in.length() == 0) return true;
  String host = host_in;
  const int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);
  lowerTrim(host);
  if (host.length() == 0) return false;

  if (host == wifiManagerLocalIP().toString()) return true;

  String s = deviceFqdn();  lowerTrim(s); if (s.length() && host == s) return true;
  s = deviceLabel();        lowerTrim(s); if (s.length() && host == s) return true;
  char m[DEVICE_FQDN_MAX + 1];
  deviceMdnsName(m, sizeof(m));
  s = m;                    lowerTrim(s); if (s.length() && host == s) return true;
  return false;
}

// "http://<one of our names>[:port]". Anything else - https, "null" from a
// sandboxed frame, another host - is a page that is not ours.
static bool originIsOurs(const String &origin) {
  if (!origin.startsWith("http://")) return false;
  String h = origin.substring(7);
  const int slash = h.indexOf('/');
  if (slash >= 0) h = h.substring(0, slash);
  return h.length() > 0 && hostIsOurs(h);
}

// "Authorization: Basic base64(user:password)". The user name is not looked
// at: the device has one password and no accounts.
static bool passwordOk(WebServer &srv) {
  if (!srv.hasHeader("Authorization")) return false;
  String a = srv.header("Authorization");
  a.trim();
  if (!a.startsWith("Basic ")) return false;
  a = a.substring(6);
  a.trim();
  unsigned char out[64];
  size_t olen = 0;
  if (mbedtls_base64_decode(out, sizeof(out) - 1, &olen,
                            (const unsigned char *)a.c_str(), a.length()) != 0) {
    return false;
  }
  out[olen] = '\0';
  const char *colon = strchr((const char *)out, ':');
  const char *pw = colon ? colon + 1 : (const char *)out;
  return strcmp(pw, s_pass) == 0;
}

// When a browser last asked for anything, answered or refused. GATE_ALWAYS
// routes are machines (webhooks, a home automation server) and do not count.
static uint32_t s_last_browser_ms = 0;
static bool     s_browser_seen    = false;

bool webBrowserSeenWithin(uint32_t ms) {
  return s_browser_seen && (millis() - s_last_browser_ms) < ms;
}

static WebVerdict verdict(WebServer &srv, WebGate g) {
  if (g != GATE_ALWAYS) {
    s_last_browser_ms = millis();
    s_browser_seen = true;
  }
  // Writing firmware holds the loop, and the progress view is served from
  // inside that loop so the bar can move. Nothing else is: a page built while
  // an image is being written would come out of the same heap, and a
  // /status.json answered mid-flash would tell the waiting browser the device
  // is back when it has not even rebooted yet.
  if (gh_flash_active && srv.uri() != "/api/ota/progress") return WEB_FLASHING;

  // Machines on the LAN: no Host check (they may use any name), but a
  // browser's cross-site POST is still refused.
  if (g == GATE_ALWAYS) {
    if (srv.method() != HTTP_GET && srv.hasHeader("Origin") &&
        !originIsOurs(srv.header("Origin"))) return WEB_BAD_ORIGIN;
    return WEB_OK;
  }

  if (!hostIsOurs(srv.hostHeader())) return WEB_BAD_HOST;
  if (srv.method() != HTTP_GET && srv.hasHeader("Origin") &&
      !originIsOurs(srv.header("Origin"))) {
    return WEB_BAD_ORIGIN;
  }
  if (!webGateOpen(g)) return WEB_GATE_SHUT;
  if (g >= GATE_CONFIG && webHasPassword() && !passwordOk(srv)) return WEB_NEED_AUTH;
  return WEB_OK;
}

bool webAllowed(WebServer &srv, WebGate g) {
  return verdict(srv, g) == WEB_OK;
}

// One line per refusal, but not one per poll: a status page left open under a
// name the device does not know would otherwise write every two seconds.
static void logRefusal(WebServer &srv, const char *why) {
  static unsigned long last_ms = 0;
  if (last_ms && millis() - last_ms < 10000UL) return;
  last_ms = millis();
  logSDf("Web: %s refused (%s) from %s, host '%s'", srv.uri().c_str(), why,
         srv.client().remoteIP().toString().c_str(), srv.hostHeader().c_str());
}

bool webRequire(WebServer &srv, WebGate g, const char *what) {
  const bool api = srv.uri().startsWith("/api/");
  switch (verdict(srv, g)) {
    case WEB_OK:
      return true;
    case WEB_FLASHING:
      srv.send(503, "text/plain", "flashing");
      return false;
    case WEB_BAD_HOST:
      logRefusal(srv, "host");
      srv.send(403, "text/plain; charset=utf-8", T(STR_W_BAD_HOST));
      return false;
    case WEB_BAD_ORIGIN:
      logRefusal(srv, "origin");
      srv.send(403, "text/plain; charset=utf-8", T(STR_W_BAD_ORIGIN));
      return false;
    case WEB_GATE_SHUT:
      // An /api/* caller is a script, not a reader. Handing it a kilobyte of
      // styled HTML it will never render only makes the failure harder to
      // see in a console.
      if (api) srv.send(403, "text/plain", T(STR_W_OFF_BODY));
      else     webSendDisabled(srv, what, T(STR_W_OFF_PATH));
      return false;
    case WEB_NEED_AUTH:
      // The browser turns this into its own password prompt on a page, and
      // sends the answer along with everything it asks for afterwards. The
      // body is what shows behind that prompt, and for a moment after a
      // cancelled one - a page in the device's own style rather than a bare
      // sentence on white.
      srv.sendHeader("WWW-Authenticate", "Basic realm=\"" APP_NAME "\"");
      if (api) {
        srv.send(401, "text/plain; charset=utf-8", T(STR_W_AUTH_NEEDED));
      } else {
        String b = F("<p>");
        b += T(STR_W_AUTH_NEEDED);
        b += F("</p>");
        sendTinyPage(srv, 401, T(STR_W_R_PASSWORD), b);
      }
      return false;
  }
  return false;
}

// The one small page this file serves by itself, for the gate that is shut
// and for the password prompt. It stands outside the shell, no tab strip and
// no footer, but takes the palette the pages run: /app.css and /app.js are
// behind no gate and no password, so they load where this page is served.
// The rules after them are this page's own and win over the shared ones.
static void sendTinyPage(WebServer &srv, int code, const char *title, const String &body) {
  String h;
  h.reserve(2200);
  h += webShellDocOpen();
  h += F("<head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<link rel='icon' type='image/png' href='/favicon.png'>"
         "<title>" APP_NAME "</title>");
  h += webShellAssets();
  h += F("<style>"
         "body{justify-content:center;padding:32px 16px;text-align:center}"
         ".card{padding:28px;max-width:460px}"
         "h1{color:var(--warn);font-size:19px;margin-bottom:12px}"
         "p{color:var(--ink-2);font-size:14px;line-height:1.6;margin-bottom:10px}"
         ".path{font-family:var(--mono);color:var(--accent);"
         "background:var(--surface-2);border:1px solid var(--line-soft);border-radius:8px;"
         "padding:8px 12px;display:inline-block;margin:6px 0;font-size:13px}"
         "a{color:var(--accent);text-decoration:none;font-size:14px}"
         "a:hover{text-decoration:underline}"
         "</style></head><body><div class='card'><h1>");
  h += title;
  h += F("</h1>");
  h += body;
  h += F("<p><a href='/'>&#8592; ");
  h += T(STR_W_BACK_STATUS);
  h += F("</a></p></div></body></html>");
  srv.send(code, "text/html", h);
}

void webSendDisabled(WebServer &srv, const char *what, const char *menu) {
  char title[80];
  snprintf(title, sizeof(title), T(STR_W_OFF_TITLE), what ? what : "");
  String b;
  b += F("<p>");
  b += T(STR_W_OFF_BODY);
  b += F("</p><p>");
  b += T(STR_W_OFF_WHERE);
  b += F("</p><div class='path'>");
  b += (menu && menu[0]) ? menu : T(STR_W_OFF_PATH);
  b += F("</div><p>");
  b += T(STR_W_OFF_RELOAD);
  b += F("</p>");
  sendTinyPage(srv, 403, title, b);
}
