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
