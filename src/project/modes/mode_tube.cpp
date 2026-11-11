// Tube / London Bus: the next four departures from one stop, from TfL.
//
// Layout as v1: four rows, the minutes to go in orange, then the
// destination and platform in grey, scrolling past them. After every fetch
// the rows scroll one after the other, a second apart.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <algorithm>

#include "project/gs_http.h"
#include "project/gs_state.h"
#include "project/modes/modes.h"
#include "project/render.h"
#include "project/tfl_rename.h"

#define TUBE_ROWS        4
#define TUBE_ROW_GAP_MS  1000   // v1: delay(1000) between the rows

struct Departure {
  int  seconds;
  char towards[40];
  char platform[28];
};

struct TubeData {
  Departure dep[TUBE_ROWS];
  uint8_t   count = 0;
};

static TubeData s_next, s_cur;

// ---- fetch (worker task) ----------------------------------------------------

static void buildUrl(char *url, size_t n) {
  static const char *DIRS[] = { "", "inbound", "outbound" };
  const char *dir = g_tfl_direction < 3 ? DIRS[g_tfl_direction] : "";
  // Several lines go comma separated ("circle,district"). Both directions
  // means no direction at all: TfL answers an empty one with HTTP 400.
  if (g_tfl_line[0]) {
    snprintf(url, n, "https://api.tfl.gov.uk/Line/%s/Arrivals/%s", g_tfl_line, g_tfl_stop);
  } else {
    snprintf(url, n, "https://api.tfl.gov.uk/StopPoint/%s/Arrivals", g_tfl_stop);
  }
  char sep = '?';
  size_t len = strlen(url);
  if (g_tfl_line[0] && dir[0]) {
    snprintf(url + len, n - len, "%cdirection=%s", sep, dir);
    sep = '&';
    len = strlen(url);
  }
  if (g_tfl_app_key[0]) snprintf(url + len, n - len, "%capp_key=%s", sep, g_tfl_app_key);
}

// One prediction from the walk: kept if it is on the platform asked for and
// among the four soonest so far. TfL does not sort them; v1 did, and so does
// this, as they come.
static void consider(JsonObjectConst v, void *ctx) {
  TubeData &t = *(TubeData *)ctx;
  // A platform filter picks one side of the station whatever line or
  // direction TfL gives the train; some predictions carry neither.
  if (g_tfl_plat_filter[0] && !strstr(v["platformName"] | "", g_tfl_plat_filter)) return;
  const int seconds = v["timeToStation"] | 0;
  int at = t.count;
  while (at > 0 && t.dep[at - 1].seconds > seconds) at--;
  if (at >= TUBE_ROWS) return;
  for (int i = std::min<int>(t.count, TUBE_ROWS - 1); i > at; i--) t.dep[i] = t.dep[i - 1];
  if (t.count < TUBE_ROWS) t.count++;
  Departure &d = t.dep[at];
  d.seconds = seconds;
  // Buses often carry no "towards"; their destination says the same.
  const char *to = v["towards"] | "";
  if (!to[0]) to = v["destinationName"] | "";
  tflRename(to, g_tfl_rename, d.towards, sizeof(d.towards));
  snprintf(d.platform, sizeof(d.platform), "%s", v["platformName"] | "");
}

static bool tubeFetch(char *err, size_t err_n) {
  if (!g_tfl_stop[0]) { snprintf(err, err_n, "no stop set"); return false; }
  char url[200];
  buildUrl(url, sizeof(url));

  // TfL's answer can be an absolute chonker with both directions: it is read
  // a prediction at a time, and only the four fields drawn are kept.
  JsonDocument filter;
  filter["timeToStation"] = true;
  filter["towards"] = true;
  filter["destinationName"] = true;
  filter["platformName"] = true;

  GsHttp req;
  req.url = url;
  s_next = TubeData();
  if (gsHttpJsonEach(req, nullptr, filter, consider, &s_next, err, err_n) < 0) {
    if (!err[0]) snprintf(err, err_n, "unexpected answer");
    return false;
  }
  return true;
}

// ---- drawing (loop task) ----------------------------------------------------

static ScrollRow s_row;
static int8_t    s_row_i = -1;          // the row scrolling now, -1 none
static uint32_t  s_next_row_ms = 0;

static const uint16_t C_MINS   = rgb565(255, 80, 0);
static const uint16_t C_TOWARD = rgb565(150, 150, 150);

static void startRow(uint8_t i) {
  s_next_row_ms = 0;   // a re-fetch mid-gap must not inherit the old gap
  if (i >= s_cur.count) {
    // No departure for this row or any below it: clear them all. v1 cleared
    // this one and stopped, which left a stale row beneath when the list got
    // shorter.
    for (uint8_t r = i; r < TUBE_ROWS; r++) fillBlankRow(r * ROW_H);
    s_row_i = -1;
    return;
  }
  const Departure &d = s_cur.dep[i];
  char mins[12], text[sizeof(d.towards) + sizeof(d.platform) + 2];
  snprintf(mins, sizeof(mins), "%dm", d.seconds / 60);
  if (g_tfl_platform && d.platform[0]) snprintf(text, sizeof(text), "%s %s", d.towards, d.platform);
  else                                 snprintf(text, sizeof(text), "%s", d.towards);
  s_row.start(i * ROW_H, mins, text, C_MINS, C_TOWARD, (uint32_t)g_scroll_ms);
  s_row_i = (int8_t)i;
}

static void tubeEnter() {
  matrixCanvas().fillScreen(0);
  s_row.stop();
  s_row_i = -1;
  if (s_cur.count) startRow(0);
}

static void tubeCommit() {
  s_cur = s_next;
  s_row.stop();
  startRow(0);
}

static bool tubeFrame(uint32_t now) {
  if (s_row_i < 0) return false;
  if (!s_row.done()) return s_row.tick(now);
  // The row finished: the next one a second later, as v1.
  if (!s_next_row_ms) s_next_row_ms = now + TUBE_ROW_GAP_MS;
  if ((int32_t)(now - s_next_row_ms) < 0) return false;
  s_next_row_ms = 0;
  const uint8_t next = (uint8_t)(s_row_i + 1);
  if (next >= TUBE_ROWS) { s_row_i = -1; return false; }
  startRow(next);
  return true;
}

const DisplayMode MODE_TUBE_OPS = {
  "tube", "glowingsquare/tube", 60000,
  tubeFetch, tubeCommit, tubeEnter, tubeFrame,
};
