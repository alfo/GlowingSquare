#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <string.h>

// ============================================================
//  One array in a JSON answer, an element at a time
//
//  jsonWalkArray() finds "key": [ in whatever `src` reads (or, with key
//  nullptr, takes the answer as the array itself) and parses the
//  array's elements one by one, each through `filter` into one small
//  document, handing each to `each`. Memory stays one element however long
//  the array is: the adsb feeds answer 55 kB for 50 nm around London, more
//  than a board without PSRAM can hold beside a TLS connection.
//
//  Src needs only int read(): the next byte, or -1 at the end. No Arduino
//  here, so the host test runs it on the recorded answers.
// ============================================================

// The reader ArduinoJson is given: read() and readBytes(), with one byte of
// lookahead for the walk between elements.
template <typename Src>
class JsonPeekReader {
 public:
  explicit JsonPeekReader(Src &src) : src_(src) {}
  int read() {
    if (ahead_ != NONE) { const int c = ahead_; ahead_ = NONE; return c; }
    return src_.read();
  }
  int peek() {
    if (ahead_ == NONE) ahead_ = src_.read();
    return ahead_;
  }
  size_t readBytes(char *buf, size_t n) {
    size_t i = 0;
    for (; i < n; i++) {
      const int c = read();
      if (c < 0) break;
      buf[i] = (char)c;
    }
    return i;
  }
  int skipSpace() {
    while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t') read();
    return peek();
  }

 private:
  static constexpr int NONE = -2;
  Src &src_;
  int ahead_ = NONE;
};

// Reads up to and including the '[' that opens the array under `key`. A
// string equal to the key that is not followed by ':' (a value, not a key)
// is passed over.
template <typename Src>
static DeserializationError jsonSeekArray(JsonPeekReader<Src> &r, const char *key) {
  if (!key) {   // the answer is the array itself (TfL)
    if (r.skipSpace() != '[') return r.peek() < 0 ? DeserializationError::IncompleteInput
                                                  : DeserializationError::InvalidInput;
    r.read();
    return DeserializationError::Ok;
  }
  const size_t n = strlen(key);
  for (;;) {
    size_t matched = 0;   // bytes of "key" seen so far, the quotes included
    while (matched < n + 2) {
      const int c = r.read();
      if (c < 0) return DeserializationError::IncompleteInput;
      const char want = (matched == 0 || matched == n + 1) ? '"' : key[matched - 1];
      if (c == want) matched++;
      else           matched = (c == '"') ? 1 : 0;
    }
    if (r.skipSpace() != ':') continue;
    r.read();
    if (r.skipSpace() != '[') return DeserializationError::InvalidInput;
    r.read();
    return DeserializationError::Ok;
  }
}

template <typename Src, typename Each>
DeserializationError jsonWalkArray(Src &src, const char *key, const JsonDocument &filter,
                                   Each each) {
  JsonPeekReader<Src> r(src);
  DeserializationError e = jsonSeekArray(r, key);
  if (e) return e;
  if (r.skipSpace() == ']') return DeserializationError::Ok;   // empty
  JsonDocument el;
  for (;;) {
    e = deserializeJson(el, r, DeserializationOption::Filter(filter));
    if (e) return e;
    each(el.as<JsonObjectConst>());
    const int c = r.skipSpace();
    r.read();
    if (c == ']') return DeserializationError::Ok;
    if (c < 0)    return DeserializationError::IncompleteInput;
    if (c != ',') return DeserializationError::InvalidInput;
  }
}
