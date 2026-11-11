#include "project/gs_http.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "app_config.h"
#include "services/github_release.h"   // githubTrust(): the IDF certificate bundle
#include "services/worker_heap.h"      // TlsSlot

static int request(const GsHttp &req, JsonDocument &out, const JsonDocument *filter,
                   char *err, size_t err_n, bool tls) {
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

  if (code < 200 || code >= 300) {
    if (code < 0) snprintf(err, err_n, "%s", http.errorToString(code).c_str());
    else          snprintf(err, err_n, "HTTP %d", code);
    http.end();
    return code;
  }
  DeserializationError e;
  if (req.buffer_max) {
    const int announced = http.getSize();   // -1 when the server sends none
    if (announced > (int)req.buffer_max) {
      snprintf(err, err_n, "answer too big (%d bytes)", announced);
      http.end();
      return -3;
    }
    const String body = http.getString();
    http.end();
    if (body.length() == 0) {
      snprintf(err, err_n, "empty answer");
      return -2;
    }
    e = filter ? deserializeJson(out, body, DeserializationOption::Filter(*filter))
               : deserializeJson(out, body);
  } else {
    e = filter ? deserializeJson(out, http.getStream(), DeserializationOption::Filter(*filter))
               : deserializeJson(out, http.getStream());
    http.end();
  }
  if (e) {
    snprintf(err, err_n, "JSON: %s", e.c_str());
    return -2;
  }
  return code;
}

// HTTPS waits its turn: the Firmware page's GitHub check and this fetch each
// need some 40 kB, and the second of two at once failed to connect. The slot
// is taken out here so the clients inside request() are gone before it is
// handed on.
int gsHttpJson(const GsHttp &req, JsonDocument &out, const JsonDocument *filter,
               char *err, size_t err_n) {
  if (err && err_n) err[0] = '\0';
  const bool tls = strncmp(req.url, "https://", 8) == 0;
  if (!tls) return request(req, out, filter, err, err_n, false);
  TlsSlot slot;
  if (!slot.held()) {
    snprintf(err, err_n, "another HTTPS request held on");
    return -4;
  }
  return request(req, out, filter, err, err_n, true);
}
