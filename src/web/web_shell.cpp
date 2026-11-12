#include "web/web_shell.h"

#include <string.h>

#include <ArduinoJson.h>   // before lang.h: T() collides with its template parameter

#include "app_config.h"
#include "lang.h"
#include "app/app_state.h"
#include "services/device_name.h"
#include "services/prefs_store.h"
#include "services/wifi_manager.h"
#include "web/web_access.h"
#include "web/web_pages.h"
#include "web/web_static.h"


// Wraps a translated string as a JavaScript string literal, quotes included.
// Every T() text that ends up inside a <script> block goes through this: the
// German table carries apostrophes and quotation marks, and one unescaped
// quote takes the whole page's script down without a word in the console.
//
// "<" is escaped as well. A literal "</script>" inside a JS string still ends
// the script element as far as the HTML parser is concerned, so a translation
// that ever contained one would break out of it.
String jsStr(const char *in) {
  String o = "\"";
  for (const char *p = in ? in : ""; *p; p++) {
    switch (*p) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': break;
      case '<':  o += "\\x3C"; break;
      default:   o += *p;
    }
  }
  o += "\"";
  return o;
}

String jsonEsc(const char *s) {
  String o;
  // The char-at-a-time append below reallocates in steps, and release notes
  // run to several kilobytes. One guess up front costs nothing and saves the
  // chain of copies.
  const size_t n = s ? strlen(s) : 0;
  o.reserve(n + n / 8 + 8);
  for (const char *p = s ? s : ""; *p; p++) {
    if (*p == '"' || *p == '\\') { o += '\\'; o += *p; }
    // Multi-line values arrive with CRLF. The CR is dropped rather than turned
    // into a space, which would leave one at the end of every line.
    else if (*p == '\n')          { o += "\\n"; }
    else if (*p == '\r')          { }
    else if ((uint8_t)*p < 0x20)  { o += ' '; }
    else                          { o += *p; }
  }
  return o;
}

String htmlEsc(const char *s) {
  String o;
  const size_t n = s ? strlen(s) : 0;
  o.reserve(n + 16);
  for (const char *p = s ? s : ""; *p; p++) {
    switch (*p) {
      case '&':  o += "&amp;";  break;
      case '<':  o += "&lt;";   break;
      case '>':  o += "&gt;";   break;
      case '"':  o += "&quot;"; break;
      case '\'': o += "&#39;";  break;
      default:   o += *p;
    }
  }
  return o;
}

// ---------------------------------------------------------------------------
// The palette is the device's own, unchanged. What carries the look is the
// type scale, the state pills and the space - a 720 px column instead of the
// 480 px this was written at when the only viewer anyone pictured was a
// phone.
//
// No web font on purpose. The device sits on a LAN that may have no route to
// the internet, so a font link would fail silently and drop to whatever the
// browser picked.
// ---------------------------------------------------------------------------
static uint8_t s_web_theme = WEB_THEME_AUTO;

void webThemeLoad() {
  const uint8_t v = prefsGetUChar("web_theme", WEB_THEME_AUTO);
  s_web_theme = v <= WEB_THEME_LIGHT ? v : WEB_THEME_AUTO;
}

uint8_t webThemeGet() { return s_web_theme; }

bool webThemeSet(uint8_t v) {
  if (v > WEB_THEME_LIGHT) return false;
  s_web_theme = v;
  prefsPutUChar("web_theme", v);
  return true;
}

const char* webThemeKey(uint8_t v) {
  return v == WEB_THEME_DARK ? "dark" : v == WEB_THEME_LIGHT ? "light" : "auto";
}

// data-pref is the stored choice; app.js turns it into data-theme before the
// body is drawn (auto follows the browser). The project's accent rides along
// as two custom properties, one per palette, which app.css mixes the
// accent fills from.
String webShellDocOpen() {
  String h;
  h.reserve(200);
  h += F("<!DOCTYPE html><html data-pref='");
  h += webThemeKey(s_web_theme);
  h += F("' data-theme='");
  h += s_web_theme == WEB_THEME_LIGHT ? "light" : "dark";
  h += F("' style='--accent-d:" APP_ACCENT_DARK ";--accent-l:" APP_ACCENT_LIGHT "'>");
  return h;
}

String webShellAssets() {
  // The stylesheet is a route now, not 4 kB rebuilt on the internal heap for
  // every request. Keyed by firmware version so a cached copy can never
  // outlive the page it styles - see web_static.cpp.
  String h = F("<link rel='stylesheet' href='/app.css?v=");
  h += webStaticVersion();
  // In the head and without defer, so $, flash and post exist by the time a
  // page's own script runs at the end of the body.
  h += F("'><script src='/app.js?v=");
  h += webStaticVersion();
  // The two buttons of ask(), on every page: app.js is cached across
  // languages, so its own words stay English fallbacks.
  h += F("'></script><script>WS.yes=");
  h += jsStr(T(STR_CONFIRM));
  h += F(";WS.no=");
  h += jsStr(T(STR_CANCEL));
  h += F(";</script>");
  return h;
}

String webShellHead(const char *subtitle) {
  String h;
  h.reserve(680);   // the stylesheet is a route now, this is just the head
  h += webShellDocOpen();
  h += F("<head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<link rel='icon' type='image/png' href='/favicon.png'>"
      "<link rel='apple-touch-icon' href='/apple-touch-icon.png'>"
      "<title>" APP_NAME " - ");
  h += subtitle;
  h += F("</title>");
  h += webShellAssets();
  h += F("</head><body><div class='wrap'>");
  return h;
}

// The address block sits in the header because it is the one thing people
// come to this page to read or to type, and it used to be a row halfway down
// the status list. Name large, IP small underneath - not either or, because
// some Android versions still will not resolve a .local name.
String webShellHeader() {
  String h;
  h += F("<div class='head'><div class='mark'>"
         "<img src='/logo.svg' width='52' height='52' alt='" APP_NAME "'></div><div>"
         "<div class='wordmark'>" APP_NAME_MAIN "<span>" APP_NAME_ACCENT "</span></div>"
         "<div class='subline'>");
  h += FW_VERSION;
  h += F(" &nbsp;&middot;&nbsp; ");
  h += ESP.getChipModel();
  h += F("</div></div>");

  if (wifi_ok) {
    char alt[48];
    deviceAltAddress(alt, sizeof(alt));
    h += F("<div class='addr'><b>");
    // deviceAltAddress() is empty exactly when the big line already carries
    // the bare IP, so the two never say the same thing twice.
    const String ip = wifiManagerLocalIP().toString();
    h += alt[0] ? deviceFqdn() : ip.c_str();
    h += F("</b><i>");
    h += alt;
    h += F("</i></div>");
  }
  h += F("</div>");
  return h;
}

String webShellNav(const char *active) {
  String n = F("<div class='nav'>");
  // One strip built from the one table. There used to be two of these in two
  // files with two hand kept lists, and they disagreed about BamBuddy.
  for (size_t i = 0; i < webPageCount(); i++) {
    const WebPage &p = *webPageAt(i);
    if (!webPageVisible(p)) continue;
    n += String(F("<a class='")) + (strcmp(p.path, active) == 0 ? "on" : "") +
         "' href='" + p.path + "'>" + webPageLabel(p) + "</a>";
  }
  n += F("</div>");
  return n;
}

// Marks are inline rather than fetched: the device often sits on a LAN with
// no route out, and an icon from a CDN would render as a broken image.
#define SVG_OPEN "<svg viewBox='0 0 24 24' aria-hidden='true' focusable='false'><path d='"
#define SVG_END  "'/></svg>"

#define SVG_GITHUB \
    "M12 .5C5.37.5 0 5.87 0 12.5c0 5.3 3.44 9.8 8.21 11.39.6.11.82-.26.82-.58" \
    "v-2.03c-3.34.73-4.04-1.61-4.04-1.61-.55-1.39-1.34-1.76-1.34-1.76-1.09-.75." \
    "08-.73.08-.73 1.2.08 1.84 1.24 1.84 1.24 1.07 1.84 2.81 1.31 3.5 1 .11-.78." \
    "42-1.31.76-1.61-2.67-.3-5.47-1.34-5.47-5.96 0-1.32.47-2.39 1.24-3.23-.12-." \
    "31-.54-1.53.12-3.18 0 0 1.01-.32 3.3 1.23a11.5 11.5 0 016.01 0c2.29-1.55 " \
    "3.3-1.23 3.3-1.23.66 1.65.24 2.87.12 3.18.77.84 1.24 1.91 1.24 3.23 0 4.63" \
    "-2.81 5.65-5.49 5.95.43.37.82 1.1.82 2.22v3.29c0 .32.22.7.83.58A12.01 12.01" \
    " 0 0024 12.5C24 5.87 18.63.5 12 .5z"

#define SVG_BOOK \
    "M4 3h7a3 3 0 013 3v15a2 2 0 00-2-2H4V3zm16 0h-5a3 3 0 00-3 3v15a2 2 0 012-2h6V3z"

// The project's links: its repository and its documentation, each only when
// project_config.h names one. A project with more to link adds them through
// PROJECT_FOOTER_LINKS_HTML.
String webShellLinks() {
  String h;
  h.reserve(1600);
  h += F("<div class='links'>");
  if (APP_GH_REPO[0]) {
    h += F("<a href='https://github.com/" APP_GH_REPO "' target='_blank' rel='noopener'>"
           SVG_OPEN SVG_GITHUB SVG_END "GitHub</a>");
  }
  if (APP_DOCS_URL[0]) {
    h += F("<a href='" APP_DOCS_URL "' target='_blank' rel='noopener'>"
           SVG_OPEN SVG_BOOK SVG_END);
    h += T(STR_W_DOCS);
    h += F("</a>");
  }
#ifdef PROJECT_FOOTER_LINKS_HTML
  h += F(PROJECT_FOOTER_LINKS_HTML);
#endif
  h += F("</div>");
  return h;
}

// Which sections the device is not serving, named, plus where the switches
// are. Read from the same table the tab strip reads, so it can never name a
// page the navigation disagrees about.
//
// It says something in both states on purpose. A reader who finds a tab
// missing has no way to tell a missing feature from a shut switch. A reader who
// finds everything open should still know the device is what decides that.
//
// A page left out because it does not apply to this device is not listed: it
// is absent, not switched off, and no switch would bring it back.
static String webShellGateNote() {
  String closed;
  for (size_t i = 0; i < webPageCount(); i++) {
    const WebPage &p = *webPageAt(i);
    if (p.applies && !p.applies()) continue;
    if (webGateOpen(p.gate)) continue;
    if (closed.length()) closed += F(", ");
    closed += webPageLabel(p);
  }

  String n;
  n.reserve(320);
  n += F("<p class='foot'>");
  if (closed.length()) {
    n += closed;
    n += ' ';
    n += T(STR_W_FOOT_GATE_OFF);
  } else {
    n += T(STR_W_FOOT_GATE_ALL);
  }
  n += F(" <b>");
  n += T(STR_W_OFF_PATH);
  n += F("</b>.</p>");
  return n;
}

// The two strings every page's script needs, assigned into the holder that
// /app.js declares. Five pages used to open their script with the same
// `const M={ok:...,err:...}`; a page that needs strings of its own still
// declares them, it just no longer repeats these two.
//
// They cannot live in /app.js: that file is cached across languages, and the
// device serves its pages in whichever one it is set to.
String webShellJsStrings() {
  String s = F("WS.ok=");
  s += jsStr(T(STR_W_SAVED));
  s += F(";WS.err=");
  s += jsStr(T(STR_W_ERROR));
  s += F(";");
  return s;
}

String webShellFoot() {
  String f = webShellLinks();
  f += webShellGateNote();
  f += F("<p class='foot'>");
  f += T(STR_W_DISCLAIMER);
  f += F("</p></div></body></html>");     // closes .wrap
  return f;
}

// Restart overlay. The device is unreachable while it boots, so the page has
// to say so and then find its own way back rather than leaving a dead tab.
//
// How long a boot takes is not a constant: an SD card adds roughly twenty
// seconds, so the page asks the device what it has before deciding what
// counts as too long. Guessing low would report a healthy boot as a failure.
String webShellRestartUi() {
  String h;
  h.reserve(2600);
  h += F("<style>"
         // The page's own ground, nearly opaque: the scrim follows the
         // palette. A browser without color-mix() keeps the solid ground.
         "#rbox{display:none;position:fixed;left:0;right:0;top:var(--vis-top,0px);"
         "bottom:var(--vis-bot,0px);background:var(--ground);"
         "background:color-mix(in srgb,var(--ground) 92%,transparent);z-index:99;"
         "align-items:center;justify-content:center}"
         "#rbox .rc{background:var(--surface);border:1px solid var(--line);border-radius:14px;"
         "padding:32px 40px;text-align:center;max-width:360px}"
         ".spin{width:44px;height:44px;margin:0 auto 18px;border:3px solid var(--line);"
         "border-top-color:var(--accent);border-radius:50%;animation:sp 1s linear infinite}"
         "@keyframes sp{to{transform:rotate(360deg)}}"
         "#rtitle{color:var(--ink);font-size:16px;font-weight:600;margin-bottom:8px}"
         "#rsec{color:var(--ink-3);font-size:13px;line-height:1.5}"
         "</style>"
         "<div id='rbox'><div class='rc'><div class='spin'></div>"
         "<div id='rtitle'>");
  h += T(STR_W_RESTARTING);
  h += F("</div><div id='rsec'>");
  h += T(STR_W_RESTART_WAIT);
  h += F("</div></div></div><script>"
         "const RT={ask:");
  h += jsStr(T(STR_W_RESTART_ASK));
  h += F(",go:");     h += jsStr(T(STR_W_RESTART));
  h += F(",wait:");   h += jsStr(T(STR_W_RESTART_WAIT));
  h += F(",sd:");     h += jsStr(T(STR_W_RESTART_SD));
  h += F(",still:");  h += jsStr(T(STR_W_RESTART_LONG));
  h += F(",gone:");   h += jsStr(T(STR_W_RESTART_GONE));
  h += F(",reload:"); h += jsStr(T(STR_W_RELOAD));
  h += F("};"
         "async function doRestart(){"
         "if(!await ask(RT.ask,RT.go))return;"
         "let sd=false;"
         "try{sd=(await(await fetch('/status.json',{cache:'no-store'})).json()).sd}catch(e){}"
         "try{await fetch('/api/restart',{method:'POST'})}catch(e){}"
         "const box=document.getElementById('rbox'),m=document.getElementById('rsec');"
         "box.style.display='flex';"
         "const expect=sd?35:10,limit=180;let t=0;"
         "const iv=setInterval(async()=>{t++;"
         "if(t<2){m.textContent=RT.wait}"
         "else if(t<expect){m.textContent=RT.wait+' - '+t+'s'+(sd?RT.sd:'')}"
         "else if(t<limit){m.textContent=RT.still+' - '+t+'s'}"
         "else{clearInterval(iv);"
         "m.innerHTML=RT.gone+\" <a href='' style='color:var(--accent)'>\"+RT.reload+'</a>';return}"
         // The old server answers for a moment after the request, so give it a
         // beat before the first poll or we reload against the dying instance.
         "if(t<2)return;"
         "try{const r=await fetch('/status.json',{cache:'no-store'});"
         "if(r.ok){clearInterval(iv);location.reload()}}catch(e){}"
         "},1000);}"
         "</script>");
  return h;
}
