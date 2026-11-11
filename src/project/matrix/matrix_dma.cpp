// ESP32-HUB75-MatrixPanel-DMA: the panel is refreshed by I2S DMA (LCD_CAM on
// the S3) with no CPU time at all, double-buffered so a frame is never seen
// half drawn. Pins come from the Settings page (g_pin), defaults in
// project_settings.cpp.
#include "project/matrix/matrix.h"

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "hardware/sd_logger.h"
#include "project/gs_state.h"

static MatrixPanel_I2S_DMA *s_dma = nullptr;

bool matrixBegin() {
  HUB75_I2S_CFG::i2s_pins pins = {
    (int8_t)g_pin[0], (int8_t)g_pin[1], (int8_t)g_pin[2],
    (int8_t)g_pin[3], (int8_t)g_pin[4], (int8_t)g_pin[5],
    (int8_t)g_pin[6], (int8_t)g_pin[7], (int8_t)g_pin[8], (int8_t)g_pin[9], (int8_t)g_pin[10],
    (int8_t)g_pin[11], (int8_t)g_pin[12], (int8_t)g_pin[13],
  };
  HUB75_I2S_CFG cfg(MATRIX_W, MATRIX_H, 1, pins);
  cfg.double_buff = true;
  // P4 panels are happy at the library's default 8 MHz; 1/16 scan is set by
  // the height.
  s_dma = new MatrixPanel_I2S_DMA(cfg);
  if (!s_dma || !s_dma->begin()) {
    logSD("Matrix: the DMA driver did not start (memory, or a pin it cannot use)");
    delete s_dma;
    s_dma = nullptr;
    return false;
  }
  s_dma->clearScreen();
  return true;
}

void matrixShow() {
  if (!s_dma) return;
  const uint16_t *px = matrixCanvas().getBuffer();
  for (int16_t y = 0; y < MATRIX_H; y++)
    for (int16_t x = 0; x < MATRIX_W; x++)
      s_dma->drawPixel(x, y, px[y * MATRIX_W + x]);
  s_dma->flipDMABuffer();
}

void matrixSetBrightness(uint8_t level) {
  if (s_dma) s_dma->setBrightness8(matrixPerceptual(level));
}

const char *matrixDriverName() { return "HUB75 DMA"; }

uint32_t matrixRefreshCount() { return 0; }

void matrixApplyTiming() {}   // the DMA refresh has no software timing

// The DMA engine reads the frame from RAM by itself: a flash write cannot
// stall it, so there is nothing to hold.
bool matrixHold(int16_t) { return true; }
