// Unifi: network statistics straight from the UniFi console. v1 needed a PHP
// script on a server between the two, because the answers were too big for
// the ESP32's RAM. Each answer's "data" array is walked straight off the
// socket an element at a time (gsHttpJsonEach, GsHttp::stream), keeping only
// the handful of fields drawn, however many clients there are. Parsed by
// ArduinoJson off the socket at its own pace instead, the client list
// arrived cut off (IncompleteInput, 12.11.2026), as TfL's had.
//
// What it reads, all through the Network application's API:
//   stat/sta     clients: count, guests, the newest one
//   stat/health  the WAN's rates now, down and up, bytes a second
//
// The graph and the month's totals are built here from those rates, a
// sample every fetch. v1 read them from stat/report/5minutes.site and
// daily.site, which a UniFi Network update in November 2026 left empty on
// a UDM Pro, for v1's script as much as this. So the graph starts empty at
// power-on and fills across the panel in 64 x 5 minutes; the month's totals
// are kept in NVS and count from the first month this ran. Both are
// estimates: a rate sampled every 30 s, times 30 s.
//
// Sign-in: an API key (UniFi OS consoles, the X-API-KEY header), or a user
// and password for a console or a classic controller.
//
// Layout as v1:
//   row 0   down icon + month download (orange)   up icon + month upload (right, purple)
//   row 8   clients icon + count, guest icon + count
//   row 16  "New:" and the name of a client that joined in the last 2 minutes
//   24..31  the download graph, 8 px tall
#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

#include "hardware/sd_logger.h"
#include "project/gs_http.h"
#include "project/gs_state.h"
#include "project/icons.h"
#include "project/modes/modes.h"
#include "project/render.h"
#include "services/prefs_store.h"

#define GRAPH_HEIGHT       8
#define GRAPH_SCALE_POINTS 100   // v1: the scale comes from the last 100 buckets
#define BUCKET_MS          (5UL * 60UL * 1000UL)
#define NEW_CLIENT_S       120   // v1: INFO_UPDATE_INTERVAL * 4 / 1000
#define NEW_SCROLL_MS      50    // v1 scrolled this row at 50 ms a pixel
// A gap between two samples longer than this (failed fetches, a restart of
// the console) counts as this long: the rate then says little about the gap.
#define SAMPLE_MAX_MS      (90UL * 1000UL)
#define MONTH_SAVE_MS      (15UL * 60UL * 1000UL)
#define BYTES_PER_MB       1000000.0

struct UnifiData {
  char     month_rx_text[16] = "-", month_tx_text[16] = "-";
  uint16_t clients = 0, guests = 0;
  uint8_t  graph[MATRIX_W] = {0};
  char     newest[40] = "";
  uint32_t min_uptime = UINT32_MAX;
  int32_t  month = -1;              // year * 12 + month, -1 before the clock is set
  double   month_rx = 0, month_tx = 0;   // bytes this month since power-on
  char     warn[96] = "";   // an extra that failed, logged by commit()
};

static UnifiData s_next, s_cur;

// ---- byte formatting, as v1's formatBytes($bytes, 0) ------------------------

// 1000 based. "123G" when the number has three digits or more, "12GB" when it
// has fewer; terabytes get one decimal.
static void formatBytes(double bytes, char *out, size_t n) {
  static const char *LONG[]  = { "B", "KB", "MB", "GB", "TB" };
  static const char *SHORT[] = { "B", "K", "M", "G", "T" };
  if (bytes < 0) bytes = 0;
  int pow = bytes >= 1 ? (int)floor(log(bytes) / log(1000.0)) : 0;
  if (pow > 4) pow = 4;
  if (pow < 0) pow = 0;
  const double v = bytes / ::pow(1000.0, pow);
  char num[16];
  snprintf(num, sizeof(num), "%.*f", pow == 4 ? 1 : 0, v);
  snprintf(out, n, "%s%s", num, strlen(num) > 2 ? SHORT[pow] : LONG[pow]);
}

// ---- sign-in ------------------------------------------------------------------

static String s_cookie;   // the session, for the two password modes
static String s_csrf;

static bool isClassic() { return g_unifi_auth == UNIFI_AUTH_CLASSIC; }

static void base(char *out, size_t n) {
  snprintf(out, n, "%s", g_unifi_url);
  size_t len = strlen(out);
  while (len && out[len - 1] == '/') out[--len] = '\0';
}

// "TOKEN=abc; Path=/; HttpOnly" -> "TOKEN=abc". Only the first cookie: both
// login answers carry the one that matters first.
static String firstCookie(const String &set_cookie) {
  const int semi = set_cookie.indexOf(';');
  return semi < 0 ? set_cookie : set_cookie.substring(0, semi);
}

static bool login(char *err, size_t err_n) {
  char url[128], b[64];
  base(b, sizeof(b));
  snprintf(url, sizeof(url), "%s%s", b, isClassic() ? "/api/login" : "/api/auth/login");
  JsonDocument body;
  body["username"] = g_unifi_user;
  body["password"] = g_unifi_pass;
  String json;
  serializeJson(body, json);

  JsonDocument doc, filter;
  filter["meta"]["rc"] = true;
  String cookie, csrf;
  GsHttp req;
  req.url = url;
  req.method = "POST";
  req.body = json.c_str();
  req.verify_tls = g_unifi_verify_tls;
  req.set_cookie = &cookie;
  req.csrf = &csrf;
  if (!gsHttpOk(gsHttpJson(req, doc, &filter, err, err_n)) || !cookie.length()) {
    snprintf(err, err_n, "%s", err[0] ? err : "login refused");
    return false;
  }
  s_cookie = firstCookie(cookie);
  s_csrf = csrf;
  return true;
}

// One call to the Network application: its "data" array, an element at a
// time through `filter`, to `each`.
struct ApiCall {
  const char         *path;
  const char         *body;     // POSTed when set
  const JsonDocument *filter;   // for one element of "data"
  GsJsonEach          each;
  void               *ctx;
};

// A refused session is renewed once.
static int api(const ApiCall &call, char *err, size_t err_n) {
  char url[192], b[64];
  base(b, sizeof(b));
  snprintf(url, sizeof(url), "%s%s/api/s/%s/%s", b, isClassic() ? "" : "/proxy/network",
           g_unifi_site, call.path);

  for (int attempt = 0; attempt < 2; attempt++) {
    if (g_unifi_auth != UNIFI_AUTH_KEY && !s_cookie.length() && !login(err, err_n)) return -1;
    GsHttp req;
    req.url = url;
    req.method = call.body ? "POST" : "GET";
    req.body = call.body;
    req.verify_tls = g_unifi_verify_tls;
    req.stream = true;   // the client list can be more than RAM
    int h = 0;
    if (g_unifi_auth == UNIFI_AUTH_KEY) {
      req.headers[h++] = "X-API-KEY";
      req.headers[h++] = g_unifi_key;
    } else {
      req.headers[h++] = "Cookie";
      req.headers[h++] = s_cookie.c_str();
      if (s_csrf.length()) { req.headers[h++] = "X-CSRF-Token"; req.headers[h++] = s_csrf.c_str(); }
    }
    const int code = gsHttpJsonEach(req, "data", *call.filter, call.each, call.ctx, err, err_n);
    if ((code == 401 || code == 403) && g_unifi_auth != UNIFI_AUTH_KEY && attempt == 0) {
      s_cookie = "";
      continue;
    }
    return code;
  }
  return -1;
}

// ---- fetch (worker task) ----------------------------------------------------

// One client: counted, and kept as the newest if it joined last.
static void countClient(JsonObjectConst c, void *ctx) {
  UnifiData &d = *(UnifiData *)ctx;
  if (c["is_guest"] | false) d.guests++;
  else                       d.clients++;
  const uint32_t up = c["uptime"] | UINT32_MAX;
  if (up >= d.min_uptime) return;
  d.min_uptime = up;
  const char *n = c["hostname"] | "";
  if (!n[0]) n = c["name"] | "";
  snprintf(d.newest, sizeof(d.newest), "%s", n);
}

static bool readClients(char *err, size_t err_n) {
  JsonDocument filter;
  filter["hostname"] = true;
  filter["name"] = true;
  filter["uptime"] = true;
  filter["is_guest"] = true;
  const ApiCall call = { "stat/sta", nullptr, &filter, countClient, &s_next };
  return gsHttpOk(api(call, err, err_n));
}

// ---- the WAN, sampled (fetch task) -------------------------------------------

struct WanRates {
  bool  found = false;
  float rx = 0, tx = 0;   // bytes a second
};

static void wanRates(JsonObjectConst sub, void *ctx) {
  if (strcmp(sub["subsystem"] | "", "wan") != 0) return;
  WanRates &r = *(WanRates *)ctx;
  r.found = true;
  r.rx = sub["rx_bytes-r"] | 0.0f;
  r.tx = sub["tx_bytes-r"] | 0.0f;
}

static bool readRates(WanRates &rates, char *err, size_t err_n) {
  JsonDocument filter;
  filter["subsystem"] = true;
  filter["rx_bytes-r"] = true;
  filter["tx_bytes-r"] = true;
  const ApiCall call = { "stat/health", nullptr, &filter, wanRates, &rates };
  if (!gsHttpOk(api(call, err, err_n))) return false;
  if (!rates.found) snprintf(err, err_n, "no WAN in stat/health");
  return rates.found;
}

// Down bytes per completed 5-minute bucket, the last GRAPH_SCALE_POINTS in a
// ring, and the month so far. The fetch task's alone; the month reaches the
// loop through s_next.
struct WanHistory {
  float    bucket[GRAPH_SCALE_POINTS];
  uint16_t done = 0;        // buckets completed
  uint32_t index = 0;       // millis() / BUCKET_MS of the one being filled
  double   cur = 0;         // down bytes in it so far
  uint32_t last_ms = 0;     // the last sample, 0 before the first
  int32_t  month = -1;
  double   month_rx = 0, month_tx = 0;
};
static WanHistory s_wan;

static int32_t monthNow() {
  const time_t now = time(nullptr);
  if (now < 1700000000) return -1;   // no clock yet
  struct tm t;
  localtime_r(&now, &t);
  return (t.tm_year + 1900) * 12 + t.tm_mon;
}

// Closes the buckets that ended before `now_ms`. A long gap closes at most a
// graph's worth: the rest would only be empty buckets scrolled off again.
static void closeBuckets(uint32_t now_ms) {
  const uint32_t index = now_ms / BUCKET_MS;
  for (int k = 0; s_wan.index < index && k < GRAPH_SCALE_POINTS; k++, s_wan.index++) {
    s_wan.bucket[s_wan.done % GRAPH_SCALE_POINTS] = (float)s_wan.cur;
    s_wan.done++;
    s_wan.cur = 0;
  }
  s_wan.index = index;
}

static void addSample(const WanRates &r, uint32_t now_ms) {
  const int32_t m = monthNow();
  if (m >= 0 && m != s_wan.month) { s_wan.month = m; s_wan.month_rx = s_wan.month_tx = 0; }
  if (!s_wan.last_ms) {   // the first: a starting point, nothing to add yet
    s_wan.last_ms = now_ms ? now_ms : 1;
    s_wan.index = now_ms / BUCKET_MS;
    return;
  }
  uint32_t dt = now_ms - s_wan.last_ms;
  if (dt > SAMPLE_MAX_MS) dt = SAMPLE_MAX_MS;
  s_wan.last_ms = now_ms ? now_ms : 1;
  const double down = (double)r.rx * dt / 1000.0, up = (double)r.tx * dt / 1000.0;
  closeBuckets(now_ms);
  s_wan.cur += down;
  if (m < 0) return;   // no clock yet: no month to count it in
  s_wan.month_rx += down;
  s_wan.month_tx += up;
}

// Bucket i of the last `count`, oldest first.
static float bucketAt(int count, int i) {
  return s_wan.bucket[(s_wan.done - count + i) % GRAPH_SCALE_POINTS];
}

// The graph from the completed buckets, newest last: v1's scale (the largest
// of the last 100) and its slice (the newest 64, from the left edge).
static void graphInto(UnifiData &d) {
  const int n = s_wan.done < GRAPH_SCALE_POINTS ? s_wan.done : GRAPH_SCALE_POINTS;
  float max_bytes = 0;
  for (int i = 0; i < n; i++) max_bytes = fmaxf(max_bytes, bucketAt(n, i));
  const int shown = n < MATRIX_W ? n : MATRIX_W;
  for (int x = 0; x < shown; x++) {
    const float b = bucketAt(n, n - shown + x);
    d.graph[x] = max_bytes > 0 ? (uint8_t)ceilf(b / max_bytes * GRAPH_HEIGHT) : 0;
  }
}

static bool unifiFetch(char *err, size_t err_n) {
  if (!g_unifi_url[0]) { snprintf(err, err_n, "no console address set"); return false; }
  if (g_unifi_auth == UNIFI_AUTH_KEY && !g_unifi_key[0]) { snprintf(err, err_n, "no API key set"); return false; }
  s_next = UnifiData();
  if (!readClients(err, err_n)) return false;
  // The rates are extras: a console that refuses them still shows the
  // clients, and commit() logs why (the log is the loop task's).
  WanRates rates;
  char e2[64];
  if (readRates(rates, e2, sizeof(e2))) addSample(rates, millis());
  else snprintf(s_next.warn, sizeof(s_next.warn), "WAN rates unavailable (%s)", e2);
  graphInto(s_next);
  s_next.month = s_wan.month;
  s_next.month_rx = s_wan.month_rx;
  s_next.month_tx = s_wan.month_tx;
  return true;
}

// ---- the month, kept (loop task) ----------------------------------------------

// What NVS held at power-on for its month, in MB; this run's bytes are added
// to it. Saved every MONTH_SAVE_MS, so a restart loses at most that much.
static int32_t  s_base_month = -2;   // -2: not read yet
static uint32_t s_base_rx_mb = 0, s_base_tx_mb = 0;
static uint32_t s_month_saved_ms = 0;

static void monthTotals(UnifiData &d) {
  if (s_base_month == -2) {
    s_base_month = prefsGetInt("uf_month", -1);
    s_base_rx_mb = prefsGetUInt("uf_rx_mb", 0);
    s_base_tx_mb = prefsGetUInt("uf_tx_mb", 0);
  }
  if (d.month < 0) return;   // no clock yet: the totals stay "-"
  if (d.month != s_base_month) {   // a new month, or the first ever
    s_base_month = d.month;
    s_base_rx_mb = s_base_tx_mb = 0;
    s_month_saved_ms = 0;
  }
  const double rx = s_base_rx_mb * BYTES_PER_MB + d.month_rx;
  const double tx = s_base_tx_mb * BYTES_PER_MB + d.month_tx;
  formatBytes(rx, d.month_rx_text, sizeof(d.month_rx_text));
  formatBytes(tx, d.month_tx_text, sizeof(d.month_tx_text));
  if (s_month_saved_ms && millis() - s_month_saved_ms < MONTH_SAVE_MS) return;
  s_month_saved_ms = millis() ? millis() : 1;
  const bool ok = prefsPutInt("uf_month", d.month) &&
                  prefsPutUInt("uf_rx_mb", (uint32_t)(rx / BYTES_PER_MB)) &&
                  prefsPutUInt("uf_tx_mb", (uint32_t)(tx / BYTES_PER_MB));
  if (!ok) logSD("Unifi: the month's totals could not be saved");
}

// ---- drawing (loop task) ----------------------------------------------------

static ScrollRow s_new;

static void draw() {
  GFXcanvas16 &c = matrixCanvas();
  c.fillScreen(0);
  s_new.stop();

  drawIcon(0, 0, 5, 7, DOWN_ICON);
  drawText(6, 0, s_cur.month_rx_text, rgb565(250, 50, 12));
  drawIcon(MATRIX_W - 5, 0, 5, 7, UP_ICON);
  drawRightAligned(MATRIX_W - 5, 0, s_cur.month_tx_text, rgb565(110, 10, 204));

  const uint16_t C_CLIENTS = rgb565(247, 20, 12);
  char num[8];
  drawIcon(0, 8, 7, 7, CLIENT_ICON);
  snprintf(num, sizeof(num), "%u", (unsigned)s_cur.clients);
  drawText(9, 8, num, C_CLIENTS);
  const int16_t gx = 14 + (int16_t)strlen(num) * 5;   // v1's placement
  drawIcon(gx, 8, 7, 7, GUEST_ICON);
  snprintf(num, sizeof(num), "%u", (unsigned)s_cur.guests);
  drawText(gx + 8, 8, num, C_CLIENTS);

  const uint16_t C_GRAPH = rgb565(21, 58, 234);
  for (int16_t x = 0; x < MATRIX_W; x++)
    for (int16_t y = 0; y < s_cur.graph[x]; y++)
      c.drawPixel(x, MATRIX_H - 1 - y, C_GRAPH);

  // Last, because it animates: a client that joined in the last two minutes.
  if (s_cur.newest[0] && s_cur.min_uptime < NEW_CLIENT_S) {
    s_new.start(16, "New:", s_cur.newest, rgb565(247, 10, 12), rgb565(128, 5, 6), NEW_SCROLL_MS);
  }
}

static void unifiEnter()  { draw(); }
static void unifiCommit() {
  // Logged once a change: the same failure every 30 s says nothing new.
  if (s_next.warn[0] && strcmp(s_next.warn, s_cur.warn) != 0) logSDf("Unifi: %s", s_next.warn);
  monthTotals(s_next);
  s_cur = s_next;
  draw();
}
static bool unifiFrame(uint32_t now) { return s_new.tick(now); }

const DisplayMode MODE_UNIFI_OPS = {
  "unifi", "glowingsquare/unifi", 30000,
  unifiFetch, unifiCommit, unifiEnter, unifiFrame,
};
