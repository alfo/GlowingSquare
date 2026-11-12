#pragma once

// Reads the core's own state from NVS (WiFi, language, setup flags, update
// check, display power) and then every setting the project declared.
void loadPrefs();
void saveWifiCredentials(const char* ssid, const char* pass);
void forgetWifiCredentials();

// Counts boots in a row that never got an IP address. After
// PORTAL_AFTER_FAILED_BOOTS of them a headless device opens its AP portal.
void noteBootFailed(bool failed);
