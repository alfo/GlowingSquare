// Just enough Arduino for Adafruit GFX and Glowing Square's drawing and
// parsing code to run on a computer. Not a general shim.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <ctime>

#ifndef ARDUINO
#define ARDUINO 10819
#endif
#define PROGMEM
#define PGM_P const char *
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const uint32_t *)(a))
#define pgm_read_pointer(a) (*(void *const *)(a))
#define strlen_P strlen
#define F(s) (s)
typedef bool boolean;
class __FlashStringHelper;
typedef uint8_t byte;

extern uint32_t g_fake_ms;
static inline uint32_t millis() { return g_fake_ms; }
static inline uint32_t micros() { return g_fake_ms * 1000; }
static inline void delay(uint32_t ms) { g_fake_ms += ms; }
static inline long random(long a, long b) { return a + (rand() % (b - a)); }
static inline long random(long b) { return rand() % b; }
static inline double radians(double d) { return d * M_PI / 180.0; }
static inline double degrees(double r) { return r * 180.0 / M_PI; }
using std::min;
using std::max;

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  const char *c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool reserve(size_t n) { s_.reserve(n); return true; }
  bool concat(const char *s) { s_ += s; return true; }
  bool concat(const char *s, size_t n) { s_.append(s, n); return true; }
  bool concat(char c) { s_ += c; return true; }
  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *o) { s_ += o; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  friend String operator+(const String &a, const String &b) { return String(a.s_ + b.s_); }
  friend String operator+(const String &a, const char *b) { return String(a.s_ + b); }
  friend String operator+(const char *a, const String &b) { return String(std::string(a) + b.s_); }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == o; }
  int indexOf(char c) const { auto p = s_.find(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(size_t a) const { return String(s_.substr(a)); }
  String substring(size_t a, size_t b) const { return String(s_.substr(a, b - a)); }
  void toUpperCase() { for (auto &c : s_) c = (char)toupper(c); }
  char operator[](size_t i) const { return s_[i]; }
 private:
  std::string s_;
};

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
  size_t print(const char *s) { return write((const uint8_t *)s, strlen(s)); }
  size_t print(const __FlashStringHelper *s) { return print((const char *)s); }
  size_t print(const String &s) { return print(s.c_str()); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(int v) { char b[16]; snprintf(b, sizeof b, "%d", v); return print(b); }
  size_t print(unsigned v) { char b[16]; snprintf(b, sizeof b, "%u", v); return print(b); }
  size_t print(long v) { char b[24]; snprintf(b, sizeof b, "%ld", v); return print(b); }
  size_t print(unsigned long v) { char b[24]; snprintf(b, sizeof b, "%lu", v); return print(b); }
  size_t println(const char *s) { return print(s) + print("\n"); }
};
