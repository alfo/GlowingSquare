#pragma once

// ============================================================
//  Core configuration.
//
//  Everything that names the product lives in project/project_config.h.
//  This file only supplies defaults for what a project leaves out, and the
//  constants the core itself needs. Nothing in services/, web/ or hardware/
//  may spell the product's name: use the APP_* macros below.
// ============================================================

#include "project/project_config.h"

// ---- Identity (override in project/project_config.h) --------------------

#ifndef FW_VERSION
#error "project/project_config.h must define FW_VERSION as \"vX.Y.Z\" or \"vX.Y.Z-beta.N\""
#endif

// Shown in the web header, the Improv GET_INFO answer and the mDNS TXT record.
#ifndef APP_NAME
#define APP_NAME "ESP32 Device"
#endif

// Lower case, no spaces. The default hostname, the AP name prefix, the NVS
// namespace and the release asset name are all made from it.
#ifndef APP_SLUG
#define APP_SLUG "esp32-device"
#endif

// The web header splits the name in two so the second half can carry the
// accent colour. Leave APP_NAME_ACCENT empty for a plain wordmark.
#ifndef APP_NAME_MAIN
#define APP_NAME_MAIN APP_NAME
#endif
#ifndef APP_NAME_ACCENT
#define APP_NAME_ACCENT ""
#endif

// Prefix of the device id that the DHCP name and the AP SSID are built from.
#ifndef APP_ID_PREFIX
#define APP_ID_PREFIX APP_SLUG "-"
#endif

// The NVS namespace. At most 15 characters. Never change it once devices
// are in the field: every stored setting would be lost.
#ifndef APP_NVS_NAMESPACE
#define APP_NVS_NAMESPACE "app"
#endif

// owner/repo on GitHub that releases are fetched from. Empty disables the
// online update check and the "Update via GitHub" button.
#ifndef APP_GH_REPO
#define APP_GH_REPO ""
#endif

// GitHub Pages root for this project: the web flasher and version.json.
// Empty disables the background check, which reads version.json from here.
#ifndef APP_PAGES_URL
#define APP_PAGES_URL ""
#endif

#ifndef APP_DOCS_URL
#define APP_DOCS_URL ""
#endif

// The web pages' accent colour, one per palette: buttons, links, the
// active tab. The light one has to hold as text on a near-white ground.
#ifndef APP_ACCENT_DARK
#define APP_ACCENT_DARK  "#28d49a"
#endif
#ifndef APP_ACCENT_LIGHT
#define APP_ACCENT_LIGHT "#0a8a60"
#endif

// The mDNS service model, e.g. the board name.
#ifndef APP_MODEL
#ifdef BOARD_NAME
#define APP_MODEL BOARD_NAME
#else
#define APP_MODEL "ESP32"
#endif
#endif

// ---- Build environment ---------------------------------------------------

// The PlatformIO env name. Each env is its own release asset and its own
// entry in version.json, so a firmware built for one board is never offered
// to another. platformio.ini sets it with -DAPP_ENV=\"name\".
#ifndef APP_ENV
#define APP_ENV "default"
#endif

// What the device calls itself to GitHub and to the servers a project talks to.
#ifndef APP_USER_AGENT
#define APP_USER_AGENT APP_NAME "/" FW_VERSION " (ESP32)"
#endif

// The release asset of this env: <slug>-<env>.bin
#define APP_IMAGE_ASSET APP_SLUG "-" APP_ENV ".bin"

#define FLASHER_URL APP_PAGES_URL

// The app slot size of the partition table this env ships with. A device
// whose slots are smaller was flashed with an older table and is told so:
// OTA never rewrites the table, only a flash over USB does.
#ifndef APP_SLOT_BYTES
#define APP_SLOT_BYTES 0x1E0000
#endif
#define PARTITION_APP_SLOT_CURRENT_BYTES APP_SLOT_BYTES

// Whether the public web flasher already carries that table. Set to 0 in a
// release that changes the table, until the flasher has been refreshed.
#ifndef FLASHER_HAS_CURRENT_LAYOUT
#define FLASHER_HAS_CURRENT_LAYOUT 1
#endif

// ---- Display power (boards with a screen) --------------------------------

// Backlight duty, 8 bit. Min is not 0: the screen has to stay usable.
#define BRIGHT_MIN                10
#define BRIGHT_MAX               255
#ifndef BRIGHT_NORMAL_DEFAULT
#define BRIGHT_NORMAL_DEFAULT    255
#endif
#ifndef DIM_TIMEOUT_DEFAULT
#define DIM_TIMEOUT_DEFAULT   300000
#endif
#ifndef SLEEP_TIMEOUT_DEFAULT
#define SLEEP_TIMEOUT_DEFAULT      0   // 0 = never; a headless device has no wake source
#endif
#ifndef OFF_TIMEOUT_DEFAULT
#define OFF_TIMEOUT_DEFAULT        0   // 0 = stage skipped
#endif

// ---- WiFi ------------------------------------------------------------------

// RSSI thresholds for the signal bars on the status page and the header.
#define SIGNAL_BARS_4_DBM  -50
#define SIGNAL_BARS_3_DBM  -65
#define SIGNAL_BARS_2_DBM  -75

// How often the watchdog calls WiFi.begin() again while the link is down.
#define WIFI_RETRY_INTERVAL_MS  30000

// Headless provisioning: the AP portal opens by itself when no SSID is
// stored, after this many boots in a row that never got an address, or when
// the BOOT button is held for APP_PORTAL_HOLD_MS.
#define PORTAL_AFTER_FAILED_BOOTS  3
#ifndef APP_PORTAL_BUTTON_PIN
#define APP_PORTAL_BUTTON_PIN      0     // BOOT on nearly every ESP32 board; -1 = none
#endif
#define APP_PORTAL_HOLD_MS         3000
// A portal opened with WiFi already stored closes again after this long and
// the device retries its network, so a router outage cannot strand it.
#define PORTAL_IDLE_TIMEOUT_MS     (10UL * 60UL * 1000UL)

// ---- Log heartbeat --------------------------------------------------------

#define HEARTBEAT_QUIET_DELTA_B   1024
#define HEARTBEAT_KEEPALIVE_MS   60000
