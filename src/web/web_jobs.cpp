#include "web_jobs.h"

#include <ArduinoJson.h>
#include <string.h>

#include "app/app_state.h"
#include "services/github_release.h"
#include "services/worker_heap.h"
#include "web/web_shell.h"

// The same numbers the update check task runs with, for the same reasons: a
// TLS handshake with the certificate bundle wants the stack, the task sits
// below the loop so the display keeps its share, and it lives on the other
// core. The heap floor (services/worker_heap.h) is checked before the stack
// is taken from it.
#define WEB_JOB_STACK_BYTES   16384
#define WEB_JOB_PRIORITY      1
#define WEB_JOB_CORE          0
// A result nobody collects is dropped after this, so the slot is free again
// for the next visitor.
#define WEB_JOB_KEEP_MS       60000UL

static volatile WebJobState s_state = WJS_IDLE;
static WebJobResult         s_res;
static char                 s_arg[160] = "";   // a release tag, or the project job's argument
static bool                 s_flag    = false;
static unsigned long        s_done_ms = 0;
static WebJobFn             s_fn      = nullptr;
static uint8_t              s_fn_id   = 0;
static uint32_t             s_floor   = 0;

static void runGhCheck() {
  s_res.image_size = 0;
  s_res.ok = githubLatestTag(s_flag, s_res.tag, sizeof(s_res.tag),
                             s_res.pub, sizeof(s_res.pub),
                             s_res.err, sizeof(s_res.err), &s_res.image_size);
}

static void runGhNotes() {
  GithubRelease rel;
  if (!githubReleaseByTag(s_arg, rel, s_res.err, sizeof(s_res.err))) {
    s_res.ok = false;
    return;
  }
  s_res.body = "{\"ok\":true,\"tag\":\"" + jsonEsc(rel.tag) +
               "\",\"name\":\"" + jsonEsc(rel.name) +
               "\",\"published\":\"" + jsonEsc(rel.published) +
               "\",\"prerelease\":" + (rel.prerelease ? "true" : "false") +
               ",\"notes\":\"" + jsonEsc(rel.notes.c_str()) + "\"}";
  strncpy(s_res.tag, rel.tag, sizeof(s_res.tag) - 1);
  s_res.tag[sizeof(s_res.tag) - 1] = '\0';
  s_res.ok = true;
}

static void webJobTask(void* arg) {
  (void)arg;
  switch (s_res.kind) {
    case WJ_GH_CHECK:  runGhCheck();  break;
    case WJ_GH_NOTES:  runGhNotes();  break;
    case WJ_PROJECT:   if (s_fn) s_fn(s_res, s_arg); break;
    default: break;
  }
  Serial.printf("[webjob] kind %d done, ok=%d code=%d, stack left %u\n",
                (int)s_res.kind, (int)s_res.ok, s_res.code,
                (unsigned)uxTaskGetStackHighWaterMark(NULL));
  s_done_ms = millis() ? millis() : 1;
  // Everything above is written before the state says so: the handler on
  // the other core reads the state first and the result after.
  __sync_synchronize();
  s_state = WJS_DONE;
  vTaskDelete(NULL);
}

// A page polls while it waits, so a refused start repeats on every request.
static LogGate s_refusal_gate{};

// The floor follows the server a job talks to: GitHub for the release jobs,
// whatever the project said for its own.
static uint32_t webJobFloor(WebJobKind kind) {
  return kind == WJ_PROJECT ? s_floor : workerFloorTls();
}

bool webJobStart(WebJobKind kind, const char* arg, bool flag) {
  if (s_state != WJS_IDLE || kind == WJ_NONE) return false;
  // Checked here rather than inside the task: the stack comes out of the
  // heap the moment the task is created, so testing afterwards would be
  // testing the wrong number.
  if (!workerHeapReady("[webjob]", webJobFloor(kind), WEB_JOB_STACK_BYTES, &s_refusal_gate)) {
    return false;
  }
  s_res.kind   = kind;
  s_res.ok     = false;
  s_res.code   = 0;
  s_res.err[0] = '\0';
  s_res.tag[0] = '\0';
  s_res.pub[0] = '\0';
  s_res.body   = "";
  s_arg[0] = '\0';
  if (arg) { strncpy(s_arg, arg, sizeof(s_arg) - 1); s_arg[sizeof(s_arg) - 1] = '\0'; }
  s_flag = flag;

  // Set before the task exists: if creation fails it is cleared again below,
  // and if it succeeds the task may finish before this line would run.
  s_state = WJS_RUNNING;
  BaseType_t ok = xTaskCreatePinnedToCore(webJobTask, "webjob", WEB_JOB_STACK_BYTES,
                                          nullptr, WEB_JOB_PRIORITY, nullptr, WEB_JOB_CORE);
  if (ok != pdPASS) {
    s_state = WJS_IDLE;
    Serial.println("[webjob] task creation failed");
    return false;
  }
  return true;
}

bool webJobStartFn(WebJobFn fn, uint8_t id, const char *arg, uint32_t floor_bytes) {
  if (!fn || s_state != WJS_IDLE) return false;
  s_fn = fn;
  s_fn_id = id;
  s_floor = floor_bytes;
  return webJobStart(WJ_PROJECT, arg, false);
}

uint8_t webJobProjectId() { return s_fn_id; }

WebJobState webJobState() { return s_state; }
WebJobKind  webJobKind()  { return s_res.kind; }

const WebJobResult& webJobResult() { return s_res; }

void webJobTake() {
  if (s_state != WJS_DONE) return;
  s_res.body = "";
  s_res.kind = WJ_NONE;
  s_state = WJS_IDLE;
}

void webJobsTick() {
  if (s_state == WJS_DONE && millis() - s_done_ms > WEB_JOB_KEEP_MS) {
    Serial.printf("[webjob] result of kind %d dropped, nobody asked\n", (int)s_res.kind);
    webJobTake();
  }
}
