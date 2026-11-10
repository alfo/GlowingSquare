#pragma once

#include <stdint.h>

// ============================================================
//  Headless provisioning
//
//  How a device without a screen gets onto a network, and back onto one
//  when it has lost it. The AP portal (services/setup_portal.h) opens:
//
//   - at boot, when no SSID is stored;
//   - at boot, after PORTAL_AFTER_FAILED_BOOTS boots in a row without an IP;
//   - when the BOOT button is held for APP_PORTAL_HOLD_MS;
//   - when portal_open_pending is set (the web page, MQTT, the project).
//
//  Improv over USB serial works throughout, so the web flasher can hand the
//  device its WiFi straight after flashing and the portal never shows.
//
//  The portal runs as an open network unless the project shows the password
//  itself: define APP_PORTAL_SECURE and read setupPortalSsid() and
//  setupPortalPassword() in APP_HOOKS.onPortal(true). Either way both are
//  printed on the serial console.
//
//  Boards with a screen use ui/wifi_portal_screen.cpp instead, which owns
//  the portal while it is shown; this module then only watches the button.
// ============================================================

// From appSetup(), after loadPrefs(): opens the portal if the stored state
// says the device cannot get online. True when it did.
bool provisioningBoot();

// From appLoop(), every pass: the button, the hand-off of what the portal's
// form sent, the connect attempt that follows, and the idle timeout.
void provisioningTick();

// Open now, for the reason given (logged).
void provisioningOpen(const char *why);

// True from the portal opening until the credentials it took have either
// connected or failed.
bool provisioningActive();
