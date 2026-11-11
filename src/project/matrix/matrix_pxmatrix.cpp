// PxMatrix, as v1 drove it: double-buffered, refreshed every 4 ms, 1/16 scan,
// with the mux delay the SRYLED P4 panels need.
//
// v1 refreshed from a hardware timer interrupt inside a critical section.
// On Arduino core 3 every SPI call takes a mutex, which may not be taken
// there: FreeRTOS asserts on the first refresh and the device boot-loops.
// An esp_timer callback runs in the esp_timer task instead, at the same
// period, where taking the SPI lock is allowed. Pins are Brian Lough's ESP32 Matrix Shield (Mini 32): R1 on GPIO13
// (SPI MOSI) and CLK on 14 through the SPI peripheral, the rest as below.
#include "project/matrix/matrix.h"

#include <Arduino.h>
#include <esp_timer.h>

#define double_buffer true   // PxMatrix reads this before its header
#include <PxMatrix.h>

#define PIN_LAT 22
#define PIN_OE   2
#define PIN_A   19
#define PIN_B   23
#define PIN_C   18
#define PIN_D    5

// The panel's on-time per refresh in microseconds: the larger, the brighter.
// 30 to 70 is safe; much more and the interrupt starves the loop.
#define DISPLAY_DRAW_TIME 70
// One refresh every 4 ms, as v1.
#define REFRESH_PERIOD_US 4000

static PxMATRIX display(MATRIX_W, MATRIX_H, PIN_LAT, PIN_OE, PIN_A, PIN_B, PIN_C, PIN_D);
static esp_timer_handle_t s_timer = nullptr;

static void displayUpdater(void *) {
  display.display(DISPLAY_DRAW_TIME);
}

bool matrixBegin() {
  display.begin(16);                   // 1/16 scan
  display.setMuxDelay(0, 2, 0, 0, 0);  // needed by the SRYLED P4 panels
  display.flushDisplay();
  display.setTextWrap(false);
  const esp_timer_create_args_t args = {
    .callback = displayUpdater,
    .arg = nullptr,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "pxmatrix",
    .skip_unhandled_events = true,   // a late refresh is skipped, not queued up
  };
  if (esp_timer_create(&args, &s_timer) != ESP_OK) return false;
  return esp_timer_start_periodic(s_timer, REFRESH_PERIOD_US) == ESP_OK;
}

void matrixShow() {
  const uint16_t *px = matrixCanvas().getBuffer();
  for (int16_t y = 0; y < MATRIX_H; y++)
    for (int16_t x = 0; x < MATRIX_W; x++)
      display.drawPixel(x, y, px[y * MATRIX_W + x]);
  display.showBuffer();
}

void matrixSetBrightness(uint8_t level) { display.setBrightness(level); }

const char *matrixDriverName() { return "PxMatrix"; }
