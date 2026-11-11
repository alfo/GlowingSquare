#include "project/gs_http.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "app_config.h"
#include "services/github_release.h"   // githubTrust(): the IDF certificate bundle
#include "services/worker_heap.h"      // TlsSlot

#include "project/body_blocks.h"
#include "project/json_walk.h"

// How the body is read once the server has said 2xx; http is open.
struct GsBody {
  JsonDocument       *out = nullptr;      // gsHttpJson
  const JsonDocument *filter = nullptr;
  bool                walk = false;       // gsHttpJsonEach: the array a piece at a time
  const char         *list = nullptr;     // its key; nullptr: the answer is the array
  GsJsonEach          each = nullptr;
  void               *ctx = nullptr;
};

// What an answer read into blocks may weigh: 50 nm of busy sky over London
// is 55 kB. And the heap left beside them, for the parse and the TLS
// connection still open.
#define GS_HTTP_WALK_MAX      (64u * 1024u)
#define GS_HTTP_HEAP_RESERVE  (16u * 1024u)

// Everything the server sends, as fast as it comes, until the Content-Length
// or, without one, until it closes (HTTP/1.0). See body_blocks.h for why
// the answer is read whole before it is parsed.
static bool readAll(HTTPClient &http, const GsHttp &req, BodyBlocks &blocks,
                    char *err, size_t err_n) {
  WiFiClient *c = http.getStreamPtr();
  const int length = http.getSize();   // -1 when the server sends none
  uint8_t buf[512];
  uint32_t last = millis();
  while (c && (length < 0 || blocks.size() < (size_t)length)) {
    const int avail = c->available();
    if (avail <= 0) {
      if (!c->connected()) break;
      if (millis() - last >= req.timeout_ms) {
        snprintf(err, err_n, "read timeout after %u bytes", (unsigned)blocks.size());
        return false;
      }
      delay(1);
      continue;
    }
    const int n = c->read(buf, (size_t)avail < sizeof(buf) ? (size_t)avail : sizeof(buf));
    if (n <= 0) continue;
    if (ESP.getFreeHeap() < GS_HTTP_HEAP_RESERVE || !blocks.append(buf, (size_t)n)) {
      snprintf(err, err_n, "answer too big (%u bytes so far)", (unsigned)blocks.size());
      return false;
    }
    last = millis();
  }
  if (length >= 0 && blocks.size() < (size_t)length) {
    snprintf(err, err_n, "answer cut off at %u of %d bytes", (unsigned)blocks.size(), length);
    return false;
  }
  return true;
}

// Out of line, both readers: their buffers and documents are on the stack
// only once the handshake is over, the deepest point of a fetch.
__attribute__((noinline)) static DeserializationError readWhole(HTTPClient &http, const GsHttp &req, const GsBody &b,
                                      char *err, size_t err_n) {
  if (req.buffer_max) {
    const int announced = http.getSize();   // -1 when the server sends none
    if (announced > (int)req.buffer_max) {
      snprintf(err, err_n, "answer too big (%d bytes)", announced);
      return DeserializationError::NoMemory;
    }
    const String body = http.getString();
    if (body.length() == 0) return DeserializationError::EmptyInput;
    return b.filter ? deserializeJson(*b.out, body, DeserializationOption::Filter(*b.filter))
                    : deserializeJson(*b.out, body);
  }
  return b.filter ? deserializeJson(*b.out, http.getStream(), DeserializationOption::Filter(*b.filter))
                  : deserializeJson(*b.out, http.getStream());
}

__attribute__((noinline)) static DeserializationError readEach(HTTPClient &http, const GsHttp &req,
                                                              const GsBody &b, char *err, size_t err_n) {
  BodyBlocks blocks(GS_HTTP_WALK_MAX);
  if (!readAll(http, req, blocks, err, err_n)) return DeserializationError::IncompleteInput;
  http.end();   // all of it is here: the connection can go before the parse
  GsJsonEach each = b.each;
  void *ctx = b.ctx;
  return jsonWalkArray(blocks, b.list, *b.filter,
                       [each, ctx](JsonObjectConst el) { each(el, ctx); });
}

static int sendRequest(HTTPClient &http, const GsHttp &req) {
  http.addHeader("User-Agent", APP_USER_AGENT);
  http.addHeader("Accept", "application/json");
  for (int i = 0; i + 1 < 8 && req.headers[i]; i += 2) http.addHeader(req.headers[i], req.headers[i + 1]);
  static const char *KEYS[] = { "Set-Cookie", "X-CSRF-Token" };
  if (req.set_cookie || req.csrf) http.collectHeaders(KEYS, 2);
  int code;
  if (req.body) {
    http.addHeader("Content-Type", "application/json");
    code = http.sendRequest(req.method, (uint8_t *)req.body, strlen(req.body));
  } else {
    code = http.sendRequest(req.method);
  }
  if (req.set_cookie) *req.set_cookie = http.header("Set-Cookie");
  if (req.csrf)       *req.csrf = http.header("X-CSRF-Token");
  return code;
}

static int request(const GsHttp &req, const GsBody &b, char *err, size_t err_n, bool tls) {
  WiFiClientSecure secure;
  WiFiClient plain;
  if (tls) {
    if (req.verify_tls) githubTrust(secure);
    else secure.setInsecure();
  }
  HTTPClient http;
  // HTTP/1.0: no chunked transfer, so the body can be read straight off.
  http.useHTTP10(true);
  http.setConnectTimeout(req.timeout_ms);
  http.setTimeout(req.timeout_ms);
  if (!http.begin(tls ? (WiFiClient &)secure : plain, req.url)) {
    snprintf(err, err_n, "bad URL");
    return -1;
  }
  const int code = sendRequest(http, req);
  if (code < 200 || code >= 300) {
    if (code < 0) snprintf(err, err_n, "%s", http.errorToString(code).c_str());
    else          snprintf(err, err_n, "HTTP %d", code);
    http.end();
    return code;
  }
  const DeserializationError e = b.walk ? readEach(http, req, b, err, err_n) : readWhole(http, req, b, err, err_n);
  http.end();
  if (!e) return code;
  if (err[0]) return -3;   // too big, cut off or too slow: the reader said why
  snprintf(err, err_n, "JSON: %s", e.c_str());
  return -2;
}

// HTTPS waits its turn: the Firmware page's GitHub check and this fetch each
// need some 40 kB, and the second of two at once failed to connect. The slot
// is taken out here so the clients inside request() are gone before it is
// handed on.
static int requestInTurn(const GsHttp &req, const GsBody &b, char *err, size_t err_n) {
  if (err && err_n) err[0] = '\0';
  const bool tls = strncmp(req.url, "https://", 8) == 0;
  if (!tls) return request(req, b, err, err_n, false);
  TlsSlot slot;
  if (!slot.held()) {
    snprintf(err, err_n, "another HTTPS request held on");
    return -4;
  }
  return request(req, b, err, err_n, true);
}

int gsHttpJson(const GsHttp &req, JsonDocument &out, const JsonDocument *filter,
               char *err, size_t err_n) {
  GsBody b;
  b.out = &out;
  b.filter = filter;
  return requestInTurn(req, b, err, err_n);
}

int gsHttpJsonEach(const GsHttp &req, const char *list, const JsonDocument &filter,
                   GsJsonEach each, void *ctx, char *err, size_t err_n) {
  GsBody b;
  b.filter = &filter;
  b.walk = true;
  b.list = list;
  b.each = each;
  b.ctx = ctx;
  return requestInTurn(req, b, err, err_n);
}
