#pragma once

#include <WebServer.h>
#include <stdint.h>

// How exposed a route is. Every handler names its level and nothing else
// decides: the gate a route sits behind is a property of the route, not of
// what some screen happens to be showing.
//
// This replaces the old pair of "is the web screen open" and "is maintenance
// on". Those were two independent systems - the pages checked one, the
// endpoints behind them the other - so switching maintenance off hid the
// upload form while /update kept flashing the device.
enum WebGate : uint8_t {
  // Answers whenever the socket is up, even with the master switch off, and
  // from any Host name. For machines: a webhook, a home automation server.
  // A browser's cross-site POST is still refused.
  GATE_ALWAYS = 0,
  // Reads nothing but public state: the status page, its JSON and the icons.
  GATE_OPEN   = 1,
  // Changes how the device behaves: settings, device name, credentials.
  GATE_CONFIG = 2,
  // Writes firmware, deletes logs, restarts, factory reset.
  GATE_MAINT  = 3
};

bool webMasterEnabled();
bool webConfigEnabled();
bool webMaintenanceEnabled();

void webSetMasterEnabled(bool on);
void webSetConfigEnabled(bool on);
void webSetMaintenanceEnabled(bool on);

void webAccessLoad();

// A visit: a device screen that sends the user to a page opens what that
// page needs, master switch included, for as long as the screen is up. Held
// in RAM only, never written to NVS, so leaving the screen or a restart -
// which is how a firmware upload ends - leaves the switches as they were.
// Both return true when they changed a switch; ending a visit that opened
// nothing does nothing.
bool webVisitOpen(WebGate g);
bool webVisitEnd();

bool webGateOpen(WebGate g);

// The lock on the two writing gates, asked for by the browser as HTTP Basic
// auth (any user name). Empty means no password, and the status page says so.
// Set on the device (digits, from its numpad) or on the Network page (any
// printable ASCII).
#define WEB_PASS_MIN  4
#define WEB_PASS_MAX  32
bool webHasPassword();
void webSetPassword(const char *pass);

// The same verdict webRequire() reaches, without answering. For the one
// handler that runs before there is a reply to give: the upload chunk
// callback, which sees the bytes before the completion handler sees anything.
bool webAllowed(WebServer &srv, WebGate g);

// The one line every handler starts with. Answers by itself and returns
// false when the gate is shut, so a caller that forgets to return has still
// not leaked anything - the reply is already sent.
//
// A page request gets the HTML page that says where to switch it on, an
// /api/* request gets a plain 403, told apart by the request path.
bool webRequire(WebServer &srv, WebGate g, const char *what);

void webSendDisabled(WebServer &srv, const char *what, const char *menu);

// Whether a browser has asked the web interface for anything within the last
// ms milliseconds. An open status page polls, so a tab left open counts as
// someone looking. The roaming check in services/wifi_roam.h stays away
// while this is true.
bool webBrowserSeenWithin(uint32_t ms);
