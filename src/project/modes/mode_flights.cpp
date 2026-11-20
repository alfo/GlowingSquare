// Flights: the aircraft nearest overhead, from adsb.lol or adsb.fi, with its
// route from adsbdb.com (all free, no key). Replaces v1's Flightradar24 feed,
// an unofficial endpoint that came with no promise to keep working.
//
// Layout (v1's four lines, redrawn in November 2026 around the airline):
//   [mark]  FROM>TO          the airline's mark, 16x16 (airline_logos.h)
//   [mark]  flight number
//   ^ 2,475ft                climbing, descending or level, then altitude
//   185kt            B788    speed, aircraft type
#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>

#include "project/airline_logos.h"
#include "project/gs_http.h"
#include "project/modes/aviation.h"
#include "project/gs_state.h"
#include "project/modes/modes.h"
#include "project/render.h"

// Climbing or descending at this rate or faster, ft/min; slower is level.
#define CLIMB_FPM            300

struct Flight {
  bool     valid = false;
  char     from[5] = "", to[5] = "";
  char     number[10] = "";
  char     aircraft[8] = "";
  char     airline[4] = "";   // the callsign's ICAO prefix, "" for none
  int8_t   climb = 0;         // 1 climbing, -1 descending, 0 level
  uint32_t speed = 0, altitude = 0;
};

static Flight s_next, s_cur;

// ---- fetch (worker task) ----------------------------------------------------

// The nearest aircraft so far, copied out of each element as it is parsed.
struct Nearest {
  bool     found = false;
  uint8_t  rank = 0;          // flightRank() of the one kept
  float    dst = 1e9f;
  char     callsign[10] = "";
  char     type[8] = "";
  int32_t  altitude = 0;
  int32_t  rate = 0;          // ft/min, + climbing
  float    speed = 0;
};

// One aircraft from the feed: kept if it is in the air, high enough, has a
// callsign to look up, and beats the best so far: a better flightRank(), or
// the same and nearer. An airline at the edge of the radius is shown over a
// helicopter overhead.
static void consider(JsonObjectConst a, void *ctx) {
  Nearest &n = *(Nearest *)ctx;
  if (!a["alt_baro"].is<int>()) return;               // "ground"
  const int32_t alt = a["alt_baro"] | 0;
  if (alt < g_fl_min_alt_ft) return;
  const char *cs = a["flight"] | "";
  if (!cs[0]) return;
  const float d = a["dst"] | 1e8f;
  const uint8_t rank = flightRank(cs);
  if (n.found && (rank < n.rank || (rank == n.rank && d >= n.dst))) return;
  n.found = true;
  n.rank = rank;
  n.dst = d;
  n.altitude = alt;
  n.speed = a["gs"] | 0.0f;
  n.rate = a["baro_rate"] | (a["geom_rate"] | 0);
  snprintf(n.callsign, sizeof(n.callsign), "%s", cs);
  snprintf(n.type, sizeof(n.type), "%s", a["t"] | "");
}

static void resetNearest(void *ctx) { *(Nearest *)ctx = Nearest(); }

static bool flightsFetch(char *err, size_t err_n) {
  const float lat = atof(g_fl_lat), lon = atof(g_fl_lon);
  if (!g_fl_lat[0] || !g_fl_lon[0] || fabsf(lat) > 90 || fabsf(lon) > 180) {
    snprintf(err, err_n, "no position set");
    return false;
  }
  JsonDocument filter;
  filter["flight"] = true;
  filter["alt_baro"] = true;
  filter["gs"] = true;
  filter["t"] = true;
  filter["dst"] = true;
  filter["baro_rate"] = true;
  filter["geom_rate"] = true;
  Nearest best;
  const AdsbQuery q = { lat, lon, (int)g_fl_radius_nm, &filter, consider, resetNearest, &best };
  if (!adsbWalk(q, err, err_n)) return false;

  s_next = Flight();
  if (!best.found) return true;     // a clear sky is an answer, not a failure

  trimRight(best.callsign);
  const Route *r = routeFor(best.callsign);
  s_next.valid = true;
  airlinePrefix(best.callsign, s_next.airline);
  s_next.climb = best.rate >= CLIMB_FPM ? 1 : best.rate <= -CLIMB_FPM ? -1 : 0;
  s_next.altitude = best.altitude;
  s_next.speed = (uint32_t)lroundf(best.speed);
  snprintf(s_next.aircraft, sizeof(s_next.aircraft), "%s", best.type);
  snprintf(s_next.number, sizeof(s_next.number), "%s", r->number[0] ? r->number : best.callsign);
  snprintf(s_next.from, sizeof(s_next.from), "%s", r->known ? r->from : "");
  snprintf(s_next.to, sizeof(s_next.to), "%s", r->known ? r->to : "");
  return true;
}

// ---- drawing (loop task) ----------------------------------------------------

// Each line its own colour, as the Unifi mode has them, and saturated: the
// panel shows a value as light, so a little of a second channel (sky blue,
// violet) reads as washed out, not as a tint, and grey as a dim white. The
// blue and purple are the Unifi mode's own.
static const uint16_t C_ROUTE  = rgb565(255, 120, 0);    // amber
static const uint16_t C_NUMBER = rgb565(210, 210, 210);  // soft white
static const uint16_t C_ALT    = rgb565(0, 220, 255);    // cyan
static const uint16_t C_SPEED  = rgb565(21, 58, 234);    // Unifi's graph blue
static const uint16_t C_TYPE   = rgb565(110, 10, 204);   // Unifi's upload purple
static const uint16_t C_UP     = rgb565(0, 255, 0);
static const uint16_t C_DOWN   = rgb565(255, 0, 0);
static const uint16_t C_LEVEL  = rgb565(130, 130, 130);

constexpr int16_t TEXT_X = AIRLINE_LOGO_SIZE + 2;   // right of the mark
constexpr int16_t ARROW_W = 5, ARROW_H = 7;

static const char *const ARROW_UP[ARROW_H] = {
  "..#..", ".###.", "#.#.#", "..#..", "..#..", "..#..", "..#..",
};

static void drawArt(int16_t x, int16_t y, const char *const *rows, int16_t w, int16_t h,
                    uint16_t c, bool flip = false) {
  GFXcanvas16 &cv = matrixCanvas();
  for (int16_t j = 0; j < h; j++)
    for (int16_t i = 0; i < w; i++)
      if (rows[flip ? h - 1 - j : j][i] == '#') cv.drawPixel(x + i, y + j, c);
}

// "38,000ft": the thousands set apart, as a pilot reads it out.
static void altitudeText(uint32_t ft, char *out, size_t n) {
  if (ft >= 1000) snprintf(out, n, "%u,%03uft", (unsigned)(ft / 1000), (unsigned)(ft % 1000));
  else            snprintf(out, n, "%uft", (unsigned)ft);
}

static void draw() {
  GFXcanvas16 &c = matrixCanvas();
  c.fillScreen(0);
  // Nothing overhead: a dark display. v1 kept the last flight standing,
  // which read as a plane that never moved.
  if (!s_cur.valid) return;

  drawAirlineMark(0, 0, s_cur.airline);
  char line[24];
  // A route adsbdb does not know: the flight number moves up to the top.
  if (s_cur.from[0]) {
    snprintf(line, sizeof(line), "%s>%s", s_cur.from, s_cur.to);
    drawText(TEXT_X, 0, line, C_ROUTE);
    drawText(TEXT_X, ROW_H, s_cur.number, C_NUMBER);
  } else {
    drawText(TEXT_X, 0, s_cur.number, C_NUMBER);
  }

  if (s_cur.climb) {
    drawArt(0, 2 * ROW_H, ARROW_UP, ARROW_W, ARROW_H, s_cur.climb > 0 ? C_UP : C_DOWN,
            s_cur.climb < 0);
  } else {
    c.drawFastHLine(0, 2 * ROW_H + 3, ARROW_W, C_LEVEL);
  }
  altitudeText(s_cur.altitude, line, sizeof(line));
  drawText(ARROW_W + 2, 2 * ROW_H, line, C_ALT);

  snprintf(line, sizeof(line), "%ukt", (unsigned)s_cur.speed);
  drawText(0, 3 * ROW_H, line, C_SPEED);
  drawRightAligned(MATRIX_W - 1, 3 * ROW_H, s_cur.aircraft, C_TYPE);
}

static void flightsEnter()  { draw(); }
static void flightsCommit() { s_cur = s_next; draw(); }
static bool flightsFrame(uint32_t) { return false; }

const DisplayMode MODE_FLIGHTS_OPS = {
  "flights", "glowingsquare/flight", 10000,
  flightsFetch, flightsCommit, flightsEnter, flightsFrame,
};
