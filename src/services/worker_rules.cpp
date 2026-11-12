#include "services/worker_rules.h"

// Plain ASCII, no locale: a URL scheme is ASCII and <ctype.h> would depend on
// the C locale of whatever the unit is compiled for.
static char lowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

// True if s starts with prefix, given in lower case. The terminator of s
// differs from every character of the prefix, so the loop never reads past it.
static bool startsWithNoCase(const char* s, const char* prefix) {
  for (; *prefix; s++, prefix++) {
    if (lowerAscii(*s) != *prefix) return false;
  }
  return true;
}

uint32_t workerMinHeap(const char* base_url) {
  // Nothing to read a scheme from: the dearer floor, not the cheaper one.
  if (!base_url || !base_url[0]) return (uint32_t)WORKER_MIN_HEAP_TLS;
  return startsWithNoCase(base_url, "https://") ? (uint32_t)WORKER_MIN_HEAP_TLS
                                                : (uint32_t)WORKER_MIN_HEAP_PLAIN;
}

uint32_t workerMinHeapBoth(const char* first, const char* second) {
  const uint32_t a = workerMinHeap(first);
  const uint32_t b = workerMinHeap(second);
  return a > b ? a : b;
}

bool workerHeapAllows(uint32_t free_bytes, uint32_t largest_block,
                      uint32_t floor_bytes, uint32_t stack_bytes) {
  return free_bytes >= floor_bytes && largest_block >= stack_bytes;
}

bool logGateDue(LogGate* g, uint32_t now_ms, uint32_t interval_ms) {
  if (!g) return true;
  // Taken in uint32_t on purpose: the difference stays right when now_ms
  // has wrapped past last_ms.
  if (!g->armed || (uint32_t)(now_ms - g->last_ms) >= interval_ms) {
    g->armed   = true;
    g->last_ms = now_ms;
    return true;
  }
  g->suppressed++;
  return false;
}

uint32_t logGateTakeSuppressed(LogGate* g) {
  if (!g) return 0;
  const uint32_t n = g->suppressed;
  g->suppressed = 0;
  return n;
}

void logGateReset(LogGate* g) {
  if (!g) return;
  g->armed      = false;
  g->last_ms    = 0;
  g->suppressed = 0;
}
