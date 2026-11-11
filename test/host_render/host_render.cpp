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

int gsHttpJson(const GsHttp &req, JsonDocument &out, const JsonDocument *filter, char *err, size_t n) {
  g_last_url = req.url;
  static const struct { const char *part, *file; } MAP[] = {
    { "api.tfl.gov.uk/StopPoint",    "tfl_nhg_stop.json" },
    { "api.tfl.gov.uk",              "tfl_victoria_oxc.json" },
    { "api.adsb.lol",                "adsb_london.json" },
    { "api.adsbdb.com",              "adsbdb_THA917.json" },
    { "stat/sta",                    "unifi_sta.json" },
    { "stat/report/5minutes.site",   "unifi_5min.json" },
    { "stat/report/daily.site",      "unifi_daily.json" },
  };
  for (auto &m : MAP) {
    if (!strstr(req.url, m.part)) continue;
    const std::string body = slurp(g_fixtures + "/" + m.file);
    auto e = filter ? deserializeJson(out, body, DeserializationOption::Filter(*filter))
                    : deserializeJson(out, body);
    if (e) { snprintf(err, n, "JSON: %s", e.c_str()); return -2; }
    return 200;
  }
  snprintf(err, n, "no fixture for %s", req.url);
  return 404;
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
  g_tfl_plat_filter[0] = '\0';
  snprintf(g_tfl_line, sizeof g_tfl_line, "victoria");

  // ---- Flights: central London ----
  snprintf(g_fl_lat, sizeof g_fl_lat, "51.5074");
  snprintf(g_fl_lon, sizeof g_fl_lon, "-0.1278");
  g_fl_radius_nm = 10;
  g_fl_min_alt_ft = 500;
  MODE_FLIGHTS_OPS.enter();
  fetchCommit(MODE_FLIGHTS_OPS);
  dump("flights");
  assert(rowHasColour(0, rgb565(0, 255, 255)));        // LHR>BKK
  assert(rowHasColour(16, rgb565(255, 0, 0)));         // 9050ft: below 10,000, red

  // ---- Unifi ----
  snprintf(g_unifi_url, sizeof g_unifi_url, "https://192.168.1.1");
  snprintf(g_unifi_key, sizeof g_unifi_key, "test");
  g_unifi_auth = UNIFI_AUTH_KEY;
  MODE_UNIFI_OPS.enter();
  fetchCommit(MODE_UNIFI_OPS);
  dump("unifi_start");
  assert(rowHasColour(24, rgb565(21, 58, 234)));       // the graph
  runFor(MODE_UNIFI_OPS, 1500);
  dump("unifi_new_client");

  // ---- Offline ----
  drawOffline();
  dump("offline");
  assert(px(63, 31) == rgb565(255, 0, 0));

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

  printf("host render: ok\n");
  return 0;
}
