#include "project/gs_web_extra.h"

#include <Arduino.h>

#include "web/web_shell.h"
#include "lang.h"

// One button and its message line; the script is shared (gs_web.cpp serves
// /api/gs/test). Built once per page view, so a static buffer is enough.
static const char *testButton(const char *mode) {
  static String s;
  s = F("<div class='inrow' style='margin-top:14px'><button class='quiet' data-gstest='");
  s += mode;
  s += F("'>");
  s += T(STR_P_TEST);
  s += F("</button><span class='msg' id='gst-");
  s += mode;
  s += F("'></span></div><script>");
  s += F("if(!window.GST){window.GST=1;var GT={wait:");
  s += jsStr(T(STR_P_TESTING));
  s += F("};"
         "function gsPoll(m,n){getJson('/api/gs/test?m='+m).then(function(d){"
         "var e='gst-'+m;if(!d){flash(e,WS.err,true);return;}"
         "if(d.busy&&n<40){setTimeout(function(){gsPoll(m,n+1);},1000);return;}"
         "flash(e,d.text||WS.err,!d.ok);});}"
         "document.addEventListener('click',function(ev){var b=ev.target.closest('[data-gstest]');"
         "if(!b)return;var m=b.getAttribute('data-gstest');flash('gst-'+m,GT.wait,false);"
         "post('/api/gs/test',m).then(function(r){if(!r.ok){flash('gst-'+m,r.text||WS.err,true);return;}"
         "setTimeout(function(){gsPoll(m,0);},1500);});});}"
         "</script>");
  return s.c_str();
}

const char *gsWebExtraTube()  { return testButton("tube"); }
const char *gsWebExtraUnifi() { return testButton("unifi"); }

// v1 took a box, "lat1,lat2,lon1,lon2". The centre is its middle; the radius
// reaches its nearest edge, so nothing outside the old box shows up.
// The browser's position as the centre, asked once (locateOnce() in app.js).
// Four decimals is about 11 m: plenty for a radius in nautical miles, and no
// more of the house's position than the display needs.
static void locateButton(String &s) {
  s += F("<p class='hint' style='margin-top:14px'>");
  s += T(STR_P_FL_LOCATE);
  s += F("</p><div class='inrow'><button class='quiet' id='flloc'>");
  s += T(STR_P_FL_LOCATE_BTN);
  s += F("</button><span class='msg' id='flloc-s'></span></div><script>(function(){var L={wait:");
  s += jsStr(T(STR_P_FL_LOCATE_WAIT));
  s += F(",denied:");
  s += jsStr(T(STR_P_FL_LOCATE_DENIED));
  s += F(",blocked:");
  s += jsStr(T(STR_P_FL_LOCATE_BLOCKED));
  s += F(",none:");
  s += jsStr(T(STR_P_FL_LOCATE_NONE));
  s += F("};"
         "$('flloc').addEventListener('click',function(){flash('flloc-s',L.wait,false);"
         "locateOnce().then(function(p){"
         "if(p.err==='closed'){flash('flloc-s','',false);return;}"
         "if(p.err){flash('flloc-s',L[p.err]||L.none,true,8000);return;}"
         "Promise.all([post('/api/settings','fl_lat='+p.lat.toFixed(4)),"
         "post('/api/settings','fl_lon='+p.lon.toFixed(4))]).then(function(rs){"
         "var ok=rs.every(function(x){return x.ok;});"
         "flash('flloc-s',ok?WS.ok:WS.err,!ok,5000);if(ok)load();});});});})();"
         "</script>");
}

const char *gsWebExtraFlights() {
  static String s;
  s = "";
  locateButton(s);
  s += F("<p class='hint' style='margin-top:14px'>");
  s += T(STR_P_FL_CONVERT);
  s += F("</p><div class='inrow'><input id='flbox' type='text' placeholder='51.5672,51.4131,-0.4146,0.1107'>"
         "<button class='quiet' id='flconv'>");
  s += T(STR_P_FL_CONVERT_BTN);
  s += F("</button></div><span class='msg' id='flconv-s'></span><script>"
         "$('flconv').addEventListener('click',function(){"
         "var p=$('flbox').value.split(',').map(Number);"
         "if(p.length!==4||p.some(isNaN)){flash('flconv-s',WS.err,true,4000);return;}"
         "var la=(p[0]+p[1])/2,lo=(p[2]+p[3])/2;"
         "var dy=Math.abs(p[0]-p[1])*60/2,dx=Math.abs(p[2]-p[3])*60*Math.cos(la*Math.PI/180)/2;"
         "var r=Math.max(1,Math.round(Math.min(dx,dy)));"
         "Promise.all([post('/api/settings','fl_lat='+la.toFixed(4)),"
         "post('/api/settings','fl_lon='+lo.toFixed(4)),"
         "post('/api/settings','fl_radius='+r)]).then(function(rs){"
         "var ok=rs.every(function(x){return x.ok;});"
         "flash('flconv-s',ok?WS.ok:WS.err,!ok,5000);if(ok)load();});});"
         "</script>");
  s += testButton("flights");
  return s.c_str();
}
