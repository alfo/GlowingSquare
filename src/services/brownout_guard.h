#pragma once

#include <stdint.h>

// ============================================================
//  Brownout guard
//
//  The WiFi radio draws a spike of several hundred milliamps while it
//  calibrates at start-up. On a board whose 3.3V regulator or 5V feed is
//  marginal (a D1 Mini 32 behind a buck converter, say) that dip trips the
//  brownout detector and the board resets, every boot, before the radio has
//  settled; the chip itself would have carried on.
//
//  A project that knows its hardware does this defines APP_BROWNOUT_GUARD_MS
//  in project_config.h: the detector is switched off when the radio starts
//  and back on that long after (or once WiFi is connected). The rest of the
//  time it protects as before, flash writes included. Better still is a
//  470-1000 uF capacitor across 5V at the board; this is for when that is
//  not an option.
// ============================================================

// Before the radio starts. Does nothing unless APP_BROWNOUT_GUARD_MS is set.
void brownoutGuardBegin();
// From the loop: switches the detector back on when the time is up.
void brownoutGuardTick();

// Around a firmware transfer: sustained WiFi plus continuous flash writes
// are the heaviest load the board sees. Safe to ride out: an image only
// becomes active once written whole and verified, so a reset mid-transfer
// leaves the running firmware as it was.
void brownoutGuardHold(bool on);
