// When the active mode fetches, on a task of its own: the loop keeps
// scrolling, answering the web interface and talking MQTT while a TLS
// handshake takes its second or two.
//
// As v1: no fetch while party mode runs or while the display is dark, and
// the red "offline" dot only after three failures in a row.
#include <Arduino.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "project/gs_state.h"
#include "project/modes/modes.h"
#include "services/ota_state.h"
#include "services/update_check.h"
#include "services/worker_heap.h"
#include "web/web_jobs.h"

#define FETCH_STACK_BYTES   12288
#define FETCH_PRIORITY      1
#define FETCH_CORE          0
#define FAILS_FOR_OFFLINE   3

enum FetchState : uint8_t { FS_IDLE, FS_RUNNING, FS_DONE };

static volatile FetchState s_state = FS_IDLE;
static const DisplayMode *s_running_mode = nullptr;
static bool          s_ok = false;
static char          s_err[96] = "";
static char          s_last_err[96] = "";
static uint32_t      s_last_start_ms = 0;
static bool          s_started_once = false;
static bool          s_now = false;
static uint8_t       s_fails = 0;
static uint32_t      s_last_ok_ms = 0;
static LogGate       s_heap_gate{};

void fetchNow() { s_now = true; }
bool fetchBusy() { return s_state != FS_IDLE; }
bool fetchOffline() { return s_fails >= FAILS_FOR_OFFLINE; }
uint32_t fetchLastOkMs() { return s_last_ok_ms; }
const char *fetchLastError() { return s_last_err; }

static void fetchTask(void *) {
  s_err[0] = '\0';
  s_ok = s_running_mode->fetch(s_err, sizeof(s_err));
  __sync_synchronize();
  s_state = FS_DONE;
  vTaskDelete(NULL);
}

// Somebody else holds a TLS connection: two handshakes at once want more
// internal heap than a classic ESP32 has.
static bool networkBusy() {
  return updateCheckBusy() || gh_flash_active || webJobState() == WJS_RUNNING;
}

static void collect() {
  const DisplayMode *m = s_running_mode;
  s_state = FS_IDLE;
  // The mode was switched while this ran: its answer is for nobody.
  if (m != &modeOps(g_mode)) return;
  if (s_ok) {
    if (s_fails >= FAILS_FOR_OFFLINE) logSDf("%s: back online", m->key);
    s_fails = 0;
    s_last_err[0] = '\0';
    s_last_ok_ms = millis() ? millis() : 1;
    m->commit();
    return;
  }
  snprintf(s_last_err, sizeof(s_last_err), "%s", s_err);
  if (s_fails < 255) s_fails++;
  if (s_fails == 1 || s_fails == FAILS_FOR_OFFLINE) logSDf("%s: fetch failed (%s)", m->key, s_err);
}

void fetchTick() {
  // Failures belong to a mode: switching starts the count again.
  static uint8_t s_counted_mode = 255;
  if (s_counted_mode != g_mode) {
    s_counted_mode = g_mode;
    s_fails = 0;
    s_last_err[0] = '\0';
  }
  if (s_state == FS_DONE) collect();
  if (s_state != FS_IDLE) return;
  if (!wifi_ok || g_party != PARTY_OFF || g_brightness_target == 0) return;

  const DisplayMode &m = modeOps(g_mode);
  const bool due = !s_started_once || s_now || millis() - s_last_start_ms >= m.interval_ms;
  if (!due || networkBusy()) return;
  if (!workerHeapReady("[fetch]", workerFloorTls(), FETCH_STACK_BYTES, &s_heap_gate)) return;

  s_now = false;
  s_started_once = true;
  s_last_start_ms = millis();
  s_running_mode = &m;
  s_state = FS_RUNNING;
  if (xTaskCreatePinnedToCore(fetchTask, "fetch", FETCH_STACK_BYTES, nullptr,
                              FETCH_PRIORITY, nullptr, FETCH_CORE) != pdPASS) {
    s_state = FS_IDLE;
    logSD("[fetch] task creation failed");
  }
}

// From the web job task: the same fetch, but nothing is committed. Only one
// mode's fetch runs at a time, and the scheduler stays away while the web
// job runs (networkBusy()), so the two never share a mode's "next" buffer.
bool fetchTestMode(uint8_t mode, char *out, size_t n) {
  char err[96] = "";
  const bool ok = modeOps(mode).fetch(err, sizeof(err));
  snprintf(out, n, "%s", ok ? "OK" : err);
  return ok;
}
