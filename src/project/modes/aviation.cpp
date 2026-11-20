// The ADS-B feeds, the adsbdb route cache and the airline marks, shared by
// the Flights and Airport modes. See aviation.h.
#include "project/modes/aviation.h"

#include <strings.h>

#include "project/airline_logos.h"
#include "project/matrix/matrix.h"

#define ROUTE_CACHE_N        16
// A callsign adsbdb does not know is asked again after this long: routes
// get added, and a flight number gets reused for a different route.
#define ROUTE_RETRY_MS       (60UL * 60UL * 1000UL)

// ---- the feeds (fetch task) ------------------------------------------------

// Two community feeds that answer in the same readsb format, tried in turn.
// adsb.lol went down for hours on 11.11.2026, its website with it, while
// adsb.fi answered throughout. The one that answered last is asked first, so
// a feed that is down costs one failed attempt, not one per fetch.
struct AdsbFeed {
  const char *url_fmt;   // latitude, longitude, radius in nautical miles
  const char *list;      // the key of the aircraft array
  bool        http11;    // refuses HTTP/1.0 (gs_http.h)
};
static const AdsbFeed FEEDS[] = {
  { "https://api.adsb.lol/v2/point/%.4f/%.4f/%d",              "ac",       true  },
  { "https://opendata.adsb.fi/api/v2/lat/%.4f/lon/%.4f/dist/%d", "aircraft", false },
};
static const size_t FEED_N = sizeof(FEEDS) / sizeof(FEEDS[0]);
static size_t s_feed = 0;

static bool askFeed(const AdsbFeed &feed, const AdsbQuery &q, char *err, size_t err_n) {
  char url[128];
  snprintf(url, sizeof(url), feed.url_fmt, q.lat, q.lon, q.radius_nm);
  GsHttp req;
  req.url = url;
  req.http11 = feed.http11;
  if (q.reset) q.reset(q.ctx);
  return gsHttpOk(gsHttpJsonEach(req, feed.list, *q.filter, q.each, q.ctx, err, err_n));
}

bool adsbWalk(const AdsbQuery &q, char *err, size_t err_n) {
  for (size_t tried = 0; tried < FEED_N; tried++) {
    if (tried) s_feed = (s_feed + 1) % FEED_N;
    if (askFeed(FEEDS[s_feed], q, err, err_n)) return true;
  }
  return false;
}

// ---- the route cache (fetch task) --------------------------------------------

static Route   s_routes[ROUTE_CACHE_N];
static uint8_t s_route_next = 0;

static const Route *cached(const char *callsign) {
  for (const Route &r : s_routes) {
    if (!r.callsign[0] || strcmp(r.callsign, callsign) != 0) continue;
    if (r.known || millis() - r.at_ms < ROUTE_RETRY_MS) return &r;
  }
  return nullptr;
}

// One end of adsbdb's flightroute into the route's fields for it.
static void routeEnd(JsonObjectConst a, char *code, char *icao, char *city) {
  const char *iata = a["iata_code"] | "";
  const char *i4 = a["icao_code"] | "";
  snprintf(code, 5, "%s", iata[0] ? iata : i4);
  snprintf(icao, 5, "%s", i4);
  snprintf(city, sizeof(Route::from_city), "%s", a["municipality"] | "");
}

// adsbdb's answer for `as` into r. False when it has none.
static bool lookUp(Route &r, const char *as) {
  char url[96];
  snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", as);
  JsonDocument filter;
  JsonObject fr = filter["response"]["flightroute"].to<JsonObject>();
  fr["callsign_iata"] = true;
  for (const char *end : { "origin", "destination" }) {
    fr[end]["iata_code"] = true;
    fr[end]["icao_code"] = true;
    fr[end]["municipality"] = true;
  }
  JsonDocument doc;
  char err[48];
  GsHttp req;
  req.url = url;
  req.buffer_max = GS_HTTP_BUFFER_MAX;
  if (!gsHttpOk(gsHttpJson(req, doc, &filter, err, sizeof(err)))) return false;
  JsonObjectConst route = doc["response"]["flightroute"];
  if (route.isNull()) return false;
  routeEnd(route["origin"], r.from, r.from_icao, r.from_city);
  routeEnd(route["destination"], r.to, r.to_icao, r.to_city);
  snprintf(r.number, sizeof(r.number), "%s", route["callsign_iata"] | "");
  r.known = r.from[0] && r.to[0];
  return r.known;
}

// As broadcast first, then without the number's leading zeros: Aeromexico's
// AMX008 is unknown to adsbdb, its AMX8 is London to Mexico City.
static void ask(Route &r) {
  if (lookUp(r, r.callsign)) return;
  char unpadded[sizeof(r.callsign)];
  if (callsignUnpadded(r.callsign, unpadded, sizeof(unpadded))) lookUp(r, unpadded);
}

bool callsignUnpadded(const char *callsign, char *out, size_t n) {
  if (!callsign || strlen(callsign) < 5) return false;
  for (int i = 0; i < 3; i++)
    if (callsign[i] < 'A' || callsign[i] > 'Z') return false;
  size_t i = 3;
  while (callsign[i] == '0' && callsign[i + 1] >= '0' && callsign[i + 1] <= '9') i++;
  if (i == 3) return false;
  snprintf(out, n, "%.3s%s", callsign, callsign + i);
  return true;
}

const Route *routeFor(const char *callsign, bool ask_now) {
  const Route *hit = cached(callsign);
  if (hit || !ask_now) return hit;
  Route &r = s_routes[s_route_next];
  s_route_next = (uint8_t)((s_route_next + 1) % ROUTE_CACHE_N);
  memset(&r, 0, sizeof(r));
  snprintf(r.callsign, sizeof(r.callsign), "%s", callsign);
  r.at_ms = millis();
  ask(r);
  return &r;
}

bool routeEndIs(const char *code, const char *iata, const char *icao) {
  if (!code || !code[0]) return false;
  return (iata && iata[0] && strcasecmp(code, iata) == 0) ||
         (icao && icao[0] && strcasecmp(code, icao) == 0);
}

void trimRight(char *s) {
  for (size_t n = strlen(s); n && s[n - 1] == ' '; n--) s[n - 1] = '\0';
}

// ---- the mark (loop task) ----------------------------------------------------------

static const uint16_t C_PLANE = rgb565(120, 120, 120);

// An airline not on the list (or a light aircraft): a plane, from above.
static const char *const PLANE_ART[AIRLINE_LOGO_SIZE] = {
  "................",
  ".......##.......",
  "......####......",
  "......####......",
  "......####......",
  ".....######.....",
  "...##########...",
  ".##############.",
  "################",
  "......####......",
  "......####......",
  "......####......",
  ".....######.....",
  "....########....",
  "................",
  "................",
};

void drawAirlineMark(int16_t x, int16_t y, const char *prefix) {
  GFXcanvas16 &cv = matrixCanvas();
  const uint16_t *logo = airlineLogo(prefix);
  for (int16_t j = 0; j < AIRLINE_LOGO_SIZE; j++)
    for (int16_t i = 0; i < AIRLINE_LOGO_SIZE; i++) {
      if (logo) cv.drawPixel(x + i, y + j, pgm_read_word(&logo[j * AIRLINE_LOGO_SIZE + i]));
      else if (PLANE_ART[j][i] == '#') cv.drawPixel(x + i, y + j, C_PLANE);
    }
}
