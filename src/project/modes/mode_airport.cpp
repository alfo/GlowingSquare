// Airport: the planes landing at and leaving one airport, each drawn on its
// way down or up. From adsb.lol or adsb.fi (the aircraft within
// AIRPORT_RADIUS_NM of it) and adsbdb (which of them are coming here or
// leaving from here), all free, no key.
//
// Layout:
//   [mark]  flight number          the airline's mark, 16x16 (aviation.h)
//   [mark]  the other end          its city (scrolling) or its code
//   rows 16..31: a side view, distance across and height up. An arrival
//   comes down from the left to the runway at the right; a departure
//   climbs away from the runway at the left. The 3 degree glide path is
//   dotted in, the plane is a bright 2x2 with its trail behind it (one dot
//   a fetch), and between fetches it moves on at its own speed and rate of
//   climb or descent.
//
// Nothing near: the airport's code where the flight number goes, and the
// path and runway with no plane.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>

#include "project/airline_logos.h"
#include "project/gs_state.h"
#include "project/modes/aviation.h"
#include "project/modes/modes.h"
#include "project/render.h"

#define AIRPORT_RADIUS_NM    15
// Above this a plane is passing over, not landing or leaving: Heathrow's
// arrivals join the approach below 7,000 ft some 20 nm out.
#define AIRPORT_MAX_ALT_FT   7000
#define CANDIDATES_N         6
// adsbdb lookups one fetch may start, a TLS handshake each; the rest wait
// for the next fetch, the cache answering for the ones already seen.
#define NEW_ROUTES_PER_FETCH 2
#define TRAIL_N              16
// A plane adsbdb has no route for (BA's shuttles, often) is judged by how it
// flies: coming down within GUESS_NM and no more than GUESS_PATH_FT above
// the glide path is landing here; climbing away below GUESS_DEP_FT within
// GUESS_NM is leaving. Nothing else lands that close to a big airport's
// runway. A route adsbdb does know is never second-guessed.
#define GUESS_NM             10.0f
#define GUESS_PATH_FT        1000.0f
#define GUESS_DEP_FT         5000
#define CLIMB_FPM            300
#define CHART_NM             12.0f     // across the panel
#define CHART_FT             4000.0f   // up the chart
#define GLIDE_FT_PER_NM      318.0f    // a 3 degree path
#define ALTERNATE_MS         8000UL
#define FRAME_MS             100UL
#define SCROLL_HOLD_MS       2000UL
#define FETCH_INTERVAL_MS    10000UL
// How long the plane moves on from its last position before it waits for
// the next one: a failed fetch must not fly it off the chart.
#define GLIDE_MAX_MS         (2 * FETCH_INTERVAL_MS)

struct Movement {
  bool     valid = false;
  bool     arrival = true;
  char     hex[8] = "";      // the aircraft, to follow it from fetch to fetch
  char     number[10] = "";
  char     airline[4] = "";
  char     place[24] = "";   // the other end, as the setting names it
  float    dst = 0;          // nm from the airport
  int32_t  alt = 0;          // ft
  float    gs = 0;           // kt
  int32_t  rate = 0;         // ft/min, + climbing
  uint8_t  trail_n = 0;      // positions before this one, oldest first
  float    trail_dst[TRAIL_N];
  int16_t  trail_alt[TRAIL_N];
};

struct AirportData { Movement arr, dep; };

static AirportData s_next, s_cur;

// ---- fetch (worker task) ----------------------------------------------------

struct Candidate {
  char    hex[8];
  char    callsign[10];
  float   dst;
  int32_t alt;
  float   gs;
  int32_t rate;
};
struct Sky {
  Candidate c[CANDIDATES_N];
  uint8_t   n = 0;
};

static void resetSky(void *ctx) { ((Sky *)ctx)->n = 0; }

// Where `d` goes among the nearest so far, CANDIDATES_N at most: the index,
// or -1 when it is further than all of a full list.
static int slotFor(const Sky &sky, float d) {
  int i = sky.n;
  while (i > 0 && sky.c[i - 1].dst > d) i--;
  return i < CANDIDATES_N ? i : -1;
}

// One aircraft: kept when it flies an airline's callsign low enough to be
// landing or leaving, among the CANDIDATES_N nearest.
static void consider(JsonObjectConst a, void *ctx) {
  Sky &sky = *(Sky *)ctx;
  if (!a["alt_baro"].is<int>()) return;               // "ground"
  const int32_t alt = a["alt_baro"] | 0;
  if (alt > AIRPORT_MAX_ALT_FT) return;
  char prefix[4];
  airlinePrefix(a["flight"] | "", prefix);
  if (!prefix[0]) return;                              // no airline, no route
  const float d = a["dst"] | 1e8f;
  const int i = slotFor(sky, d);
  if (i < 0) return;
  const int last = sky.n < CANDIDATES_N ? sky.n : CANDIDATES_N - 1;
  memmove(&sky.c[i + 1], &sky.c[i], (size_t)(last - i) * sizeof(Candidate));
  Candidate &c = sky.c[i];
  snprintf(c.hex, sizeof(c.hex), "%s", a["hex"] | "");
  snprintf(c.callsign, sizeof(c.callsign), "%s", a["flight"] | "");
  trimRight(c.callsign);
  c.dst = d;
  c.alt = alt;
  c.gs = a["gs"] | 0.0f;
  c.rate = a["baro_rate"] | (a["geom_rate"] | 0);
  if (sky.n < CANDIDATES_N) sky.n++;
}

// `r` nullptr: a guessed direction, no route to name the other end.
static void fill(Movement &m, const Candidate &c, const Route *r, bool arrival) {
  m = Movement();
  m.valid = true;
  m.arrival = arrival;
  snprintf(m.hex, sizeof(m.hex), "%s", c.hex);
  snprintf(m.number, sizeof(m.number), "%s", r && r->number[0] ? r->number : c.callsign);
  airlinePrefix(c.callsign, m.airline);
  if (r) {
    const char *city = arrival ? r->from_city : r->to_city;
    const char *code = arrival ? r->from : r->to;
    snprintf(m.place, sizeof(m.place), "%s", g_ap_place == AP_PLACE_CITY && city[0] ? city : code);
  }
  m.dst = c.dst;
  m.alt = c.alt;
  m.gs = c.gs;
  m.rate = c.rate;
}

// The previous position of the same aircraft goes on its trail; another
// aircraft starts a trail of its own. `prev` is the slot's last fetch.
static void carryTrail(Movement &m, Movement &prev) {
  if (m.valid && prev.valid && strcmp(m.hex, prev.hex) == 0) {
    uint8_t n = prev.trail_n;
    memcpy(m.trail_dst, prev.trail_dst, sizeof(m.trail_dst));
    memcpy(m.trail_alt, prev.trail_alt, sizeof(m.trail_alt));
    if (n == TRAIL_N) {   // full: the oldest goes
      memmove(m.trail_dst, m.trail_dst + 1, (TRAIL_N - 1) * sizeof(float));
      memmove(m.trail_alt, m.trail_alt + 1, (TRAIL_N - 1) * sizeof(int16_t));
      n--;
    }
    m.trail_dst[n] = prev.dst;
    m.trail_alt[n] = (int16_t)prev.alt;
    m.trail_n = n + 1;
  }
  prev = m;
}

static Movement s_prev_arr, s_prev_dep;   // the fetch task's alone

// 1 landing here, -1 leaving, 0 neither, by its flight alone (GUESS_NM).
static int8_t guessDirection(const Candidate &c) {
  if (c.dst > GUESS_NM) return 0;
  if (c.rate <= -CLIMB_FPM && c.alt <= c.dst * GLIDE_FT_PER_NM + GUESS_PATH_FT) return 1;
  if (c.rate >= CLIMB_FPM && c.alt <= GUESS_DEP_FT) return -1;
  return 0;
}

// The nearest arrival and the nearest departure among the candidates.
static void classify(const Sky &sky, AirportData &out) {
  int asked = 0;
  for (uint8_t i = 0; i < sky.n && !(out.arr.valid && out.dep.valid); i++) {
    const Candidate &c = sky.c[i];
    const Route *r = routeFor(c.callsign, false);
    if (!r && asked < NEW_ROUTES_PER_FETCH) { r = routeFor(c.callsign, true); asked++; }
    if (!r) continue;   // not asked yet: the next fetch will
    int8_t dir = 0;
    if (r->known) dir = routeEndIs(g_ap_code, r->to, r->to_icao) ? 1
                      : routeEndIs(g_ap_code, r->from, r->from_icao) ? -1 : 0;
    else dir = guessDirection(c);
    const Route *named = r->known ? r : nullptr;
    if (dir > 0 && !out.arr.valid) fill(out.arr, c, named, true);
    if (dir < 0 && !out.dep.valid) fill(out.dep, c, named, false);
  }
}

static bool airportFetch(char *err, size_t err_n) {
  const float lat = atof(g_ap_lat), lon = atof(g_ap_lon);
  if (!g_ap_code[0]) { snprintf(err, err_n, "no airport set"); return false; }
  if (!g_ap_lat[0] || !g_ap_lon[0] || fabsf(lat) > 90 || fabsf(lon) > 180) {
    snprintf(err, err_n, "no position set");
    return false;
  }
  JsonDocument filter;
  filter["hex"] = true;
  filter["flight"] = true;
  filter["alt_baro"] = true;
  filter["gs"] = true;
  filter["dst"] = true;
  filter["baro_rate"] = true;
  filter["geom_rate"] = true;
  Sky sky;
  const AdsbQuery q = { lat, lon, AIRPORT_RADIUS_NM, &filter, consider, resetSky, &sky };
  if (!adsbWalk(q, err, err_n)) return false;

  s_next = AirportData();
  classify(sky, s_next);
  carryTrail(s_next.arr, s_prev_arr);
  carryTrail(s_next.dep, s_prev_dep);
  return true;   // nothing landing or leaving is an answer, not a failure
}

// ---- drawing (loop task) ----------------------------------------------------

static const uint16_t C_NUMBER = rgb565(210, 210, 210);
static const uint16_t C_PLACE  = rgb565(255, 120, 0);
static const uint16_t C_PATH   = rgb565(45, 45, 45);
static const uint16_t C_RUNWAY = rgb565(160, 160, 160);
static const uint16_t C_PLANE  = rgb565(255, 255, 255);
static const uint8_t  TRAIL_G = 220, TRAIL_B = 255;   // cyan, fading with age

constexpr int16_t TEXT_X = AIRLINE_LOGO_SIZE + 2;     // right of the mark
constexpr int16_t CHART_TOP = 2 * ROW_H;
constexpr int16_t CHART_H = MATRIX_H - CHART_TOP;
constexpr int16_t RUNWAY_W = 9;
constexpr int16_t PATH_STEP = 3;                      // a dot every third pixel

static uint32_t s_commit_ms = 0;   // when s_cur arrived
static bool     s_alt_dep = false; // alternating: the departure's turn
static uint32_t s_alt_ms = 0;
static uint32_t s_scroll_ms = 0;   // when the place started its scroll
static uint32_t s_frame_ms = 0;
static uint32_t s_drawn_key = 0;   // what the last draw showed, to skip the same

static int16_t chartX(float nm, bool arrival) {
  const float frac = fminf(fmaxf(nm, 0), CHART_NM) / CHART_NM;
  return (int16_t)lroundf((arrival ? 1 - frac : frac) * (MATRIX_W - 1));
}

static int16_t chartY(float ft) {
  const float frac = fminf(fmaxf(ft, 0), CHART_FT) / CHART_FT;
  return (int16_t)(MATRIX_H - 1 - lroundf(frac * (CHART_H - 2)));
}

// The plane where it is now: its last position, moved on at its speed and
// rate for the time since, GLIDE_MAX_MS at most.
static void glidePosition(const Movement &m, uint32_t now, float &nm, float &ft) {
  uint32_t dt = now - s_commit_ms;
  if (dt > GLIDE_MAX_MS) dt = GLIDE_MAX_MS;
  const float s = dt / 1000.0f;
  const float moved = m.gs * s / 3600.0f;
  nm = fmaxf(0, m.arrival ? m.dst - moved : m.dst + moved);
  ft = fmaxf(0, m.alt + m.rate * s / 60.0f);
}

static const Movement *shown() {
  const Movement *a = s_cur.arr.valid ? &s_cur.arr : nullptr;
  const Movement *d = s_cur.dep.valid ? &s_cur.dep : nullptr;
  if (g_ap_show == AP_SHOW_ARRIVALS) return a;
  if (!a || !d) return a ? a : d;
  if (g_ap_show == AP_SHOW_ALTERNATE) return s_alt_dep ? d : a;
  return a->dst <= d->dst ? a : d;   // nearest the runway
}

static void drawChart(const Movement *m, bool arrival, uint32_t now) {
  GFXcanvas16 &c = matrixCanvas();
  c.drawFastHLine(arrival ? MATRIX_W - RUNWAY_W : 0, MATRIX_H - 1, RUNWAY_W, C_RUNWAY);
  for (int16_t k = 0; k < MATRIX_W; k += PATH_STEP) {
    const float nm = (arrival ? MATRIX_W - 1 - k : k) * CHART_NM / (MATRIX_W - 1);
    const int16_t y = chartY(nm * GLIDE_FT_PER_NM);
    if (y < MATRIX_H - 1) c.drawPixel(chartX(nm, arrival), y, C_PATH);
  }
  if (!m) return;
  for (uint8_t i = 0; i < m->trail_n; i++) {
    const float k = 0.3f + 0.7f * (i + 1) / (m->trail_n + 1);
    c.drawPixel(chartX(m->trail_dst[i], arrival), chartY(m->trail_alt[i]),
                rgb565(0, (uint8_t)(TRAIL_G * k), (uint8_t)(TRAIL_B * k)));
  }
  float nm, ft;
  glidePosition(*m, now, nm, ft);
  const int16_t x = chartX(nm, arrival), y = chartY(ft);
  c.fillRect(arrival ? x : x - 1, y - 1, 2, 2, C_PLANE);
}

// The place's scroll, pixels from its start: still for SCROLL_HOLD_MS, a
// pixel every g_scroll_ms to its end, still again, then from the start.
static int16_t placeOffset(const char *place, uint32_t now) {
  const int16_t over = (int16_t)strlen(place) * CHAR_ADVANCE - (MATRIX_W - TEXT_X);
  if (over <= 0) return 0;
  const uint32_t step = g_scroll_ms > 0 ? (uint32_t)g_scroll_ms : 40;
  const uint32_t cycle = 2 * SCROLL_HOLD_MS + (uint32_t)over * step;
  const uint32_t t = (now - s_scroll_ms) % cycle;
  if (t < SCROLL_HOLD_MS) return 0;
  const uint32_t moving = (t - SCROLL_HOLD_MS) / step;
  return (int16_t)(moving < (uint32_t)over ? moving : over);
}

// Everything that decides the picture, packed: a new key, a new draw.
static uint32_t drawKey(const Movement *m, uint32_t now) {
  if (!m) return 1;
  float nm, ft;
  glidePosition(*m, now, nm, ft);
  return ((uint32_t)chartX(nm, m->arrival) << 24) ^ ((uint32_t)chartY(ft) << 16) ^
         ((uint32_t)placeOffset(m->place, now) << 4) ^ (m == &s_cur.dep ? 2u : 4u);
}

static void draw(uint32_t now) {
  GFXcanvas16 &c = matrixCanvas();
  c.fillScreen(0);
  const Movement *m = shown();
  s_drawn_key = drawKey(m, now);
  drawChart(m, m ? m->arrival : true, now);
  if (!m) {
    char code[sizeof(g_ap_code)];
    snprintf(code, sizeof(code), "%s", g_ap_code);
    for (char *p = code; *p; p++) *p = (char)toupper((unsigned char)*p);
    drawText(TEXT_X, 0, code, C_NUMBER);
    return;
  }
  drawText(TEXT_X, 0, m->number, C_NUMBER);
  drawText(TEXT_X - placeOffset(m->place, now), ROW_H, m->place, C_PLACE);
  // A scrolled place runs under the mark: cleared, and the mark on top.
  c.fillRect(0, ROW_H, TEXT_X, ROW_H, 0);
  drawAirlineMark(0, 0, m->airline);
}

static void airportEnter() {
  s_scroll_ms = s_alt_ms = millis();
  draw(millis());
}

static void airportCommit() {
  const bool same = s_cur.arr.valid == s_next.arr.valid && s_cur.dep.valid == s_next.dep.valid &&
                    !strcmp(s_cur.arr.hex, s_next.arr.hex) && !strcmp(s_cur.dep.hex, s_next.dep.hex);
  s_cur = s_next;
  s_commit_ms = millis();
  if (!same) s_scroll_ms = millis();   // a new plane's place starts its scroll again
  draw(millis());
}

static bool airportFrame(uint32_t now) {
  if (now - s_frame_ms < FRAME_MS) return false;
  s_frame_ms = now;
  if (g_ap_show == AP_SHOW_ALTERNATE && s_cur.arr.valid && s_cur.dep.valid &&
      now - s_alt_ms >= ALTERNATE_MS) {
    s_alt_dep = !s_alt_dep;
    s_alt_ms = s_scroll_ms = now;
  }
  if (drawKey(shown(), now) == s_drawn_key) return false;
  draw(now);
  return true;
}

const DisplayMode MODE_AIRPORT_OPS = {
  "airport", "glowingsquare/airport", FETCH_INTERVAL_MS,
  airportFetch, airportCommit, airportEnter, airportFrame,
};
