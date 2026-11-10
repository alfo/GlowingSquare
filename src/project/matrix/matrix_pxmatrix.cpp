// PxMatrix, as v1 drove it: double-buffered, refreshed from a hardware timer
// interrupt every 4 ms, 1/16 scan, with the mux delay the SRYLED P4 panels
// need. Pins are Brian Lough's ESP32 Matrix Shield (Mini 32): R1 on GPIO13
// (SPI MOSI) and CLK on 14 through the SPI peripheral, the rest as below.
#include "project/matrix/matrix.h"

#include <Arduino.h>

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
// One refresh every 4 ms, from a 1 MHz timer.
#define REFRESH_TIMER_HZ  1000000
#define REFRESH_TICKS     4000

static PxMATRIX display(MATRIX_W, MATRIX_H, PIN_LAT, PIN_OE, PIN_A, PIN_B, PIN_C, PIN_D);
static hw_timer_t *s_timer = nullptr;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR displayUpdater() {
  portENTER_CRITICAL_ISR(&s_mux);
  display.display(DISPLAY_DRAW_TIME);
  portEXIT_CRITICAL_ISR(&s_mux);
}

bool matrixBegin() {
  display.begin(16);                   // 1/16 scan
  display.setMuxDelay(0, 2, 0, 0, 0);  // needed by the SRYLED P4 panels
  display.flushDisplay();
  display.setTextWrap(false);
  // Arduino core 3 timer API: a frequency, an ISR, an alarm in ticks.
  s_timer = timerBegin(REFRESH_TIMER_HZ);
  if (!s_timer) return false;
  timerAttachInterrupt(s_timer, &displayUpdater);
  timerAlarm(s_timer, REFRESH_TICKS, true, 0);
  return true;
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
