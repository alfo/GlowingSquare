#include "services/ota_ui.h"

// Weak defaults: nothing to show on a device without a screen. A strong
// definition anywhere in the build replaces each one on its own.
__attribute__((weak)) void otaUiShow() {}
__attribute__((weak)) void otaUiStatus(const char *) {}
__attribute__((weak)) void otaUiProgress(uint32_t, uint32_t) {}
__attribute__((weak)) void otaUiHide() {}
__attribute__((weak)) void otaUiFailed() {}
__attribute__((weak)) void otaUiPump() {}
__attribute__((weak)) void otaUiBadges(bool) {}
__attribute__((weak)) bool otaUiScreenBusy() { return false; }
