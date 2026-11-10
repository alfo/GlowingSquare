// ============================================================
//  esp32-iot-template
//
//  The core lives in app/, services/, web/ and hardware/ (and ui/ on boards
//  with a screen). Your project lives in project/: start with
//  project_config.h, project_settings.cpp and project.cpp.
//
//  Derived from SpoolmanScale by Niko11111 (GPL-3.0).
// ============================================================

#include <Arduino.h>

#include "app/app_boot.h"
#include "app/app_loop.h"
#include "hardware/sd_logger.h"

// The loop task runs the web server, the LVGL callbacks and the project. 16 kB
// is what SpoolmanScale's deepest LVGL path needed; a headless project
// rarely needs more than 8.
#ifdef APP_UI_LVGL
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#else
SET_LOOP_TASK_STACK_SIZE(10 * 1024);
#endif

void setup() {
  // Before anything that logs, so the ring holds the whole boot.
  logRingInit();
  appSetup();
}

void loop() {
  appLoop();
}
