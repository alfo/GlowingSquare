#pragma once

#include <stdint.h>

// ============================================================
//  WHEN A BACKGROUND WORKER MAY START: THE RULES
//
//  A worker that talks to a server runs on a task of its own (web jobs,
//  the update check, whatever the project adds). Its stack comes out of the
//  internal heap the moment the task is created, so the start is refused
//  while too little is free.
//
//  With PSRAM, mbedTLS allocates there (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC),
//  so what a request takes from the internal heap is its stack and its
//  connection: about 22 to 24 kB measured on an S3, encrypted or not.
//  Without PSRAM (classic ESP32) the TLS session itself, some 40 kB, comes
//  out of the same internal heap, hence the much higher TLS floor there.
//
//  Only the address decides, the one thing known before the task exists.
//  The free sum is not enough on its own: the stack has to fit in one piece,
//  so the largest free block must hold it as well.
//
//  The rules live here without Arduino, the heap or the log, so that
//  test/test_worker_rules.py can hold them. Workers ask them through
//  services/worker_heap.h.
// ============================================================

#ifndef WORKER_MIN_HEAP_TLS
#ifdef BOARD_HAS_PSRAM
#define WORKER_MIN_HEAP_TLS   50000   // https: stack and socket, TLS lives in PSRAM
#else
#define WORKER_MIN_HEAP_TLS   75000   // https: stack, socket and the TLS session
#endif
#endif
#ifndef WORKER_MIN_HEAP_PLAIN
#define WORKER_MIN_HEAP_PLAIN 40000   // http: stack plus plain sockets
#endif

#define WORKER_REFUSAL_LOG_MS 60000

// The free internal heap a worker needs before it may start, for a server
// at this base URL. "https://" in any case of letters, an empty address or
// none: the TLS floor, because nothing says it is plain. Anything else is
// http, a bare address without scheme included.
uint32_t workerMinHeap(const char* base_url);

// The higher of the two floors, for a worker that may talk to either address.
uint32_t workerMinHeapBoth(const char* first, const char* second);

// True when a task with a stack of stack_bytes may start: at least floor_bytes
// free in all, and the largest free block big enough for the stack.
bool workerHeapAllows(uint32_t free_bytes, uint32_t largest_block,
                      uint32_t floor_bytes, uint32_t stack_bytes);

// Says whether a line that can repeat on every pass may be written now: the
// first time, then once per interval. The refused calls are counted so the
// line can say how many it stands for. Zero-initialise it (LogGate g{}); one
// gate for one line, used from one task.
struct LogGate {
  bool     armed;       // a line has been written, last_ms is valid
  uint32_t last_ms;     // when it was written
  uint32_t suppressed;  // calls refused since then
};

// True for the first call and then once interval_ms has passed since the
// last line, counted modulo 2^32 so a wrapping millis() does not stall it.
// Every other call counts as suppressed. No gate: always due.
bool logGateDue(LogGate* g, uint32_t now_ms, uint32_t interval_ms);

// The calls refused since the last line, which it also clears. 0 for no gate.
uint32_t logGateTakeSuppressed(LogGate* g);

// Makes the next call due again and forgets the count, for when the refusals
// have stopped and the next episode should start with a line of its own.
void logGateReset(LogGate* g);
