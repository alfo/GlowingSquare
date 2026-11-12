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
// The least stack a fetch has left, in bytes, and the least logged so far.
// A TLS handshake is the deepest it goes; this says how close that came.
static volatile uint32_t s_stack_left = 0;
static uint32_t      s_stack_logged = UINT32_MAX;

void fetchNow() { s_now = true; }
bool fetchBusy() { return s_state != FS_IDLE; }
bool fetchOffline() { return s_fails >= FAILS_FOR_OFFLINE; }
uint32_t fetchLastOkMs() { return s_last_ok_ms; }
const char *fetchLastError() { return s_last_err; }

static void fetchTask(void *) {
  s_err[0] = '\0';
  s_ok = s_running_mode->fetch(s_err, sizeof(s_err));
  s_stack_left = uxTaskGetStackHighWaterMark(NULL);
  __sync_synchronize();
  s_state = FS_DONE;
  vTaskDelete(NULL);
}

// Somebody else holds a TLS connection: two handshakes at once want more
// internal heap than a classic ESP32 has.
static bool networkBusy() {
  return updateCheckBusy() || gh_flash_active || webJobState() == WJS_RUNNING;
}

// True when the answer was committed, and so drawn.
static bool collect() {
  const DisplayMode *m = s_running_mode;
  s_state = FS_IDLE;
  if (s_stack_left < s_stack_logged) {
    s_stack_logged = s_stack_left;
    logSDf("[fetch] stack: %lu of %u bytes left at the deepest", (unsigned long)s_stack_left,
           (unsigned)FETCH_STACK_BYTES);
  }
  // The mode was switched while this ran: its answer is for nobody.
  if (m != &modeOps(g_mode)) return false;
  if (s_ok) {
    if (s_fails >= FAILS_FOR_OFFLINE) logSDf("%s: back online", m->key);
    s_fails = 0;
    s_last_err[0] = '\0';
    s_last_ok_ms = millis() ? millis() : 1;
    m->commit();
    return true;
  }
  snprintf(s_last_err, sizeof(s_last_err), "%s", s_err);
  if (s_fails < 255) s_fails++;
  if (s_fails == 1 || s_fails == FAILS_FOR_OFFLINE) logSDf("%s: fetch failed (%s)", m->key, s_err);
  return false;
}

bool fetchTick() {
  // Failures belong to a mode: switching starts the count again.
  static uint8_t s_counted_mode = 255;
  if (s_counted_mode != g_mode) {
    s_counted_mode = g_mode;
    s_fails = 0;
    s_last_err[0] = '\0';
  }
  const bool drawn = s_state == FS_DONE && collect();
  if (s_state != FS_IDLE) return drawn;
  if (!wifi_ok || g_party != PARTY_OFF || g_brightness_target == 0) return drawn;

  const DisplayMode &m = modeOps(g_mode);
  const bool due = !s_started_once || s_now || millis() - s_last_start_ms >= m.interval_ms;
  if (!due || networkBusy()) return drawn;
  if (!workerHeapReady("[fetch]", workerFloorTls(), FETCH_STACK_BYTES, &s_heap_gate)) return drawn;

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
  return drawn;
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
