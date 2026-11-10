#pragma once
#include <cstdarg>
#include <cstdio>
static inline void logSD(const char *m) { printf("  [log] %s\n", m); }
static inline void logSDf(const char *f, ...) {
  va_list a; va_start(a, f); printf("  [log] "); vprintf(f, a); printf("\n"); va_end(a);
}
