// Runs Glowing Square's real fetch and drawing code on a computer: each mode
// parses a recorded API answer (test/fixtures), commits it, and draws. The
// canvas is written out as raw RGB565 at the moments named below, for
// test_render.py to check and to turn into pictures.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <cassert>
#include <fstream>
#include <sstream>
#include <vector>

#include "project/gs_http.h"
#include "project/airline_logos.h"
#include "project/body_blocks.h"
#include "project/json_walk.h"
#include "project/tfl_rename.h"
#include "project/gs_state.h"
#include "project/matrix/matrix.h"
#include "project/modes/modes.h"
#include "project/render.h"

uint32_t g_fake_ms = 1000;
static std::string g_fixtures;
static std::string g_out;

static std::string slurp(const std::string &path) {
  std::ifstream f(path);
  if (!f) { fprintf(stderr, "missing fixture %s\n", path.c_str()); exit(2); }
  std::stringstream b; b << f.rdbuf(); return b.str();
}

// Every request answered from a file, picked by a piece of its URL. The
// filter is applied exactly as on the device.
static std::string g_last_url;
static std::vector<std::string> g_urls;   // every request, in order
static const char *g_down = nullptr;      // a host that refuses, as on the device
static int g_down_code = -1;              // its answer: a refusal, or an HTTP status
static std::vector<bool> g_http11;        // per request: asked over HTTP/1.1

// The fixture for a URL, or "" with err set: the refusal of a host that is
// down, or no fixture at all.
static std::string answerFor(const char *url, int &code, char *err, size_t n) {
  g_last_url = url;
  g_urls.push_back(url);
  if (g_down && strstr(url, g_down)) {
    if (g_down_code < 0) snprintf(err, n, "connection refused");
    else                 snprintf(err, n, "HTTP %d", g_down_code);
    code = g_down_code;
    return "";
  }
  static const struct { const char *part, *file; } MAP[] = {
    { "940GZZLUNHG?11nov",           "tfl_nhg_mislabelled.json" },
    { "api.tfl.gov.uk/StopPoint",    "tfl_nhg_stop.json" },
    { "api.tfl.gov.uk",              "tfl_victoria_oxc.json" },
    // Made-up skies, by the latitude asked. Each has the aircraft under both
    // feeds' keys ("ac" and "aircraft"), so either feed can be asked first.
    { "51.6001",                     "adsb_mixed_sky.json" },
    { "51.6002",                     "adsb_no_listed_airline.json" },
    { "api.adsb.lol",                "adsb_london.json" },
    { "opendata.adsb.fi",            "adsbfi_london.json" },
    { "api.adsbdb.com",              "adsbdb_THA917.json" },
    { "stat/sta",                    "unifi_sta.json" },
    // A UDM Pro's dashboard, wan_activity alone (12.11.2026, the ISP made
    // anonymous): 100 x 5 minutes for the graph, and since the 1st for the
    // month, whose first weeks are empty after that week's update.
    { "historySeconds=30000&",       "unifi_dash_graph.json" },
    { "aggregated-dashboard",        "unifi_dash_month.json" },
  };
  for (auto &m : MAP) {
    if (!strstr(url, m.part)) continue;
    code = 200;
    return slurp(g_fixtures + "/" + m.file);
  }
  snprintf(err, n, "no fixture for %s", url);
  code = 404;
  return "";
}

int gsHttpJson(const GsHttp &req, JsonDocument &out, const JsonDocument *filter, char *err, size_t n) {
  int code;
  const std::string body = answerFor(req.url, code, err, n);
  if (code != 200) return code;
  auto e = filter ? deserializeJson(out, body, DeserializationOption::Filter(*filter))
                  : deserializeJson(out, body);
  if (e) { snprintf(err, n, "JSON: %s", e.c_str()); return -2; }
  return 200;
}

// The device's walk over the fixture's bytes, read one at a time as off a
// socket, so json_walk.h is tested on the recorded answers themselves.
struct StringSource {
  const std::string &s;
  size_t pos = 0;
  int read() { return pos < s.size() ? (uint8_t)s[pos++] : -1; }
};

int gsHttpJsonEach(const GsHttp &req, const char *list, const JsonDocument &filter,
                   GsJsonEach each, void *ctx, char *err, size_t n) {
  g_http11.push_back(req.http11);
  int code;
  const std::string body = answerFor(req.url, code, err, n);
  if (code != 200) return code;
  StringSource src{body};
  auto e = jsonWalkArray(src, list, filter, [&](JsonObjectConst el) { each(el, ctx); });
  if (e) { snprintf(err, n, "JSON: %s", e.c_str()); return -2; }
  return 200;
}

// ---- json_walk.h on its own: the edges a recorded answer does not have ----
static int walkCount(const char *json, const char *key, DeserializationError *out = nullptr) {
  std::string s(json);
  StringSource src{s};
  JsonDocument filter;
  filter["k"] = true;
  int count = 0;
  auto e = jsonWalkArray(src, key, filter, [&](JsonObjectConst el) { count += el["k"] | 0; });
  if (out) *out = e;
  return count;
}

static void testJsonWalk() {
  DeserializationError e;
  assert(walkCount("{\"ac\":[{\"k\":1},{\"k\":2,\"x\":[1,{}]} , {\"k\":4}],\"n\":1}", "ac", &e) == 7 && !e);
  assert(walkCount("{\"msg\":\"ac\",\"ac\" : [ ]}", "ac", &e) == 0 && !e);       // "ac" as a value first
  assert(walkCount("{\"aircraft\":[{\"k\":1},{\"k\":2}", "aircraft", &e) == 3);  // cut off
  assert(e == DeserializationError::IncompleteInput);
  assert(walkCount(" [{\"k\":2},{\"k\":3}]", nullptr, &e) == 5 && !e);       // the answer is the array
  assert(walkCount("[]", nullptr, &e) == 0 && !e);
  walkCount("{\"now\":1}", "ac", &e);
  assert(e == DeserializationError::IncompleteInput);                            // never found
}

static void testBodyBlocks() {
  // Across several blocks, and walked from them as on the device.
  std::string json = "[";
  for (int i = 1; i <= 400; i++) json += std::string(i > 1 ? "," : "") + "{\"k\":1,\"pad\":\"................\"}";
  json += "]";
  assert(json.size() > 3 * BodyBlocks::BLOCK);
  BodyBlocks blocks(64 * 1024);
  for (size_t i = 0; i < json.size(); i += 333)
    assert(blocks.append((const uint8_t *)json.data() + i, std::min<size_t>(333, json.size() - i)));
  assert(blocks.size() == json.size());
  JsonDocument filter;
  filter["k"] = true;
  int sum = 0;
  assert(!jsonWalkArray(blocks, nullptr, filter, [&](JsonObjectConst el) { sum += el["k"] | 0; }));
  assert(sum == 400);
  assert(blocks.read() < 0);                               // and stays ended
  BodyBlocks small(10);                                    // over the cap: refused, nothing kept
  assert(small.append((const uint8_t *)"0123456789", 10));
  assert(!small.append((const uint8_t *)"x", 1));
  BodyBlocks none(10);
  assert(none.read() < 0);
}

static void testRename() {
  char out[40];
  const char *rules = "Hammersmith=Circle via Victoria; check front of train = Wimbledon";
  assert(tflRename("Hammersmith", rules, out, sizeof out) && !strcmp(out, "Circle via Victoria"));
  assert(tflRename("Check Front of Train", rules, out, sizeof out) && !strcmp(out, "Wimbledon"));
  assert(!tflRename("Edgware Road (Circle)", rules, out, sizeof out) && !strcmp(out, "Edgware Road (Circle)"));
  assert(!tflRename("Epping", "", out, sizeof out) && !strcmp(out, "Epping"));
  assert(!tflRename("Epping", "nonsense;=x; ;", out, sizeof out) && !strcmp(out, "Epping"));
  assert(tflRename("Tower Hill via Victoria", "x=y;tower hill=Circle", out, sizeof out) && !strcmp(out, "Circle"));
}

static void dump(const char *name) {
  std::ofstream f(g_out + "/" + name + ".rgb565", std::ios::binary);
  f.write((const char *)matrixCanvas().getBuffer(), MATRIX_W * MATRIX_H * 2);
}

static uint16_t px(int x, int y) { return matrixCanvas().getPixel(x, y); }
static bool rowHasColour(int y0, uint16_t c) {
  for (int y = y0; y < y0 + 8; y++) for (int x = 0; x < MATRIX_W; x++) if (px(x, y) == c) return true;
  return false;
}

static bool urlAsked(const char *part) {
  for (auto &u : g_urls) if (u.find(part) != std::string::npos) return true;
  return false;
}

// The airline's mark exactly, top left.
static bool markIs(const uint16_t *logo) {
  for (int y = 0; y < AIRLINE_LOGO_SIZE; y++)
    for (int x = 0; x < AIRLINE_LOGO_SIZE; x++)
      if (px(x, y) != logo[y * AIRLINE_LOGO_SIZE + x]) return false;
  return true;
}

// The callsign's prefix, and the curated list behind it.
static void testAirlines() {
  char p[4];
  airlinePrefix("BAW136", p);  assert(strcmp(p, "BAW") == 0);
  airlinePrefix("GKUBE", p);   assert(p[0] == '\0');   // a registration
  airlinePrefix("N123AB", p);  assert(p[0] == '\0');
  airlinePrefix("EZY8", p);    assert(strcmp(p, "EZY") == 0);
  airlinePrefix("", p);        assert(p[0] == '\0');
  assert(airlineLogo("BAW") && airlineLogo("SHT") == airlineLogo("BAW"));   // the Shuttle is BA
  assert(airlineLogo("EJU") == airlineLogo("EZY"));
  assert(airlineLogo("THA") && airlineLogo("THA") != airlineLogo("BAW"));
  assert(!airlineLogo("XYZ") && !airlineLogo(""));
  for (size_t i = 0; i < AIRLINE_LOGOS_N; i++) {
    assert(strlen(AIRLINE_LOGOS[i].prefix) == 3);
    bool lit = false;                                   // no mark is all black
    for (int k = 0; k < AIRLINE_LOGO_SIZE * AIRLINE_LOGO_SIZE; k++) lit |= AIRLINE_LOGOS[i].img[k] != 0;
    assert(lit);
  }
}

static void runFor(const DisplayMode &m, uint32_t ms) {
  const uint32_t end = g_fake_ms + ms;
  while (g_fake_ms < end) { g_fake_ms += 5; m.frame(g_fake_ms); }
}

static void fetchCommit(const DisplayMode &m) {
  char err[96] = "";
  if (!m.fetch(err, sizeof err)) { fprintf(stderr, "%s fetch failed: %s\n", m.key, err); exit(1); }
  m.commit();
}

int main(int argc, char **argv) {
  g_fixtures = argv[1];
  g_out = argv[2];
  renderBegin();

  // ---- Tube: Oxford Circus, Victoria line, southbound ----
  snprintf(g_tfl_stop, sizeof g_tfl_stop, "940GZZLUOXC");
  snprintf(g_tfl_line, sizeof g_tfl_line, "victoria");
  g_scroll_ms = 40;
  fetchCommit(MODE_TUBE_OPS);
  dump("tube_start");
  assert(rowHasColour(0, rgb565(255, 80, 0)));         // minutes, orange
  assert(rowHasColour(0, rgb565(150, 150, 150)));      // destination, grey
  runFor(MODE_TUBE_OPS, 1200);
  dump("tube_scrolling");
  runFor(MODE_TUBE_OPS, 60000);                        // every row has had its turn
  dump("tube_settled");
  for (int r = 0; r < 4; r++) assert(rowHasColour(r * 8, rgb565(255, 80, 0)));

  // Both directions and several lines: no empty "direction=" (TfL says 400).
  snprintf(g_tfl_line, sizeof g_tfl_line, "circle,district");
  g_tfl_direction = 0;
  fetchCommit(MODE_TUBE_OPS);
  assert(g_last_url.find("direction") == std::string::npos);
  assert(g_last_url.find("/Line/circle,district/Arrivals/") != std::string::npos);
  g_tfl_direction = 1;
  fetchCommit(MODE_TUBE_OPS);
  assert(g_last_url.find("?direction=inbound") != std::string::npos);

  // Every line at the stop, one platform only: Central (platform 3) and the
  // other side (platform 2) drop out, whatever line TfL gives the rest.
  g_tfl_line[0] = '\0';
  g_tfl_direction = 0;
  snprintf(g_tfl_plat_filter, sizeof g_tfl_plat_filter, "Platform 1");
  matrixCanvas().fillScreen(0);
  fetchCommit(MODE_TUBE_OPS);
  runFor(MODE_TUBE_OPS, 20000);
  dump("tube_platform_filter");
  assert(g_last_url.find("/StopPoint/") != std::string::npos);
  assert(rowHasColour(0, rgb565(255, 80, 0)) && rowHasColour(8, rgb565(255, 80, 0)));
  assert(!rowHasColour(16, rgb565(255, 80, 0)));       // only two trains from platform 1
  // TfL on 11.11.2026: every train on platform 1 as the Hammersmith & City,
  // the Circle to "Hammersmith" and the District as "Check Front of Train".
  // The Rename setting says what the platform's own sign says.
  snprintf(g_tfl_stop, sizeof g_tfl_stop, "940GZZLUNHG?11nov");   // the recorded answer
  snprintf(g_tfl_rename, sizeof g_tfl_rename, "Hammersmith=Circle via Victoria; Check Front of Train=Wimbledon");
  matrixCanvas().fillScreen(0);
  fetchCommit(MODE_TUBE_OPS);
  runFor(MODE_TUBE_OPS, 60000);
  dump("tube_renamed");
  for (int r = 0; r < 4; r++) assert(rowHasColour(r * 8, rgb565(255, 80, 0)));
  g_tfl_rename[0] = '\0';
  snprintf(g_tfl_stop, sizeof g_tfl_stop, "940GZZLUOXC");
  g_tfl_plat_filter[0] = '\0';
  snprintf(g_tfl_line, sizeof g_tfl_line, "victoria");

  testJsonWalk();
  testRename();
  testBodyBlocks();
  testAirlines();

  // ---- Flights: central London ----
  snprintf(g_fl_lat, sizeof g_fl_lat, "51.5074");
  snprintf(g_fl_lon, sizeof g_fl_lon, "-0.1278");
  g_fl_radius_nm = 10;
  g_fl_min_alt_ft = 500;
  MODE_FLIGHTS_OPS.enter();
  fetchCommit(MODE_FLIGHTS_OPS);
  dump("flights");
  assert(markIs(airlineLogo("THA")));                  // THA917: Thai's orchid
  assert(rowHasColour(0, rgb565(255, 120, 0)));        // LHR>BKK, amber
  assert(rowHasColour(8, rgb565(210, 210, 210)));      // TG917
  assert(rowHasColour(16, rgb565(0, 255, 0)));         // climbing at 2,304 ft/min
  assert(rowHasColour(16, rgb565(0, 220, 255)));       // 9,050ft, cyan
  assert(rowHasColour(24, rgb565(21, 58, 234)));       // 276kt, Unifi's blue
  assert(rowHasColour(24, rgb565(110, 10, 204)));      // B77W, Unifi's purple

  // adsb.lol down: the same flight from adsb.fi, which is then asked first.
  g_down = "api.adsb.lol";
  matrixCanvas().fillScreen(0);
  g_urls.clear();
  fetchCommit(MODE_FLIGHTS_OPS);
  assert(g_urls.size() >= 2 && g_urls[0].find("api.adsb.lol") != std::string::npos);
  assert(g_urls[1].find("opendata.adsb.fi/api/v2/lat/51.5074/lon/-0.1278/dist/10") != std::string::npos);
  assert(markIs(airlineLogo("THA")));                  // TG917 again
  g_urls.clear();
  fetchCommit(MODE_FLIGHTS_OPS);
  assert(g_urls[0].find("opendata.adsb.fi") != std::string::npos);
  g_down = nullptr;

  // adsb.lol up but refusing HTTP/1.0 ("426 Upgrade Required", 12.11.2026):
  // an error status is a failed feed, not a clear sky, so adsb.fi answers.
  // adsb.lol itself is asked over HTTP/1.1, adsb.fi over 1.0.
  g_down = "opendata.adsb.fi";                         // adsb.fi down: adsb.lol next
  g_urls.clear();
  g_http11.clear();
  fetchCommit(MODE_FLIGHTS_OPS);
  assert(g_urls.size() >= 2 && g_urls[1].find("api.adsb.lol") != std::string::npos);
  assert(!g_http11[0] && g_http11[1]);
  g_down = "api.adsb.lol";                             // and now adsb.lol says 426
  g_down_code = 426;
  matrixCanvas().fillScreen(0);
  g_urls.clear();
  fetchCommit(MODE_FLIGHTS_OPS);
  assert(g_urls.size() >= 2 && g_urls[0].find("api.adsb.lol") != std::string::npos);
  assert(g_urls[1].find("opendata.adsb.fi") != std::string::npos);
  assert(markIs(airlineLogo("THA")));                  // a flight, not a dark panel
  g_down = nullptr;
  g_down_code = -1;

  // An airline is shown over anything nearer that is not one: easyJet at
  // 6.5 nm over NetJets at 2 nm and a helicopter at 0.8 nm.
  snprintf(g_fl_lat, sizeof g_fl_lat, "51.6001");
  g_urls.clear();
  fetchCommit(MODE_FLIGHTS_OPS);
  dump("flights_airline_first");
  assert(markIs(airlineLogo("EZY")));
  assert(urlAsked("callsign/EZY12AB"));
  // No airline on the list: an airline-style callsign over a registration.
  snprintf(g_fl_lat, sizeof g_fl_lat, "51.6002");
  g_urls.clear();
  fetchCommit(MODE_FLIGHTS_OPS);
  assert(urlAsked("callsign/NJE123") && !urlAsked("callsign/GKUBE"));
  assert(px(0, 8) == rgb565(120, 120, 120));           // the plane, not a logo
  assert(flightRank("EZY12AB") == 2 && flightRank("NJE123") == 1 && flightRank("GKUBE") == 0);
  snprintf(g_fl_lat, sizeof g_fl_lat, "51.5074");

  // ---- Unifi ----
  snprintf(g_unifi_url, sizeof g_unifi_url, "https://192.168.1.1");
  snprintf(g_unifi_key, sizeof g_unifi_key, "test");
  g_unifi_auth = UNIFI_AUTH_KEY;
  MODE_UNIFI_OPS.enter();
  g_urls.clear();
  fetchCommit(MODE_UNIFI_OPS);
  dump("unifi_start");
  assert(urlAsked("/proxy/network/v2/api/site/default/aggregated-dashboard?historySeconds=30000&widgets=wan_activity"));
  {
    int columns = 0;                                   // full from the first fetch
    for (int x = 0; x < MATRIX_W; x++) columns += px(x, MATRIX_H - 1) == rgb565(21, 58, 234);
    assert(columns >= 60);
  }
  // The graph and the month are kept between fetches, not asked every time.
  g_urls.clear();
  g_fake_ms += 30000;
  fetchCommit(MODE_UNIFI_OPS);
  assert(urlAsked("stat/sta") && !urlAsked("aggregated-dashboard"));
  assert(rowHasColour(24, rgb565(21, 58, 234)));
  runFor(MODE_UNIFI_OPS, 1500);
  dump("unifi_new_client");

  // ---- Offline ----
  drawOffline();
  dump("offline");
  assert(px(63, 31) == rgb565(255, 0, 0));

  // ---- Firmware update: one held line, filling from the left ----
  {
    assert(updateBarFill(0, 0) == 0);                    // size not known: track only
    assert(updateBarFill(0, 1000) == 0);
    assert(updateBarFill(500, 1000) == UPDATE_BAR_W / 2);
    assert(updateBarFill(1000, 1000) == UPDATE_BAR_W);
    assert(updateBarFill(1200, 1000) == UPDATE_BAR_W);   // the multipart envelope overshoots
    drawUpdateBar(0);
    dump("update_start");
    drawUpdateBar(updateBarFill(3, 10));
    dump("update_30");
    const int16_t filled = updateBarFill(3, 10);
    for (int16_t y = 0; y < MATRIX_H; y++)
      for (int16_t x = 0; x < MATRIX_W; x++) {
        const uint16_t c = px(x, y);
        if (y != UPDATE_BAR_Y) { assert(c == 0); continue; }   // the held row alone
        const int16_t i = x - UPDATE_BAR_X;
        if (i < 0 || i >= UPDATE_BAR_W) assert(c == 0);
        else if (i < filled)            assert(c == rgb565(0, 255, 255));
        else                            assert(c == ((i % 2) == 0 ? rgb565(0, 0, 255) : 0));
      }
    drawUpdateBar(UPDATE_BAR_W);
    dump("update_done");
    assert(px(UPDATE_BAR_X + UPDATE_BAR_W - 1, UPDATE_BAR_Y) == rgb565(0, 255, 255));
  }

  // ---- ScrollRow: hold still, then scroll; text that fits never moves ----
  {
    matrixCanvas().fillScreen(0);
    ScrollRow row;
    row.start(8, "W:", "Glowing Square-1C70", 0xFFFF, 0xFFFF, 70, 4000, true);
    assert(!row.done());                                   // too long: it will scroll
    std::vector<uint16_t> before(matrixCanvas().getBuffer(), matrixCanvas().getBuffer() + 2048);
    for (uint32_t t = 0; t < 3900; t += 10) { g_fake_ms += 10; row.tick(g_fake_ms); }
    assert(std::equal(before.begin(), before.end(), matrixCanvas().getBuffer()));   // still held
    for (uint32_t t = 0; t < 400; t += 10) { g_fake_ms += 10; row.tick(g_fake_ms); }
    assert(!std::equal(before.begin(), before.end(), matrixCanvas().getBuffer()));  // moving now
    ScrollRow pass;
    pass.start(16, "P:", "hpyzw77u", 0xFFFF, 0xFFFF, 70, 4000, true);
    assert(pass.done());                                   // fits: drawn still, no scrolling
  }

  // ---- Brightness as people see it ----
  assert(matrixPerceptual(0) == 0);
  assert(matrixPerceptual(1) == 1);                      // dim, never dark
  assert(matrixPerceptual(255) == 255);
  assert(matrixPerceptual(128) > 50 && matrixPerceptual(128) < 60);   // half looks half
  for (int v = 1; v < 255; v++) assert(matrixPerceptual(v) <= matrixPerceptual(v + 1));

  printf("host render: ok\n");
  return 0;
}
