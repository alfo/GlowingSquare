// Network: the device name and mDNS, the WiFi it is on, the time zone, the
// web password and the look of these pages. The core's own settings; the
// project's live on the Settings page, which the registry renders.
//
// The device name takes a whole name, "lamp" or "lamp.home.arpa": ".local"
// is what mDNS answers to, not the only address a network can have.
#include "web/web_pages.h"

#include <Arduino.h>
#include <WebServer.h>

#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "app_config.h"
#include "hardware/sd_logger.h"
#include "services/device_name.h"
#include "services/mdns_service.h"
#include "services/prefs_store.h"
#include "services/time_service.h"
#include "services/wifi_manager.h"
#include "web/web_access.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

static const char* label() { return T(STR_W_NAV_NETWORK); }


// The other addresses that reach the same device, in the order someone would
// try them. Both are listed because neither always works: a .local name is
// lost on some Android builds, and a DNS name only exists where the network
// serves it.
static String altAddresses() {
  String a;
  if (deviceDomain()[0] && mdnsRunning()) {
    char m[DEVICE_FQDN_MAX + 1];
    deviceMdnsName(m, sizeof(m));
    a = m;
  }
  if (wifi_ok) {
    if (a.length()) a += " · ";
    a += wifiManagerLocalIP().toString();
  }
  return a;
}

// The verdict under the field, already translated and already filled in.
// Building it here rather than in the page keeps every user facing word on
// the T() side, the way the rest of the firmware does it.
static String dnsVerdict(bool &bad, bool &waiting) {
  bad = waiting = false;
  if (!deviceDomain()[0]) return String();
  switch (deviceDnsState()) {
    // UNKNOWN with a domain set means the verdict is still being worked out -
    // either the tick has not started the lookup yet or the answer is on its
    // way. Both read the same to someone waiting for it.
    case DEV_DNS_UNKNOWN:
    case DEV_DNS_CHECKING:
      waiting = true;
      return String(T(STR_W_DEVNAME_DNS_WAIT));
    case DEV_DNS_MATCH:
      return String(T(STR_W_DEVNAME_DNS_OK));
    case DEV_DNS_OTHER: {
      bad = true;
      char b[160];
      snprintf(b, sizeof(b), T(STR_W_DEVNAME_DNS_OTHER),
               deviceDnsIP().toString().c_str());
      return String(b);
    }
    case DEV_DNS_FAIL:
      bad = true;
      return String(T(STR_W_DEVNAME_DNS_NONE));
    default:
      return String();
  }
}

// One shape for the card, whether it is rendered into the page or fetched
// after a save. Two builders would drift.
static String nameStateJson() {
  bool bad = false, waiting = false;
  const String verdict = dnsVerdict(bad, waiting);
  String j = "{\"name\":\"" + jsonEsc(deviceFqdn()) + "\"";
  j += ",\"mdns\":"    + String(deviceMdnsEnabled() ? "true" : "false");
  j += ",\"mdnsRun\":" + String(mdnsRunning() ? "true" : "false");
  j += ",\"also\":\""   + jsonEsc(altAddresses().c_str()) + "\"";
  j += ",\"dnsMsg\":\"" + jsonEsc(verdict.c_str()) + "\"";
  j += ",\"dnsBad\":"  + String(bad ? "true" : "false");
  j += ",\"dnsWait\":" + String(waiting ? "true" : "false");
  j += "}";
  return j;
}

static String body() {
  String h;
  h.reserve(7400);

  h += F("<div class='grid'><div class='card wide'><h2>");
  h += T(STR_W_C_DEVNAME);
  h += F("</h2><div class='field'><div class='inrow'>"
         "<input id='hn' type='text' maxlength='");
  h += String(DEVICE_FQDN_MAX);
  h += F("' spellcheck='false' placeholder='" APP_SLUG ".home.arpa' value='");
  h += deviceFqdn();
  h += F("'><button id='hn-b'>");
  h += T(STR_W_SAVE);
  // One message line, not two. It carries the DNS verdict, and a save
  // overwrites it for a moment before the verdict comes back - the two are
  // never needed at the same time, and an empty .msg reserves a line whether
  // it says anything or not.
  //
  // Both lines are filled in here as well as by paint(). Everything this card
  // shows is known at render time, and a page that arrives blank and then
  // fills itself in reads as a page that is broken for a moment.
  bool bad = false, waiting = false;
  const String verdict = dnsVerdict(bad, waiting);
  const String also    = altAddresses();

  h += F("</button></div><span class='msg");
  if (bad) h += F(" bad");
  h += F("' id='hn-s'>");
  h += verdict;
  h += F("</span><span class='hint' id='hn-a'>");
  if (also.length()) { h += T(STR_W_DEVNAME_ALSO); h += ": "; h += also; }
  h += F("</span><span class='hint'>");
  h += T(STR_W_DEVNAME_HINT);
  h += F("</span></div>");

  // Second field rather than a second card: it is the same subject, and the
  // switch only makes sense next to the name it applies to.
  //
  // Rendered with its state here rather than filled in by the script: a
  // control that arrives blank and then jumps is worse than one that is a
  // frame late. The shape is .switch/.check from the stylesheet now - it used
  // to be an inline style copied between this page and page_tags.cpp.
  h += F("<div class='field'>"
         "<label class='check'><span class='switch'>"
         "<input id='md' type='checkbox'");
  if (deviceMdnsEnabled()) h += F(" checked");
  h += F("><i></i></span>");
  h += T(STR_W_MDNS);
  h += F("</label><span class='hint'>");
  h += T(STR_W_MDNS_HINT);
  h += F("</span><span class='msg' id='md-s'></span></div></div>");

  // The zone the device writes every timestamp in, the log included. It belongs here rather than next to the log: it
  // changes behaviour, it is not a property of one page.
  h += F("<div class='card'><h2>");
  h += T(STR_TZ_TITLE);
  // The index, not the POSIX string. One of those is "<-03>3", and a value
  // carrying angle brackets through an HTML attribute is a fight not worth
  // having when the server has the table anyway.
  h += F("</h2><div class='field'>"
         "<select id='tz'>");
  {
    const int cur = timeZoneIndex();
    if (cur < 0) {
      // A zone that is not in the table can only come from a hand written
      // preference. Shown so it is not silently replaced by the first entry,
      // and inert so it cannot be picked again.
      h += F("<option value='-1' selected disabled>");
      h += timeZoneName();
      h += F("</option>");
    }
    for (size_t i = 0; i < TZ_COUNT; i++) {
      h += F("<option value='");
      h += String((unsigned)i);
      h += F("'");
      if (cur == (int)i) h += F(" selected");
      h += F(">");
      h += TZ_LIST[i].name;
      h += F(" (");
      h += TZ_LIST[i].offset;
      h += F(")</option>");
    }
  }
  h += F("</select><span class='hint'>");
  h += T(STR_W_TZ_NOTE);
  h += F("</span></div><span class='msg' id='tz-s'></span></div>");

  // ---- WiFi -------------------------------------------------------------
  // Moving the device to another network goes through the setup portal, the
  // same way it got its first one: a new password typed here would cut this
  // very page off before it could say whether it worked.
  h += F("<div class='card'><h2>");
  h += T(STR_W_C_WIFI);
  h += F("</h2><div class='rows'><div class='row'><span class='k'>");
  h += T(STR_W_R_WIFI);
  h += F("</span><span class='v'>");
  h += htmlEsc(cfg_wifi_ssid);
  h += F("</span></div></div><div class='field'><button class='quiet' id='wf-b'>");
  h += T(STR_W_WIFI_CHANGE);
  h += F("</button><span class='hint'>");
  h += T(STR_W_WIFI_CHANGE_HINT);
  h += F("</span><span class='msg' id='wf-s'></span></div></div>");

  // ---- password -----------------------------------------------------------
  h += F("<div class='card'><h2>");
  h += T(STR_W_R_PASSWORD);
  h += F("</h2><div class='field'><div class='inrow'>"
         "<input id='pw' type='password' autocomplete='new-password' maxlength='");
  h += String(WEB_PASS_MAX);
  h += F("' placeholder='");
  h += webHasPassword() ? T(STR_W_S_SET) : T(STR_W_S_NOTSET);
  h += F("'><button id='pw-b'>");
  h += T(STR_W_SAVE);
  h += F("</button></div><span class='hint'>");
  h += T(STR_W_PASS_HINT);
  h += F("</span><span class='msg' id='pw-s'></span></div></div>");

  // ---- look ---------------------------------------------------------------
  h += F("<div class='card'><h2>");
  h += T(STR_W_C_LOOK);
  h += F("</h2><div class='field'><select id='th'>");
  {
    const StringID names[] = { STR_W_THEME_AUTO, STR_W_THEME_DARK, STR_W_THEME_LIGHT };
    for (uint8_t i = 0; i <= WEB_THEME_LIGHT; i++) {
      h += F("<option value='");
      h += String(i);
      h += F("'");
      if (webThemeGet() == i) h += F(" selected");
      h += F(">");
      h += T(names[i]);
      h += F("</option>");
    }
  }
  h += F("</select><span class='msg' id='th-s'></span></div></div></div>");

  // Every handler is bound here rather than written into an onclick attribute:
  // a page body is JavaScript inside a C++ string literal, and an attribute is
  // the one place where the two levels of quoting collide.
  // $, flash, post and postFlash come from /app.js.
  h += F("<script>");
  h += webShellJsStrings();
  h += F("const M={also:");
  h += jsStr(T(STR_W_DEVNAME_ALSO));
  h += F(",wifiq:");
  h += jsStr(T(STR_W_PORTAL_Q));
  h += F(",th:['auto','dark','light']};"
         "let poll=0;"
         // Never while the field has focus. paint() runs from a poll every
         // two seconds during a DNS check, and it would rewrite the name out
         // from under someone in the middle of typing the next one.
         "function paint(d){"
         "if(document.activeElement!==$('hn'))$('hn').value=d.name;"
         "$('hn-s').textContent=d.dnsMsg;"
         "$('hn-s').className='msg'+(d.dnsBad?' bad':'');"
         "$('hn-a').textContent=d.also?M.also+': '+d.also:'';"
         "$('md').checked=d.mdns;"
         "if(d.dnsWait&&poll<10){poll++;setTimeout(load,2000);}else if(!d.dnsWait){poll=0;}}"
         "function load(){getJson('/api/hostname').then(d=>{if(d)paint(d);});}"
         "$('hn-b').addEventListener('click',()=>{"
         "post('/api/hostname',$('hn').value).then(r=>{poll=0;"
         "flash('hn-s',r.text||WS.err,!r.ok,4000,r.ok?load:null);});});"
         // The box already shows what the user asked for, so a failure has to
         // put it back.
         "$('md').addEventListener('change',()=>{"
         "const want=$('md').checked;"
         "post('/api/mdns',want?'1':'0').then(r=>{"
         "if(!r.ok||!r.json){$('md').checked=!want;"
         "flash('md-s',WS.err,true,4000);return;}"
         "paint(r.json);flash('md-s',WS.ok,false,4000);});});"
         "$('tz').addEventListener('change',()=>{"
         "postFlash('/api/timezone',$('tz').value,'tz-s',4000);});"
         "$('wf-b').addEventListener('click',()=>{"
         "ask(M.wifiq).then(y=>{if(y)postFlash('/api/portal','1','wf-s');});});"
         "$('pw-b').addEventListener('click',()=>{"
         "post('/api/password',$('pw').value).then(r=>{$('pw').value='';"
         "flash('pw-s',r.text||WS.err,!r.ok,6000);});});"
         // Applied to this page at once, the stored choice for every other.
         "$('th').addEventListener('change',()=>{const v=$('th').value;"
         "post('/api/webtheme',v).then(r=>{if(r.ok)SCH.set(M.th[+v]);"
         "flash('th-s',r.ok?WS.ok:WS.err,!r.ok,4000);});});"
         "load();"
         "</script>");
  return h;
}

static void routes(WebServer &srv) {
  srv.on("/api/hostname", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_SETTINGS))) return;
    srv.send(200, "application/json", nameStateJson());
  });

  srv.on("/api/hostname", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_SETTINGS))) return;
    String name = srv.arg("plain");
    name.trim();
    if (!deviceSetName(name.c_str())) {
      // Named rather than a bare "invalid": the rule is not obvious and the
      // field has just been typed.
      srv.send(400, "text/plain", T(STR_W_DEVNAME_BAD));
      return;
    }
    logSDf("Web: device name set to %s", deviceFqdn());
    // The responder restarts on the next sync pass, so the URL named here is
    // the one that will answer a second from now rather than the one that
    // does this instant. The message also carries the note about the router,
    // which is the one moment it is worth reading - it used to sit under the
    // field for good.
    char url[DEVICE_FQDN_MAX + 16];
    snprintf(url, sizeof(url), "http://%s/", deviceFqdn());
    char msg[DEVICE_FQDN_MAX + 160];
    snprintf(msg, sizeof(msg), T(STR_W_DEVNAME_NOW), url);
    srv.send(200, "text/plain", msg);
  });

  // Takes the state it should be in, not a toggle. /api/verbose on the log
  // page toggles because a button has no state of its own, but this is a
  // checkbox: a request that arrives twice would flip twice and leave the box
  // disagreeing with the device. The answer carries the whole card back, so
  // the page never has to work out what it now looks like.
  srv.on("/api/mdns", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_SETTINGS))) return;
    String want = srv.arg("plain");
    want.trim();
    deviceSetMdnsEnabled(want == "1");
    // Ask the one owner of the responder to act on it now, so the addresses
    // in the reply already tell the truth.
    mdnsSyncState();
    deviceNameTick();
    logSDf("Web: mDNS -> %s", deviceMdnsEnabled() ? "on" : "off");
    srv.send(200, "application/json", nameStateJson());
  });

  // Applied at once: timeZoneSet() hands the string to the C library, so the
  // next line written to the log is already in the new zone. No restart.
  srv.on("/api/timezone", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, "timezone")) return;
    if (!srv.hasArg("plain")) { srv.send(400, "text/plain", "no body"); return; }
    // An index into the table, so nothing that reaches setenv() comes from
    // the request. A malformed TZ string would silently mean UTC, which reads
    // as the setting having been lost.
    const long idx = strtol(srv.arg("plain").c_str(), nullptr, 10);
    if (idx < 0 || idx >= (long)TZ_COUNT) {
      srv.send(400, "text/plain", "unknown zone");
      return;
    }
    timeZoneSet(TZ_LIST[idx].tz);
    logSDf("Time zone set to %s (%s)", TZ_LIST[idx].name, TZ_LIST[idx].tz);
    srv.send(200, "text/plain", TZ_LIST[idx].name);
  });

  // The lock on the writing gates. Behind GATE_CONFIG, so whoever sets it
  // already got past the old one. An empty body removes it.
  srv.on("/api/password", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, "password")) return;
    const String v = srv.arg("plain");
    if (v.length() && (v.length() < WEB_PASS_MIN || v.length() > WEB_PASS_MAX)) {
      srv.send(400, "text/plain; charset=utf-8", T(STR_W_PASS_LEN));
      return;
    }
    webSetPassword(v.c_str());
    srv.send(200, "text/plain; charset=utf-8",
             webHasPassword() ? T(STR_W_PASS_SET) : T(STR_W_PASS_REMOVED));
  });

  srv.on("/api/webtheme", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, "theme")) return;
    const long v = strtol(srv.arg("plain").c_str(), nullptr, 10);
    if (v < 0 || !webThemeSet((uint8_t)v)) { srv.send(400, "text/plain", "bad theme"); return; }
    srv.send(200, "text/plain", webThemeKey((uint8_t)v));
  });
}

extern const WebPage PAGE_CONFIG;
const WebPage PAGE_CONFIG = {
  "/network", label,  GATE_CONFIG, nullptr,
  body, routes
};
