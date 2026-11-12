#pragma once

#include <Arduino.h>
#include <stdint.h>

// ============================================================
//  WEB JOBS
//
//  Some things a browser can ask for take seconds to answer: the GitHub
//  check, the release notes, a project's "test this connection" button.
//  Answered inside the HTTP handler they would hold the loop task - display,
//  MQTT, everything - for that long, because the handler runs on that task.
//
//  Now the handler starts a job and answers 202. The job runs on its own
//  task, the same shape as the daily update check (core 0, below the loop
//  in priority, stack from the heap for as long as it runs), and the
//  browser asks the same URL again until the answer is ready. One job at a
//  time: a second TLS handshake beside the first is 40 kB the device does
//  not have twice.
//
//  What must not run on the second task stays out of it: LVGL, NVS and the
//  badges are touched by the handler that collects the result, on the loop
//  task; logSD() queues lines from other tasks; the stall counters only
//  count the loop task's own waits.
// ============================================================

enum WebJobKind : uint8_t {
  WJ_NONE = 0,
  WJ_GH_CHECK,    // the latest tag on the chosen channel, TLS
  WJ_GH_NOTES,    // one release's notes, TLS
  WJ_PROJECT      // a function the project hands in, see webJobStartFn()
};

enum WebJobState : uint8_t {
  WJS_IDLE = 0,
  WJS_RUNNING,
  WJS_DONE
};

struct WebJobResult {
  WebJobKind kind;
  bool   ok;
  int    code;        // the HTTP code behind the answer, where there is one
  char   err[80];     // why not, when ok is false
  char   tag[40];     // GH check: the tag found
  char   pub[24];     // GH check: when it was published
  uint32_t image_size; // GH check: the firmware image's size, 0 when unknown
  String body;        // a ready made reply body, for the jobs that have one
};

// Starts a job. `arg` is the release tag for WJ_GH_NOTES, unused otherwise;
// `flag` is the pre-release channel for WJ_GH_CHECK. False when a job is
// already running or the heap is too low for a second task.
bool webJobStart(WebJobKind kind, const char* arg, bool flag);

WebJobState webJobState();
WebJobKind  webJobKind();

// The result, valid while the state is WJS_DONE. webJobTake() hands the slot
// back; a handler calls it once it has sent the answer.
const WebJobResult& webJobResult();
void webJobTake();

// A project job: `fn` runs on the job task with `arg` copied, fills the
// result (ok, code, err, body) and returns. `floor_bytes` is the heap it
// needs, workerFloorFor(url) for a request to url. `id` tells two project
// jobs apart when a handler collects (webJobProjectId()).
typedef void (*WebJobFn)(WebJobResult &res, const char *arg);
bool webJobStartFn(WebJobFn fn, uint8_t id, const char *arg, uint32_t floor_bytes);
uint8_t webJobProjectId();

// From appLoop(): drops a result no browser came back for.
void webJobsTick();
