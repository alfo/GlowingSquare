#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// One HTTP(S) request whose answer is JSON, parsed straight off the socket
// through an optional filter, so a big answer never has to fit in RAM whole.
// For the fetch task only: it blocks for as long as the server takes.
struct GsHttp {
  const char  *url;
  const char  *method  = "GET";
  const char  *body    = nullptr;      // sent as application/json
  const char  *headers[8] = {nullptr}; // name, value, name, value ...
  bool         verify_tls = true;      // false: any certificate (self-signed consoles)
  uint32_t     timeout_ms = 8000;
  // Filled when given: the Set-Cookie and X-CSRF-Token answers (UniFi login).
  String      *set_cookie = nullptr;
  String      *csrf       = nullptr;
};

// The HTTP status, or a negative HTTPClient error. `err` gets a line for the
// log whenever the answer is not a 2xx parsed cleanly.
int gsHttpJson(const GsHttp &req, JsonDocument &out, const JsonDocument *filter,
               char *err, size_t err_n);
