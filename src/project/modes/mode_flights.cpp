// Flights: the aircraft nearest overhead, from adsb.lol, with its route from
// adsbdb.com (both free, no key). Replaces v1's Flightradar24 feed, an
// unofficial endpoint that came with no promise to keep working.
//
// Layout as v1:
//   FROM>TO        cyan
//   flight number  white
//   altitude ft    green above 10,000 ft, red below
//   speed kts      white, then the aircraft type in cyan
#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>

#include "project/gs_http.h"
#include "project/gs_state.h"
#include "project/modes/modes.h"
#include "project/render.h"

#define ROUTE_CACHE_N        16
// A callsign adsbdb does not know is asked again after this long: routes
// get added, and a flight number gets reused for a different route.
#define ROUTE_RETRY_MS       (60UL * 60UL * 1000UL)
#define HIGH_ALTITUDE_FT     10000

struct Flight {
  bool     valid = false;
  char     from[5] = "", to[5] = "";
  char     number[10] = "";
  char     aircraft[8] = "";
  uint32_t speed = 0, altitude = 0;
};

static Flight s_next, s_cur;

// ---- route cache (fetch task only) -----------------------------------------

struct Route {
  char     callsign[10];
  char     number[10];
  char     from[5], to[5];
  bool     known;
  uint32_t at_ms;
};
static Route s_routes[ROUTE_CACHE_N];
static uint8_t s_route_next = 0;

static const Route *routeFor(const char *callsign) {
  for (const Route &r : s_routes) {
    if (!r.callsign[0] || strcmp(r.callsign, callsign) != 0) continue;
    if (r.known || millis() - r.at_ms < ROUTE_RETRY_MS) return &r;
  }

  Route &r = s_routes[s_route_next];
  s_route_next = (uint8_t)((s_route_next + 1) % ROUTE_CACHE_N);
  memset(&r, 0, sizeof(r));
  snprintf(r.callsign, sizeof(r.callsign), "%s", callsign);
  r.at_ms = millis();

  char url[96];
  snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", callsign);
  JsonDocument filter;
  JsonObject fr = filter["response"]["flightroute"].to<JsonObject>();
  fr["callsign_iata"] = true;
  fr["origin"]["iata_code"] = true;
  fr["origin"]["icao_code"] = true;
  fr["destination"]["iata_code"] = true;
  fr["destination"]["icao_code"] = true;
  JsonDocument doc;
  char err[48];
  GsHttp req;
  req.url = url;
  req.buffer_max = GS_HTTP_BUFFER_MAX;
  if (gsHttpJson(req, doc, &filter, err, sizeof(err)) >= 200) {
    JsonObjectConst route = doc["response"]["flightroute"];
    if (!route.isNull()) {
      auto code = [](JsonObjectConst a) {
        const char *iata = a["iata_code"] | "";
        return iata[0] ? iata : (const char *)(a["icao_code"] | "");
      };
      snprintf(r.from, sizeof(r.from), "%s", code(route["origin"]));
      snprintf(r.to, sizeof(r.to), "%s", code(route["destination"]));
      snprintf(r.number, sizeof(r.number), "%s", route["callsign_iata"] | "");
      r.known = r.from[0] && r.to[0];
    }
  }
  return &r;
}

// ---- fetch (worker task) ----------------------------------------------------

static void trimRight(char *s) {
  for (size_t n = strlen(s); n && s[n - 1] == ' '; n--) s[n - 1] = '\0';
}

static bool flightsFetch(char *err, size_t err_n) {
  const float lat = atof(g_fl_lat), lon = atof(g_fl_lon);
  if (!g_fl_lat[0] || !g_fl_lon[0] || fabsf(lat) > 90 || fabsf(lon) > 180) {
    snprintf(err, err_n, "no position set");
    return false;
  }
  char url[128];
  snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.4f/%.4f/%d",
           lat, lon, (int)g_fl_radius_nm);

  JsonDocument filter;
  JsonObject f = filter["ac"][0].to<JsonObject>();
  f["flight"] = true;
  f["alt_baro"] = true;
  f["gs"] = true;
  f["t"] = true;
  f["dst"] = true;

  JsonDocument doc;
  GsHttp req;
  req.url = url;
  req.buffer_max = GS_HTTP_BUFFER_MAX;
  if (gsHttpJson(req, doc, &filter, err, err_n) < 0) return false;

  // The nearest one in the air, high enough, with a callsign to look up.
  JsonObjectConst best;
  float best_dst = 1e9f;
  for (JsonObjectConst a : doc["ac"].as<JsonArrayConst>()) {
    if (!a["alt_baro"].is<int>()) continue;              // "ground"
    if ((int32_t)(a["alt_baro"] | 0) < g_fl_min_alt_ft) continue;
    if (!(a["flight"] | "")[0]) continue;
    const float d = a["dst"] | 1e8f;
    if (d < best_dst) { best_dst = d; best = a; }
  }

  s_next = Flight();
  if (best.isNull()) return true;     // a clear sky is an answer, not a failure

  char callsign[10];
  snprintf(callsign, sizeof(callsign), "%s", best["flight"] | "");
  trimRight(callsign);
  const Route *r = routeFor(callsign);

  s_next.valid = true;
  s_next.altitude = best["alt_baro"] | 0;
  s_next.speed = (uint32_t)lroundf(best["gs"] | 0.0f);
  snprintf(s_next.aircraft, sizeof(s_next.aircraft), "%s", best["t"] | "");
  snprintf(s_next.number, sizeof(s_next.number), "%s", r->number[0] ? r->number : callsign);
  snprintf(s_next.from, sizeof(s_next.from), "%s", r->known ? r->from : "");
  snprintf(s_next.to, sizeof(s_next.to), "%s", r->known ? r->to : "");
  return true;
}

// ---- drawing (loop task) ----------------------------------------------------

static void draw() {
  GFXcanvas16 &c = matrixCanvas();
  c.fillScreen(0);
  // Nothing overhead: a dark display. v1 kept the last flight standing,
  // which read as a plane that never moved.
  if (!s_cur.valid) return;

  const uint16_t CYAN = rgb565(0, 255, 255), WHITE = rgb565(255, 255, 255);
  char line[24];
  // A route adsbdb does not know: the callsign takes the first line too.
  if (s_cur.from[0]) snprintf(line, sizeof(line), "%s>%s", s_cur.from, s_cur.to);
  else               snprintf(line, sizeof(line), "%s", s_cur.number);
  drawText(0, 0, line, CYAN);
  drawText(0, 8, s_cur.number, WHITE);
  snprintf(line, sizeof(line), "%uft", (unsigned)s_cur.altitude);
  drawText(0, 16, line, s_cur.altitude > HIGH_ALTITUDE_FT ? rgb565(0, 255, 0) : rgb565(255, 0, 0));
  c.setCursor(0, 24);
  c.setTextColor(WHITE);
  c.print(s_cur.speed);
  c.print("kts");
  c.setTextColor(CYAN);
  c.print(s_cur.aircraft);
}

static void flightsEnter()  { draw(); }
static void flightsCommit() { s_cur = s_next; draw(); }
static bool flightsFrame(uint32_t) { return false; }

const DisplayMode MODE_FLIGHTS_OPS = {
  "flights", "glowingsquare/flight", 10000,
  flightsFetch, flightsCommit, flightsEnter, flightsFrame,
};
