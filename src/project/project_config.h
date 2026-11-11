#pragma once

// Glowing Square 2: what the core prints, advertises and fetches under our
// name. See app_config.h for every macro and its default.

#define FW_VERSION        "v2.0.0-beta.3"

#define APP_NAME          "Glowing Square"
#define APP_NAME_MAIN     "Glowing"
#define APP_NAME_ACCENT   "Square"
#define APP_SLUG          "glowingsquare"
#define APP_NVS_NAMESPACE "glowsq"          // never change once devices exist

#define APP_GH_REPO       "alfo/GlowingSquare"
#define APP_PAGES_URL     "https://alfo.github.io/GlowingSquare/"
#define APP_DOCS_URL      "https://github.com/alfo/GlowingSquare#readme"

// The Tube orange the departures are drawn in.
#define APP_ACCENT_DARK   "#ff7a1a"
#define APP_ACCENT_LIGHT  "#b84d00"

// London, with summer time.
#define APP_DEFAULT_TZ    "GMT0BST,M3.5.0/1,M10.5.0"

// The panel and the ESP32 share one 5V supply, and the radio's transmit
// spikes sagged it far enough to brown the board out. 15 dBm is plenty
// indoors.
#define APP_WIFI_TX_POWER WIFI_POWER_15dBm

// The device shows the setup WiFi's name and password on the matrix itself,
// so the network does not have to be open.
#define APP_PORTAL_SECURE
