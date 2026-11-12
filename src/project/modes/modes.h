#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  A display mode
//
//  fetch() runs on the fetch task (core 0) and fills the mode's own
//  "next" data; nothing it touches is read by the loop until it returns.
//  Everything else runs on the loop task:
//
//    enter()     the mode became active, or party mode ended: draw from
//                the data already held (blank if none) and start over
//    commit()    a fetch succeeded: take "next" as current and draw it
//    frame(now)  advance whatever animates (scrolling rows); true when
//                the canvas changed and has to go to the panel
// ============================================================

struct DisplayMode {
  const char *key;          // for logs and the web: "tube"
  const char *mqtt_name;    // v1's MQTT device name: "glowingsquare/tube"
  uint32_t    interval_ms;  // between fetches, as v1
  bool (*fetch)(char *err, size_t err_n);
  void (*commit)();
  void (*enter)();
  bool (*frame)(uint32_t now);
};

extern const DisplayMode MODE_TUBE_OPS;
extern const DisplayMode MODE_FLIGHTS_OPS;
extern const DisplayMode MODE_UNIFI_OPS;

const DisplayMode &modeOps(uint8_t mode);

// The fetch scheduler (gs_fetch.cpp).
// From the loop. True when a fetch's answer was committed: the mode has
// drawn it on the canvas, and the canvas must go to the panel.
bool fetchTick();
void fetchNow();              // the next pass fetches, whatever the interval
bool fetchBusy();             // a fetch is running or waiting to be collected
bool fetchOffline();          // three failures in a row: the red dot
uint32_t fetchLastOkMs();     // millis() of the last success, 0 = never
const char *fetchLastError();
// A "test now" from the Settings page: one fetch of `mode`, whichever mode is
// active, its result as a line of text. Runs on the web job task.
bool fetchTestMode(uint8_t mode, char *out, size_t n);
