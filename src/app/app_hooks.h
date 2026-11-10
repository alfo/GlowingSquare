#pragma once

// ============================================================
//  The project's way into the core
//
//  The core (services/, web/, hardware/, app/) never names the project.
//  It calls the project through this one table, which the project defines
//  in project/project.cpp. Every member may be null.
//
//  When each runs:
//
//    setup()            once, after NVS, logging and (with a screen) the
//                       display are up, before WiFi connects. Start your
//                       hardware here.
//    loop()             every pass of the loop task. Never block: no
//                       delay() longer than a frame, no HTTP. Network work
//                       goes on a worker task (see web/web_jobs.h for the
//                       pattern) and comes back through a flag.
//    onWifiUp()         each time the station gets an IP address.
//    onWifiDown()       each time it loses it.
//    onPortal(on)       the setup AP opened (true) or closed (false). A
//                       headless project shows the AP name and password
//                       on whatever display it has (setupPortalSsid()...).
//    registerWebRoutes  once, with the server, for routes outside a page.
//                       Every handler starts with webRequire().
//    statusCard()       an HTML card for the Status page. Elements with
//                       data-k="<key>" are refreshed from statusJson().
//    statusJson(j)      appends "key":value pairs (no braces, no leading
//                       comma on the first) to /status.json's "app" object.
//    statusLine(b, n)   one line for the boot block in the log.
//    onMdnsService()    after the core's mDNS TXT records, to add more.
//    buildMainScreen(p) (LVGL builds) builds the home screen into p, an
//                       lv_obj_t* under the header.
//    openScreen(id)     (LVGL builds) a SUBMENU row asked for screen id.
// ============================================================

#include <Arduino.h>
#include <WebServer.h>
#include <stddef.h>

struct WebPage;

struct AppHooks {
  void (*setup)();
  void (*loop)();
  void (*onWifiUp)();
  void (*onWifiDown)();
  void (*onPortal)(bool open);
  void (*registerWebRoutes)(WebServer &srv);
  String (*statusCard)();
  void (*statusJson)(String &j);
  void (*statusLine)(char *buf, size_t n);
  void (*onMdnsService)();
  void (*buildMainScreen)(void *parent);   // an lv_obj_t*: the area under the header
  void (*openScreen)(uint8_t id);
};

extern const AppHooks APP_HOOKS;

// The project's own web pages, shown between Status and Settings.
extern const WebPage* const PROJECT_WEB_PAGES[];
extern const size_t         PROJECT_WEB_PAGE_COUNT;
