#include "web/web_server.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/setup_portal.h"
#include "services/wifi_manager.h"
#include "web/web_access.h"
#include "web/web_assets.h"
#include "web/web_static.h"
#include "web/web_pages.h"
#include "web/web_portal.h"
#include "web/web_shell.h"
#include "app/app_hooks.h"


// How long an accepted socket may stay silent before it is dropped. The
// library waits HTTP_MAX_DATA_WAIT, five seconds, and serves one connection
// at a time - so a socket a browser opened and never used (Chrome opens
// spares on hover and on navigation) parked every other request for those
// five seconds, and with the accept backlog at four the browser saw
// "site not reachable" in between. A real request follows the handshake
// within milliseconds on a LAN; a second is patience enough.
#define WEB_IDLE_CLIENT_MS 1000

// The library's handleClient() with that one number changed. Everything it
// touches is protected and the method is virtual, so this is an override
// rather than a patched copy of the library.
class AppWebServer : public WebServer {
 public:
  using WebServer::WebServer;

  // Nagle off for the accepted sockets: every reply here is small and the
  // browser waits for the last packet of it.
  void tune() { _server.setNoDelay(true); }

  void handleClient() override {
    if (_currentStatus == HC_NONE) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
      _currentClient = _server.accept();
#else
      _currentClient = _server.available();
#endif
      if (!_currentClient) {
        if (_nullDelay) delay(1);
        return;
      }
      _currentStatus = HC_WAIT_READ;
      _statusChange  = millis();
    }

    bool keepCurrentClient = false;
    bool callYield = false;

    if (_currentClient.connected() && _currentStatus == HC_WAIT_READ) {
      if (_currentClient.available()) {
        if (_parseRequest(_currentClient)) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
          // Core 3 counts this timeout in milliseconds, and keeps the
          // response headers of every request in a list that the library's
          // own handleClient() empties. Left out, each request kept its
          // headers for good: about 170 bytes of internal heap per request.
          _currentClient.setTimeout(HTTP_MAX_SEND_WAIT);
          _contentLength = CONTENT_LENGTH_NOT_SET;
          _responseCode  = 0;
          _clearResponseHeaders();
#else
          _currentClient.setTimeout(HTTP_MAX_SEND_WAIT / 1000);
          _contentLength = CONTENT_LENGTH_NOT_SET;
#endif
          const String req_uri = uri();
          const uint32_t t0 = millis();
          _handleRequest();
          const uint32_t took = millis() - t0;
          if (took >= 300) {
            logSDf("[verbose] web: %s from %s took %u ms", req_uri.c_str(),
                   _currentClient.remoteIP().toString().c_str(), (unsigned)took);
          }
        }
      } else {
        // An idle socket keeps its second only while nobody is waiting behind
        // it. Browsers open connections on speculation and leave them empty;
        // one of those at the front of a one-client server held the tab the
        // user actually clicked for up to a second. hasClient() accepts the
        // next connection into the server's own slot, so nothing is lost by
        // asking.
        if (millis() - _statusChange <= WEB_IDLE_CLIENT_MS && !_server.hasClient())
          keepCurrentClient = true;
        callYield = true;
      }
    }

    if (!keepCurrentClient) {
      _currentClient = WiFiClient();
      _currentStatus = HC_NONE;
      _currentUpload.reset();
      _currentRaw.reset();
    }
    if (callYield) yield();
  }
};

static AppWebServer ota_server(80);
static bool ota_server_running  = false;
static bool routes_registered   = false;

// While the WiFi setup portal runs, port 80 serves its form instead. The
// access point is the only network then, the main interface has nothing to
// offer on it, and its host check would refuse every request a phone sends.
// A server object of its own, because routes cannot be taken off one.
static AppWebServer portal_server(80);
static bool portal_server_running    = false;
static bool portal_routes_registered = false;

// The one condition that decides whether port 80 is open.
static bool serverShouldRun() {
  return wifi_ok && webMasterEnabled();
}

static void registerRoutes();

static void serverEnsureRunning() {
  if (ota_server_running) return;
  registerRoutes();
  // The headers webRequire() reads. Authorization is collected by the library
  // on its own; Origin has to be asked for, and asked for before begin().
  static const char *HDR_KEYS[] = { "Origin" };
  ota_server.collectHeaders(HDR_KEYS, 1);
  ota_server.begin();
  ota_server.tune();
  ota_server_running = true;
  Serial.printf("Web server listening: http://%s/\n",
                wifiManagerLocalIP().toString().c_str());
}

static void serverEnsureStopped() {
  if (!ota_server_running) return;
  ota_server.stop();
  ota_server_running = false;
  Serial.println("Web server stopped");
}

static void portalEnsureRunning() {
  if (portal_server_running) return;
  if (!portal_routes_registered) {
    portal_routes_registered = true;
    registerPortalRoutes(portal_server);
  }
  portal_server.begin();
  portal_server.tune();
  portal_server_running = true;
  Serial.printf("Setup portal listening: http://%s/\n", setupPortalIP().toString().c_str());
}

static void portalEnsureStopped() {
  if (!portal_server_running) return;
  portal_server.stop();
  portal_server_running = false;
  Serial.println("Setup portal stopped");
}

// Idempotent, called once a second from appLoop() and once more whenever a
// screen wants the state to settle now. The only owner of the socket: there
// used to be a second automaton in web_access.cpp with its own copy of this
// state, and the copies drifted until port 80 stayed shut until reboot. The
// setup portal is one more state of the same socket, not a second owner:
// each server is stopped before the other one binds the port.
void webServerSyncState() {
  if (setupPortalActive()) {
    serverEnsureStopped();
    portalEnsureRunning();
    return;
  }
  portalEnsureStopped();
  if (serverShouldRun()) serverEnsureRunning();
  else                   serverEnsureStopped();
}

bool webServerIsListening() { return ota_server_running; }

// Registered exactly once. This used to run on every visit to the web screen,
// which appended another handler set to WebServer's list each time and never
// freed the previous ones - WebServer::stop() does not release them.
static void registerRoutes() {
  if (routes_registered) return;
  routes_registered = true;

  registerAssetRoutes(ota_server);
  registerStaticRoutes(ota_server);

  // Every page and every endpoint it owns, straight from the one table. The
  // page route itself is wrapped here so the gate check and the chrome happen
  // in one place and cannot be forgotten in a new page.
  for (size_t i = 0; i < webPageCount(); i++) {
    // Captured as a pointer by value, not as a reference to the loop
    // variable. The descriptor itself lives in ROM either way, but a
    // by-value pointer says so at the capture and leaves nothing to reason
    // about once the loop has ended.
    const WebPage *pg = webPageAt(i);
    ota_server.on(pg->path, HTTP_GET, [pg]() {
      // A page that does not apply to this device is absent, not refused.
      if (pg->applies && !pg->applies()) {
        ota_server.send(404, "text/plain", "Not found");
        return;
      }
      if (!webRequire(ota_server, pg->gate, webPageLabel(*pg))) return;
      String html = webShellHead(webPageLabel(*pg));
      html += webShellHeader();
      html += webShellNav(pg->path);
      html += pg->body();
      html += webShellFoot();   // links and disclaimer close the page
      // No page of the device inside somebody else's frame: a site on the
      // same network could otherwise lay its own buttons over Flash, Erase or
      // Restart and have a visitor click them.
      ota_server.sendHeader("X-Frame-Options", "DENY");
      ota_server.send(200, "text/html", html);
    });
    if (pg->routes) pg->routes(ota_server);
  }


  // Whatever the project serves outside a page of its own: an API for a
  // companion app, a webhook. Every handler starts with webRequire().
  if (APP_HOOKS.registerWebRoutes) APP_HOOKS.registerWebRoutes(ota_server);
}

void handleOtaServerClient() {
  if (ota_server_running)    ota_server.handleClient();
  if (portal_server_running) portal_server.handleClient();
}
