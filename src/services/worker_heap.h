#pragma once

#include <stdint.h>

#include "services/worker_rules.h"

// ============================================================
//  WORKER START: THE HEAP CHECK EVERY BACKGROUND TASK ASKS
//
//  Reads the internal heap and its largest free block and applies the rules
//  of worker_rules.h. A refusal is logged at most once per
//  WORKER_REFUSAL_LOG_MS through the caller's gate, because a refused
//  start is asked for again on every pass of the loop.
// ============================================================

// The floor for a worker that talks to the server at this address.
uint32_t workerFloorFor(const char* base_url);

// The floor for a worker that talks to GitHub, always https.
uint32_t workerFloorTls();

// True when a task with this stack may start now. `who` names the worker in
// the log line; nullptr or no gate keeps a refusal silent.
bool workerHeapReady(const char* who, uint32_t floor_bytes, uint32_t stack_bytes,
                     LogGate* gate);

// ============================================================
//  TLS SLOT: ONE HANDSHAKE AT A TIME
//
//  A TLS connection takes some 40 kB of internal heap. Two at once - a
//  project's own fetch and a GitHub check from the Firmware page, say - left
//  too little for the second, which then failed as "connection refused".
//  Every worker that opens a TLS connection holds the slot for as long as the
//  connection is open, and waits for it, in its own task, when another has
//  it. Never taken on the loop task: waiting there is a frozen device.
// ============================================================

// How long a worker waits for the slot before it gives up on this attempt.
#define TLS_SLOT_WAIT_MS  20000

// True when the slot was free, or came free within wait_ms.
bool tlsSlotTake(uint32_t wait_ms);
void tlsSlotGive();

// The slot for the life of a block: given back however the block is left.
class TlsSlot {
 public:
  explicit TlsSlot(uint32_t wait_ms = TLS_SLOT_WAIT_MS) : held_(tlsSlotTake(wait_ms)) {}
  ~TlsSlot() { if (held_) tlsSlotGive(); }
  TlsSlot(const TlsSlot&) = delete;
  TlsSlot& operator=(const TlsSlot&) = delete;
  bool held() const { return held_; }
 private:
  bool held_;
};
