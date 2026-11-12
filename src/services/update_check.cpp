#include "update_check.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <time.h>
#include <string.h>

#include "app_config.h"
#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/partition_layout.h"
#include "services/github_release.h"
#include "services/ota_state.h"
#include "web/web_server.h"
#include "services/prefs_store.h"
#include "services/version_compare.h"
#include "services/worker_heap.h"
#include "services/ota_ui.h"

namespace {

// Served from GitHub Pages next to manifest.json and written by the release
// workflow. Deliberately not api.github.com: that endpoint allows 60 requests
// per hour and IP, which users behind carrier grade NAT share, and its answer
// for the release list is over a hundred kilobytes. This file is a few hundred
// bytes and has no rate limit.
// APP_PAGES_URL ends in a slash. Empty: no background check at all.
constexpr const char* VERSION_URL = APP_PAGES_URL "version.json";
constexpr bool        VERSION_URL_SET = sizeof(APP_PAGES_URL) > 1;

// The first check waits until the boot is long over. Display, NFC, scale and
// the backend are all up well before this, and the heap has settled.
constexpr unsigned long FIRST_RUN_DELAY_MS = 90000;
// Devices in one workshop all boot when the power strip goes on. Without a
// spread they would hit the server in the same second.
constexpr unsigned long FIRST_RUN_JITTER_MS = 60000;
// After a failure, and as the re-evaluation interval while a check is not yet
// due. Long enough that a server outage cannot turn into a request storm.
constexpr unsigned long RETRY_DELAY_MS = 3600000;
// At most one check per day per device.
constexpr uint32_t CHECK_INTERVAL_S = 86400;
constexpr int HTTP_TIMEOUT_MS = 4000;

// Unix time safely in the past. getLocalTime() reports 1970 until NTP has
// answered, and a timestamp from then would make the daily window meaningless.
constexpr uint32_t TIME_IS_SYNCED = 1700000000;

// The mbedTLS handshake is the deepest thing this task does and wants roughly
// 8 kB on its own. 12 kB leaves headroom for the JSON parse on top of it. The
// stack is taken from the heap when the task starts and given back when it
// deletes itself, so this costs nothing for the other 86399 seconds of the day.
// The watermark is logged below; if it stays comfortable this can come down.
// Raised from 12 kB when the connection stopped being setInsecure(): the
// certificate bundle parses the matching root inside the handshake, on this
// stack.
constexpr uint32_t TASK_STACK_BYTES = 14336;
constexpr int      TASK_PRIORITY    = 1;   // below the Arduino loop task
constexpr int      TASK_CORE        = 0;   // the loop runs on core 1

enum Result : uint8_t {
  RES_NONE = 0,
  RES_UP_TO_DATE,
  RES_AVAILABLE,
  RES_FAIL_HTTP,
  RES_FAIL_JSON,
  RES_FAIL_HEAP,
  RES_FAIL_BEGIN,
};

// Handover from the task to updateCheckTick(). Everything else is written
// before s_result and read after it, so the flag alone orders the exchange and
// no mutex is needed.
volatile uint8_t s_result = RES_NONE;
volatile int     s_http_code = 0;
volatile bool    s_task_running = false;
volatile uint32_t s_persist_epoch = 0;   // non-zero: tick writes it to NVS

// The task stages the tag it found here instead of writing gh_latest_version
// directly. That buffer is read by the OTA screen on the loop task, and a
// background result landing mid-draw would otherwise show a half-written
// string. Only updateCheckTick() copies it across.
char s_found_version[32] = "";
char s_found_sha[65]     = "";
// The image's size, next to its checksum in version.json; 0 from a file
// written before the workflow published it.
volatile uint32_t s_found_size = 0;

unsigned long s_due_ms = 0;
bool s_scheduled = false;

// The whole check, in a function of its own: the task deletes itself, and a
// task that calls vTaskDelete(NULL) never returns, so the destructors of the
// locals in that frame never run. With the TLS client among them that leaked
// some 55 kB of internal heap per check, which on a board without PSRAM left
// too little for any later HTTPS request. Here every local is gone before the
// task ends.
uint8_t runCheck() {
  // Before the client, so the connection is closed before the slot is given.
  TlsSlot slot;
  if (!slot.held()) return RES_FAIL_BEGIN;
  WiFiClientSecure client;
  githubTrust(client);
  HTTPClient http;

  if (!http.begin(client, VERSION_URL)) return RES_FAIL_BEGIN;

  http.addHeader("User-Agent", APP_USER_AGENT);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);

  int code = http.GET();
  s_http_code = code;

  if (code != 200) {
    Serial.printf("[upd] HTTP %d\n", code);
    http.end();
    return RES_FAIL_HTTP;
  }

  // Parsed straight off the stream. The old code pulled the whole body into a
  // String first, which for the release list meant a six figure allocation on
  // a fragmented heap.
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) {
    Serial.printf("[upd] JSON error: %s\n", err.c_str());
    return RES_FAIL_JSON;
  }

  // The server answered, so the daily window starts now. Set before the
  // result is published: the tick reads the result first and would otherwise
  // take the no-clock branch and lose the timestamp until its next pass.
  const time_t now = time(nullptr);
  if ((uint32_t)now > TIME_IS_SYNCED) s_persist_epoch = (uint32_t)now;

  // With pre-releases on, fall back to the stable tag when the file carries no
  // separate pre-release, which is the case right after a public release.
  //
  // Each build env has its own entry under "envs", so a firmware built for
  // one board is never offered to another. A project with a single env may
  // publish the fields at the top level instead.
  JsonVariantConst e = doc["envs"][APP_ENV];
  if (e.isNull()) e = doc.as<JsonVariantConst>();
  const char* tag = nullptr;
  const char* sha = nullptr;
  uint32_t size = 0;
  if (gh_prerelease) {
    tag = e["prerelease"].as<const char*>();
    sha = e["prerelease_sha256"].as<const char*>();
    size = e["prerelease_size"] | 0u;
  }
  if (!tag || tag[0] == '\0') {
    tag = e["stable"].as<const char*>();
    sha = e["stable_sha256"].as<const char*>();
    size = e["stable_size"] | 0u;
  }
  s_found_size = size;

  // Nothing published on this channel yet (a project with betas only, and
  // pre-releases switched off): nothing to update to, not a bad answer.
  if (!tag || tag[0] == '\0') return RES_UP_TO_DATE;

  strncpy(s_found_version, tag, sizeof(s_found_version) - 1);
  s_found_version[sizeof(s_found_version) - 1] = '\0';
  // Published next to the tag by the release workflow. A version.json from
  // before that carries none, and the flash then runs on TLS alone.
  s_found_sha[0] = '\0';
  if (sha && strlen(sha) == 64) {
    strncpy(s_found_sha, sha, sizeof(s_found_sha) - 1);
    s_found_sha[sizeof(s_found_sha) - 1] = '\0';
  }

  uint64_t installed = parseVersion(FW_VERSION);
  uint64_t remote    = parseVersion(s_found_version);
  Serial.printf("[upd] installed=%s latest=%s\n", FW_VERSION, s_found_version);

  Serial.printf("[upd] task stack low water mark: %u bytes free\n",
                (unsigned)uxTaskGetStackHighWaterMark(NULL));

  return (remote > installed) ? RES_AVAILABLE : RES_UP_TO_DATE;
}

void updateCheckTask(void* arg) {
  (void)arg;
  const uint8_t result = runCheck();
  // Published last. Everything the tick needs is in place by now.
  s_result = result;
  s_task_running = false;
  vTaskDelete(NULL);
}

void maybeStartCheck() {
  if (!s_scheduled) return;
  if (!g_upd_autocheck) return;
  if (!wifi_ok) return;
  if (s_task_running) return;
  // Never a second TLS connection while an image is being written.
  if (gh_flash_active || otaWebUploadActive()) return;
  // Signed difference, so this survives the millis() wrap after 49 days.
  if ((long)(millis() - s_due_ms) < 0) return;

  // Checked here rather than inside the task: the stack comes out of the
  // heap the moment the task is created, so testing after the fact would be
  // testing the wrong number. Skipping a version check costs nothing, an out
  // of memory reboot costs the user their session. GitHub is always https,
  // so the TLS floor of services/backend_job_rules.h. A refusal waits
  // RETRY_DELAY_MS, so it is logged every time.
  if (!workerHeapReady("[upd]", workerFloorTls(), TASK_STACK_BYTES, nullptr)) {
    Serial.printf("[upd] postponed, heap %u\n", (unsigned)ESP.getFreeHeap());
    s_result = RES_FAIL_HEAP;
    s_due_ms = millis() + RETRY_DELAY_MS;
    return;
  }

  // Without a synced clock this test cannot run; updateCheckTick() then keeps
  // the interval on uptime instead.
  time_t now = time(nullptr);
  if ((uint32_t)now > TIME_IS_SYNCED && g_upd_last_epoch > TIME_IS_SYNCED &&
      (uint32_t)now - g_upd_last_epoch < CHECK_INTERVAL_S) {
    s_due_ms = millis() + RETRY_DELAY_MS;
    return;
  }

  // Set before the task exists: if creation fails the guard is cleared again
  // below, and if it succeeds the task may finish before this line would run.
  s_task_running = true;
  s_due_ms = millis() + RETRY_DELAY_MS;

  BaseType_t ok = xTaskCreatePinnedToCore(updateCheckTask, "updchk",
                                          TASK_STACK_BYTES, nullptr,
                                          TASK_PRIORITY, nullptr, TASK_CORE);
  if (ok != pdPASS) {
    s_task_running = false;
    Serial.println("[upd] task creation failed");
  }
}

}  // namespace

bool updateCheckBusy() { return s_task_running; }

// Restores the badge from what the last check found, without going near the
// network. Comparing rather than trusting a stored flag means it corrects
// itself: once FW_VERSION has caught up, the badge stays off on its own.
void updateCheckRestoreBadge() {
  String stored_s = prefsGetString("upd_ver", "");
  const char* stored = stored_s.c_str();
  if (stored[0] == '\0') return;
  if (parseVersion(stored) <= parseVersion(FW_VERSION)) return;

  strncpy(gh_latest_version, stored, sizeof(gh_latest_version) - 1);
  gh_latest_version[sizeof(gh_latest_version) - 1] = '\0';
  // The checksum that came with it, if the workflow published one.
  String sha_s = prefsGetString("upd_sha", "");
  if (sha_s.length() == 64) {
    strncpy(gh_latest_sha, sha_s.c_str(), sizeof(gh_latest_sha) - 1);
    gh_latest_sha[sizeof(gh_latest_sha) - 1] = '\0';
    strncpy(gh_latest_sha_tag, gh_latest_version, sizeof(gh_latest_sha_tag) - 1);
    gh_latest_sha_tag[sizeof(gh_latest_sha_tag) - 1] = '\0';
  }
  update_available = true;
  otaUiBadges(true);
  logSDf("Update badge restored from NVS: %s > %s", stored, FW_VERSION);
}

void updateCheckScheduleFirstRun() {
  if (!VERSION_URL_SET) return;   // the project publishes no version.json
  s_due_ms = millis() + FIRST_RUN_DELAY_MS + (esp_random() % FIRST_RUN_JITTER_MS);
  s_scheduled = true;
}

void updateCheckScheduleIn(unsigned long delay_ms) {
  if (!VERSION_URL_SET) return;
  s_due_ms = millis() + delay_ms;
  s_scheduled = true;
}

void updateCheckTick() {
  // Everything below runs on the loop task on purpose. LVGL is not thread
  // safe, the SD logger shares one file handle, and NVS writes block on flash.
  // The task only sets flags; the side effects happen here.
  uint8_t result = s_result;
  bool answered = (result == RES_AVAILABLE || result == RES_UP_TO_DATE);
  if (result != RES_NONE) {
    s_result = RES_NONE;
    switch (result) {
      case RES_AVAILABLE:
        // Larger than this device's app slot: no badge that leads to a
        // download bound to fail, the hint with the way to the flasher instead.
        if (!partitionImageFits(s_found_size)) {
          partitionNoteTooBig(s_found_version, s_found_size);
          break;
        }
        strncpy(gh_latest_version, s_found_version, sizeof(gh_latest_version) - 1);
        gh_latest_version[sizeof(gh_latest_version) - 1] = '\0';
        strncpy(gh_latest_sha, s_found_sha, sizeof(gh_latest_sha) - 1);
        gh_latest_sha[sizeof(gh_latest_sha) - 1] = '\0';
        strncpy(gh_latest_sha_tag, s_found_version, sizeof(gh_latest_sha_tag) - 1);
        gh_latest_sha_tag[sizeof(gh_latest_sha_tag) - 1] = '\0';
        update_available = true;
        otaUiBadges(true);
        logSDf("Update check: %s available (installed %s)",
               gh_latest_version, FW_VERSION);
        break;
      case RES_UP_TO_DATE:
        // A clean result has to take the badge down again, not just leave the
        // old one standing. Switching pre-releases off would otherwise keep
        // the dot for a build the device no longer looks for, while the OTA
        // screen showed no version to install.
        //
        // Not while that screen is open, though: the user may be looking at
        // the result of a manual check, and pulling the labels out from under
        // it would be the device arguing with itself. The next check settles it.
        if (!otaUiScreenBusy()) {
          gh_latest_version[0] = '\0';
          gh_latest_sha[0] = '\0';
          gh_latest_sha_tag[0] = '\0';
          update_available = false;
          otaUiBadges(false);
        }
        if (sd_verbose) logSDf("[verbose] Update check: up to date (%s)", FW_VERSION);
        break;
      case RES_FAIL_HTTP:
        logSDf("Update check: HTTP %d", s_http_code);
        break;
      case RES_FAIL_JSON:
        logSD("Update check: bad response");
        break;
      case RES_FAIL_HEAP:
        logSD("Update check: skipped, low heap");
        break;
      case RES_FAIL_BEGIN:
        logSD("Update check: connection setup failed");
        break;
      default:
        break;
    }
  }

  uint32_t epoch = s_persist_epoch;
  if (epoch != 0) {
    s_persist_epoch = 0;
    g_upd_last_epoch = epoch;
    prefsPutUInt("upd_last", epoch);
    // The tag goes with it. Only the timestamp used to be kept, and since the
    // check skips itself for a day, a reboot inside that window left the badge
    // off while an update was in fact waiting.
    //
    // Only a version the badge stands for. gh_latest_version also holds one
    // found too large for this device's slot, and stored here it came back
    // after a reboot as "update available" through updateCheckRestoreBadge().
    const bool keep = update_available && !partitionTagTooBig(gh_latest_version);
    prefsPutString("upd_ver", keep ? gh_latest_version : "");
    prefsPutString("upd_sha", keep ? gh_latest_sha : "");
  } else if (answered) {
    // The server answered but NTP has not, so the daily window cannot be
    // measured in wall clock time. Count uptime instead, otherwise the retry
    // interval would take over and the device would check every hour.
    s_due_ms = millis() + (unsigned long)CHECK_INTERVAL_S * 1000UL;
  }

  maybeStartCheck();
}
