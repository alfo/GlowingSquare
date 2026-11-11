#include "project/gs_http.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "app_config.h"
#include "services/github_release.h"   // githubTrust(): the IDF certificate bundle
#include "services/worker_heap.h"      // TlsSlot

#include "project/json_walk.h"

// How the body is read once the server has said 2xx; http is open.
struct GsBody {
  JsonDocument       *out = nullptr;      // gsHttpJson
  const JsonDocument *filter = nullptr;
  const char         *list = nullptr;     // gsHttpJsonEach
  GsJsonEach          each = nullptr;
  void               *ctx = nullptr;
};

// The body byte by byte. Waits while the connection is open and nothing has
// come yet, so a body that ends by closing (adsb.fi) is read to its end.
class SocketSource {
 public:
  SocketSource(WiFiClient *c, uint32_t timeout_ms) : c_(c), timeout_ms_(timeout_ms) {}
  int read() {
    if (pos_ < len_) return buf_[pos_++];
    const uint32_t start = millis();
    while (millis() - start < timeout_ms_) {
      const int avail = c_ ? c_->available() : 0;
      if (avail > 0) {
        len_ = c_->read(buf_, (size_t)avail < sizeof(buf_) ? (size_t)avail : sizeof(buf_));
        pos_ = 0;
        if (len_ > 0) return buf_[pos_++];
      } else if (!c_ || !c_->connected()) {
        return -1;
      }
      delay(1);
    }
    return -1;
  }

 private:
  WiFiClient *c_;
  uint32_t    timeout_ms_;
  uint8_t     buf_[256];
  int         len_ = 0, pos_ = 0;
};

static DeserializationError readWhole(HTTPClient &http, const GsHttp &req, const GsBody &b,
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

static DeserializationError readEach(HTTPClient &http, const GsHttp &req, const GsBody &b) {
  SocketSource src(http.getStreamPtr(), req.timeout_ms);
  GsJsonEach each = b.each;
  void *ctx = b.ctx;
  return jsonWalkArray(src, b.list, *b.filter,
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
  // HTTP/1.0: no chunked transfer, so the body can be parsed off the stream.
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
  const DeserializationError e = b.list ? readEach(http, req, b) : readWhole(http, req, b, err, err_n);
  http.end();
  if (!e) return code;
  if (err[0]) return -3;   // refused before reading: readWhole() said why
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
  b.list = list;
  b.each = each;
  b.ctx = ctx;
  return requestInTurn(req, b, err, err_n);
}
