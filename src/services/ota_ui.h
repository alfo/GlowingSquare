#pragma once

#include <stdint.h>

// ============================================================
//  OTA feedback on the device itself
//
//  The core calls these while an image is downloaded or uploaded and when
//  the update check finds something. Each has a weak no-op default
//  (ota_ui.cpp): a headless device needs none of them. The LVGL layer
//  overrides them with its overlay and badges (ui/ota_ui_lvgl.cpp), and a
//  project with a display of its own - an LED matrix, an e-ink panel - can
//  override them too, simply by defining a function of the same name.
//
//  All of them run on the loop task. otaUiProgress() is called at most every
//  OTA_PROGRESS_MS while the loop is held by the transfer, so it must draw
//  and return, not wait.
// ============================================================

void otaUiShow();                                  // a transfer starts: cover the screen
void otaUiStatus(const char *text);                // one line of status
void otaUiProgress(uint32_t done, uint32_t total); // bytes so far, total (0 = unknown)
void otaUiHide();                                  // the transfer ended without a restart
void otaUiFailed();                                // ...and failed: say so
void otaUiPump();                                  // let the display draw now
void otaUiBadges(bool update_available);           // the "new version" marks
// True while the device shows the result of a check the user asked for. The
// background check then leaves the result alone instead of arguing with it.
bool otaUiScreenBusy();
