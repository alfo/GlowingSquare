#include "services/worker_heap.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "hardware/sd_logger.h"
uint32_t workerFloorFor(const char* base_url) {
  return workerMinHeap(base_url);
}

uint32_t workerFloorTls() {
  return (uint32_t)WORKER_MIN_HEAP_TLS;
}

bool workerHeapReady(const char* who, uint32_t floor_bytes, uint32_t stack_bytes,
                     LogGate* gate) {
  const uint32_t free_bytes = (uint32_t)ESP.getFreeHeap();
  const uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (workerHeapAllows(free_bytes, largest, floor_bytes, stack_bytes)) {
    // The refusals have stopped: the next episode gets a line of its own.
    logGateReset(gate);
    return true;
  }
  if (who && gate && logGateDue(gate, millis(), WORKER_REFUSAL_LOG_MS)) {
    logSDf("%s: postponed, heap %u of %u, largest block %u for a %u stack (%u more since the last line)",
           who, (unsigned)free_bytes, (unsigned)floor_bytes, (unsigned)largest,
           (unsigned)stack_bytes, (unsigned)logGateTakeSuppressed(gate));
  }
  return false;
}

// Made on first use. Two tasks can get there together, so the one that loses
// the race throws its own away.
static SemaphoreHandle_t s_tls_slot = nullptr;
static portMUX_TYPE      s_tls_mux  = portMUX_INITIALIZER_UNLOCKED;

static SemaphoreHandle_t tlsSlot() {
  if (s_tls_slot) return s_tls_slot;
  SemaphoreHandle_t made = xSemaphoreCreateBinary();
  if (!made) return nullptr;
  xSemaphoreGive(made);
  portENTER_CRITICAL(&s_tls_mux);
  if (!s_tls_slot) { s_tls_slot = made; made = nullptr; }
  portEXIT_CRITICAL(&s_tls_mux);
  if (made) vSemaphoreDelete(made);
  return s_tls_slot;
}

bool tlsSlotTake(uint32_t wait_ms) {
  SemaphoreHandle_t s = tlsSlot();
  // No semaphore means no heap for one either: going ahead is no worse.
  if (!s) return true;
  return xSemaphoreTake(s, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void tlsSlotGive() {
  if (s_tls_slot) xSemaphoreGive(s_tls_slot);
}
