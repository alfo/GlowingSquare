// Settings: every setting the project declares in its registry
// (project/project_settings.cpp), one card per section. Nothing here knows
// what a setting means: the rows are built by one renderer from
// GET /api/settings, so a row added to the table shows up in the browser
// without a line being written here.
#include "web/web_pages.h"

#include <Arduino.h>
#include <WebServer.h>

#include "hardware/sd_logger.h"
#include "services/settings_registry.h"
#include "web/web_access.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

static const char* label() { return T(STR_W_NAV_SETTINGS); }

// The page exists only when the project declared something to set.
static bool applies() { return SETTINGS_COUNT > 0; }

static const char* kindName(const SettingDesc &s) {
  switch (s.kind) {
    case SET_BOOL:   return "bool";
    case SET_ENUM:   return "enum";
    case SET_INT:    return "int";
    case SET_TEXT:   return "text";
    case SET_SECRET: return "secret";
    default:         return "submenu";
  }
}

static void rowJson(String &j, const SettingDesc &s) {
  char sub[96];
  settingSubtitle(s, sub, sizeof(sub));

  j += F("{\"id\":\"");    j += s.id;
  j += F("\",\"kind\":\""); j += kindName(s);
  j += F("\",\"name\":\""); j += jsonEsc(T((StringID)s.str_name));
  j += F("\",\"sub\":\"");  j += jsonEsc(sub);
  j += '"';
  if (s.str_info) { j += F(",\"info\":\""); j += jsonEsc(T((StringID)s.str_info)); j += '"'; }
  if (s.flags & SF_REBOOT)    j += F(",\"reboot\":true");
  if (s.flags & SF_MULTILINE) j += F(",\"multi\":true");

  switch (s.kind) {
    case SET_TEXT:
      j += F(",\"v\":\""); j += jsonEsc(settingTextValue(s)); j += '"';
      j += F(",\"max\":"); j += String(s.max - 1);
      if (s.def_text && s.def_text[0]) { j += F(",\"ph\":\""); j += jsonEsc(s.def_text); j += '"'; }
      break;
    case SET_SECRET:
      // Never the value: whether one is stored, nothing more.
      j += F(",\"set\":"); j += settingIsSet(s) ? F("true") : F("false");
      j += F(",\"max\":"); j += String(s.max - 1);
      break;
    case SET_INT:
      j += F(",\"v\":");   j += String(settingGet(s));
      j += F(",\"min\":"); j += String(s.min);
      j += F(",\"max\":"); j += String(s.max);
      if (s.unit) { j += F(",\"unit\":\""); j += jsonEsc(s.unit); j += '"'; }
      break;
    case SET_BOOL:
      j += F(",\"v\":"); j += String(settingGet(s));
      break;
    case SET_ENUM:
      j += F(",\"v\":"); j += String(settingGet(s));
      j += F(",\"opts\":[");
      for (uint8_t v = 0; v < s.opt_count; v++) {
        if (v) j += ',';
        j += '"'; j += jsonEsc(T((StringID)s.opt_str[v])); j += '"';
      }
      j += ']';
      // Which of them cannot be picked right now: offered, but inert.
      if (s.opt_ok) {
        j += F(",\"na\":[");
        bool f2 = true;
        for (uint8_t v = 0; v < s.opt_count; v++) {
          if (s.opt_ok(v)) continue;
          if (!f2) j += ',';
          f2 = false;
          j += String(v);
        }
        j += ']';
      }
      break;
    default:
      break;
  }
  j += '}';
}

// The registry as JSON, filtered to what belongs on screen right now. Built
// by hand rather than with ArduinoJson: it is a flat list of short fields,
// and a document big enough to hold every info text would come off the same
// internal heap the rest of the firmware needs.
//
// [{"id","name","info"?,"rows":[{...}]}]
static String settingsJson() {
  String j;
  j.reserve(3000);
  j += '[';
  bool first_sec = true;
  for (uint8_t sec = 0; sec < SETTING_SECTIONS_COUNT; sec++) {
    if (!settingSectionVisible(sec)) continue;
    const SettingSection &ss = SETTING_SECTIONS[sec];
    String rows;
    bool first = true;
    for (size_t i = 0; i < SETTINGS_COUNT; i++) {
      const SettingDesc &s = SETTINGS[i];
      if (s.section != sec || !settingVisible(s)) continue;
      if (s.flags & SF_DEVICE_ONLY) continue;
      if (!first) rows += ',';
      first = false;
      rowJson(rows, s);
    }
    if (first && !ss.web_extra) continue;   // nothing to show in this section
    if (!first_sec) j += ',';
    first_sec = false;
    j += F("{\"id\":\""); j += ss.id;
    j += F("\",\"name\":\""); j += jsonEsc(T((StringID)ss.str_name)); j += '"';
    if (ss.str_info) { j += F(",\"info\":\""); j += jsonEsc(T((StringID)ss.str_info)); j += '"'; }
    j += F(",\"rows\":["); j += rows; j += F("]}");
  }
  j += ']';
  return j;
}

// The cards are server rendered empty, one per visible section, so a
// section's extra markup (a test button and its script) is in the page from
// the start. The script fills the rows.
static String body() {
  String h;
  h.reserve(5200);
  h += F("<div class='grid'>");
  for (uint8_t sec = 0; sec < SETTING_SECTIONS_COUNT; sec++) {
    if (!settingSectionVisible(sec)) continue;
    const SettingSection &ss = SETTING_SECTIONS[sec];
    h += F("<div class='card wide' id='sec-");
    h += ss.id;
    h += F("'><h2>");
    h += T((StringID)ss.str_name);
    h += F("</h2>");
    if (ss.str_info) {
      h += F("<p class='hint'>");
      h += T((StringID)ss.str_info);
      h += F("</p>");
    }
    h += F("<div class='srows'><p class='hint'>");
    h += T(STR_W_LOADING);
    h += F("</p></div>");
    if (ss.web_extra) h += ss.web_extra();
    h += F("</div>");
  }
  h += F("</div><p class='msg toast' id='os-s' role='status'></p>");

  h += F("<script>");
  h += webShellJsStrings();
  h += F("const M={dev:");
  h += jsStr(T(STR_W_ON_DEVICE));
  h += F(",save:");
  h += jsStr(T(STR_W_SAVE));
  h += F(",set:");
  h += jsStr(T(STR_W_S_SET));
  h += F(",unset:");
  h += jsStr(T(STR_W_S_NOTSET));
  h += F(",clear:");
  h += jsStr(T(STR_W_CLEAR));
  h += F(",reboot:");
  h += jsStr(T(STR_W_TAKES_RESTART));
  h += F(",range:");
  h += jsStr(T(STR_W_RANGE));
  h += F("};"
         // Built with createElement rather than innerHTML: names and help
         // texts are prose that goes through no escaping on the way here.
         "function el(t,c,x){const e=document.createElement(t);if(c)e.className=c;"
         "if(x!==undefined)e.textContent=x;return e;}"
         "function send(d,v,undo){return post('/api/settings',d.id+'='+v).then(r=>{"
         "if(!r.ok){if(undo)undo();flash('os-s',r.text||WS.err,true,5000);return r;}"
         "const m=d.name+': '+(r.text||WS.ok);"
         "flash('os-s',d.reboot?m+' '+M.reboot:m,false,4000);return r;});}"
         // A field and its own Save button, for text, numbers and secrets.
         "function field(d,inp,get){const w=el('div','inrow');w.appendChild(inp);"
         "const b=el('button','quiet',M.save);b.addEventListener('click',()=>{"
         "const v=get();if(v===null){flash('os-s',M.range,true,5000);return;}"
         "send(d,v).then(r=>{if(r.ok)load();});});"
         "inp.addEventListener('keydown',e=>{if(e.key==='Enter'&&!d.multi)b.click();});"
         "w.appendChild(b);return w;}"
         "function ctl(d){"
         "if(d.kind==='bool'){const w=el('label','switch'),c=el('input');c.type='checkbox';"
         "c.checked=!!d.v;c.addEventListener('change',()=>{const want=c.checked;"
         "send(d,want?1:0,()=>{c.checked=!want;}).then(()=>load());});"
         "w.appendChild(c);w.appendChild(el('i'));return w;}"
         "if(d.kind==='enum'){const s=el('select');(d.opts||[]).forEach((o,i)=>{"
         "const op=el('option',null,o);op.value=i;if(d.na&&d.na.indexOf(i)>=0)op.disabled=true;"
         "if(i===d.v)op.selected=true;s.appendChild(op);});"
         "s.addEventListener('change',()=>{send(d,s.value).then(()=>load());});return s;}"
         "if(d.kind==='int'){const i=el('input');i.type='number';i.min=d.min;i.max=d.max;"
         "i.value=d.v;i.style.maxWidth='9em';const f=field(d,i,()=>{const n=Number(i.value);"
         "return Number.isInteger(n)&&n>=d.min&&n<=d.max?n:null;});"
         "if(d.unit)f.insertBefore(el('span','hint',d.unit),f.children[1]);return f;}"
         "if(d.kind==='text'){const i=el(d.multi?'textarea':'input');if(!d.multi)i.type='text';"
         "i.value=d.v;i.maxLength=d.max;i.spellcheck=false;if(d.ph)i.placeholder=d.ph;"
         "return field(d,i,()=>i.value);}"
         "if(d.kind==='secret'){const i=el('input');i.type='password';i.autocomplete='off';"
         "i.maxLength=d.max;i.placeholder=d.set?M.set:M.unset;"
         "const f=field(d,i,()=>i.value);"
         "if(d.set){const c=el('button','quiet',M.clear);c.addEventListener('click',()=>{"
         "send(d,'').then(r=>{if(r.ok)load();});});f.appendChild(c);}"
         "return f;}"
         // A submenu row is the device's business.
         "return el('span','od',M.dev);}"
         "function row(d){const wrap=el('div'),r=el('div','orow'),l=el('div','ol');"
         "l.appendChild(el('span','on',d.name));if(d.sub)l.appendChild(el('span','os',d.sub));"
         "r.appendChild(l);const v=el('div','ov');"
         "const wide=d.kind==='text'||d.kind==='secret'||d.kind==='int';"
         "if(wide){r.className='orow ow';}v.appendChild(ctl(d));r.appendChild(v);wrap.appendChild(r);"
         "if(d.info){const q=el('button','quiet oq','?'),p=el('p','oi',d.info);"
         "q.addEventListener('click',()=>p.classList.toggle('open'));"
         "v.insertBefore(q,v.firstChild);wrap.appendChild(p);}"
         "return wrap;}"
         // A section that stops applying (a mode switch hid it) is reloaded
         // rather than left standing: the page asks again for the whole set.
         "let shown=null;"
         "function load(){getJson('/api/settings').then(d=>{if(!d){flash('os-s',WS.err,true);return;}"
         "const ids=d.map(s=>s.id).join(',');"
         "if(shown!==null&&shown!==ids){location.reload();return;}shown=ids;"
         "d.forEach(s=>{const c=document.querySelector('#sec-'+s.id+' .srows');if(!c)return;"
         "c.textContent='';s.rows.forEach(x=>c.appendChild(row(x)));});});}"
         "load();"
         "</script>");
  return h;
}

static void routes(WebServer &srv) {
  srv.on("/api/settings", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_SETTINGS))) return;
    srv.send(200, "application/json", settingsJson());
  });

  // "<id>=<value>" as text/plain. The value is everything after the first
  // '=', so a text setting may hold one. settingSetFromString() is the only
  // thing that writes, so this runs the same code a device screen would.
  srv.on("/api/settings", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_NAV_SETTINGS))) return;
    const String body = srv.arg("plain");
    const int eq = body.indexOf('=');
    if (eq <= 0) { srv.send(400, "text/plain", T(STR_W_ERROR)); return; }

    String id = body.substring(0, eq);
    id.trim();
    const SettingDesc *s = settingById(id.c_str());
    // Not visible means not settable: a value the device is not offering
    // right now must not be reachable by guessing its id.
    if (!s || !settingVisible(*s) || s->kind == SET_SUBMENU || (s->flags & SF_DEVICE_ONLY)) {
      srv.send(404, "text/plain", T(STR_W_ERROR));
      return;
    }
    const String value = body.substring(eq + 1);
    const SettingResult r = settingSetFromString(*s, value.c_str());
    if (r == SR_RANGE) { srv.send(400, "text/plain; charset=utf-8", T(STR_W_RANGE)); return; }
    if (r != SR_OK && r != SR_UNCHANGED) { srv.send(400, "text/plain", T(STR_W_ERROR)); return; }

    // Secrets are not written to the log, not even their length.
    if (s->kind == SET_SECRET)     logSDf("Web: %s %s", s->id, value.length() ? "set" : "cleared");
    else if (r == SR_OK)           logSDf("Web: %s -> %s", s->id, value.c_str());
    srv.send(200, "text/plain; charset=utf-8", T(STR_W_SAVED));
  });
}

extern const WebPage PAGE_SETTINGS;
const WebPage PAGE_SETTINGS = {
  "/settings", label, GATE_CONFIG, applies,
  body, routes
};
