// Unifi: network statistics straight from the UniFi console. v1 needed a PHP
// script on a server between the two, because the answers were too big for
// the ESP32's RAM; parsing them off the socket through a filter keeps only
// the handful of fields drawn.
//
// What it reads, all through the Network application's API:
//   stat/sta                   clients: count, guests, the newest one
//   stat/report/5minutes.site  WAN download per 5 minutes: the graph, exactly
//                              the history v1's script drew, so it is full
//                              from the first fetch
//   stat/report/daily.site     WAN totals for this month
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

#define GRAPH_HEIGHT       8
#define GRAPH_SCALE_POINTS 100   // v1: the scale comes from the last 100 buckets
#define BUCKET_MS          (5UL * 60UL * 1000UL)
#define NEW_CLIENT_S       120   // v1: INFO_UPDATE_INTERVAL * 4 / 1000
#define NEW_SCROLL_MS      50    // v1 scrolled this row at 50 ms a pixel

struct UnifiData {
  char     month_rx[16] = "-", month_tx[16] = "-";
  uint16_t clients = 0, guests = 0;
  uint8_t  graph[MATRIX_W] = {0};
  char     newest[40] = "";
  uint32_t min_uptime = UINT32_MAX;
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

// One call to the Network application. A refused session is renewed once.
static int api(const char *path, const char *body, const JsonDocument &filter,
               JsonDocument &out, char *err, size_t err_n) {
  char url[192], b[64];
  base(b, sizeof(b));
  snprintf(url, sizeof(url), "%s%s/api/s/%s/%s", b, isClassic() ? "" : "/proxy/network",
           g_unifi_site, path);

  for (int attempt = 0; attempt < 2; attempt++) {
    if (g_unifi_auth != UNIFI_AUTH_KEY && !s_cookie.length() && !login(err, err_n)) return -1;
    GsHttp req;
    req.url = url;
    req.method = body ? "POST" : "GET";
    req.body = body;
    req.verify_tls = g_unifi_verify_tls;
    int h = 0;
    if (g_unifi_auth == UNIFI_AUTH_KEY) {
      req.headers[h++] = "X-API-KEY";
      req.headers[h++] = g_unifi_key;
    } else {
      req.headers[h++] = "Cookie";
      req.headers[h++] = s_cookie.c_str();
      if (s_csrf.length()) { req.headers[h++] = "X-CSRF-Token"; req.headers[h++] = s_csrf.c_str(); }
    }
    const int code = gsHttpJson(req, out, &filter, err, err_n);
    if ((code == 401 || code == 403) && g_unifi_auth != UNIFI_AUTH_KEY && attempt == 0) {
      s_cookie = "";
      continue;
    }
    return code;
  }
  return -1;
}

// ---- fetch (worker task) ----------------------------------------------------

static bool readClients(char *err, size_t err_n) {
  JsonDocument filter, doc;
  JsonObject f = filter["data"][0].to<JsonObject>();
  f["hostname"] = true;
  f["name"] = true;
  f["uptime"] = true;
  f["is_guest"] = true;
  if (!gsHttpOk(api("stat/sta", nullptr, filter, doc, err, err_n))) return false;
  for (JsonObjectConst c : doc["data"].as<JsonArrayConst>()) {
    if (c["is_guest"] | false) s_next.guests++;
    else                       s_next.clients++;
    const uint32_t up = c["uptime"] | UINT32_MAX;
    if (up < s_next.min_uptime) {
      s_next.min_uptime = up;
      const char *n = c["hostname"] | "";
      if (!n[0]) n = c["name"] | "";
      snprintf(s_next.newest, sizeof(s_next.newest), "%s", n);
    }
  }
  return true;
}

static bool readGraph(char *err, size_t err_n) {
  if (time(nullptr) < 1700000000) return true;   // no clock yet: no window to ask for
  const uint64_t now_ms = (uint64_t)time(nullptr) * 1000ULL;
  char body[128];
  snprintf(body, sizeof(body), "{\"attrs\":[\"wan-rx_bytes\",\"time\"],\"start\":%llu,\"end\":%llu}",
           (unsigned long long)(now_ms - (uint64_t)GRAPH_SCALE_POINTS * BUCKET_MS),
           (unsigned long long)now_ms);
  JsonDocument filter, doc;
  filter["data"][0]["wan-rx_bytes"] = true;
  if (!gsHttpOk(api("stat/report/5minutes.site", body, filter, doc, err, err_n))) return false;

  JsonArrayConst data = doc["data"].as<JsonArrayConst>();
  const int n = (int)data.size();
  double max_bytes = 0;
  for (int i = max(0, n - GRAPH_SCALE_POINTS); i < n; i++)
    max_bytes = fmax(max_bytes, data[i]["wan-rx_bytes"] | 0.0);
  // The newest 64 buckets from the left edge, like v1's array_slice(-64).
  const int first = max(0, n - MATRIX_W);
  for (int i = first, x = 0; i < n; i++, x++) {
    const double b = data[i]["wan-rx_bytes"] | 0.0;
    s_next.graph[x] = max_bytes > 0 ? (uint8_t)ceil(b / max_bytes * GRAPH_HEIGHT) : 0;
  }
  return true;
}

static bool readMonth(char *err, size_t err_n) {
  time_t now = time(nullptr);
  if (now < 1700000000) return true;   // no clock yet: totals stay "-"
  struct tm t;
  localtime_r(&now, &t);
  t.tm_mday = 1; t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
  const time_t start = mktime(&t);
  char body[160];
  snprintf(body, sizeof(body),
           "{\"attrs\":[\"wan-rx_bytes\",\"wan-tx_bytes\",\"time\"],\"start\":%llu,\"end\":%llu}",
           (unsigned long long)start * 1000ULL, (unsigned long long)now * 1000ULL);
  JsonDocument filter, doc;
  filter["data"][0]["wan-rx_bytes"] = true;
  filter["data"][0]["wan-tx_bytes"] = true;
  if (!gsHttpOk(api("stat/report/daily.site", body, filter, doc, err, err_n))) return false;
  double rx = 0, tx = 0;
  for (JsonObjectConst d : doc["data"].as<JsonArrayConst>()) {
    rx += d["wan-rx_bytes"] | 0.0;
    tx += d["wan-tx_bytes"] | 0.0;
  }
  formatBytes(rx, s_next.month_rx, sizeof(s_next.month_rx));
  formatBytes(tx, s_next.month_tx, sizeof(s_next.month_tx));
  return true;
}

static bool unifiFetch(char *err, size_t err_n) {
  if (!g_unifi_url[0]) { snprintf(err, err_n, "no console address set"); return false; }
  if (g_unifi_auth == UNIFI_AUTH_KEY && !g_unifi_key[0]) { snprintf(err, err_n, "no API key set"); return false; }
  s_next = UnifiData();
  if (!readClients(err, err_n)) return false;
  // The graph and the totals are extras: a console that refuses one of them
  // still shows the clients, and the log says which call failed.
  char e2[64];
  if (!readGraph(e2, sizeof(e2)))  logSDf("Unifi: graph unavailable (%s)", e2);
  if (!readMonth(e2, sizeof(e2)))  logSDf("Unifi: month totals unavailable (%s)", e2);
  return true;
}

// ---- drawing (loop task) ----------------------------------------------------

static ScrollRow s_new;

static void draw() {
  GFXcanvas16 &c = matrixCanvas();
  c.fillScreen(0);
  s_new.stop();

  drawIcon(0, 0, 5, 7, DOWN_ICON);
  drawText(6, 0, s_cur.month_rx, rgb565(250, 50, 12));
  drawIcon(MATRIX_W - 5, 0, 5, 7, UP_ICON);
  drawRightAligned(MATRIX_W - 5, 0, s_cur.month_tx, rgb565(110, 10, 204));

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
static void unifiCommit() { s_cur = s_next; draw(); }
static bool unifiFrame(uint32_t now) { return s_new.tick(now); }

const DisplayMode MODE_UNIFI_OPS = {
  "unifi", "glowingsquare/unifi", 30000,
  unifiFetch, unifiCommit, unifiEnter, unifiFrame,
};
