#pragma once

#include <Adafruit_GFX.h>
#include <stdint.h>

// ============================================================
//  The 64x32 matrix
//
//  Everything draws into one GFXcanvas16 (4 kB of RGB565) and matrixShow()
//  hands the finished frame to the panel. Two drivers sit behind it, one
//  per build env:
//
//   matrix_pxmatrix.cpp  PxMatrix, for Brian Lough's Mini 32 shield (one
//                        colour line, chained shift registers)
//   matrix_dma.cpp       ESP32-HUB75-MatrixPanel-DMA, for boards with all six
//                        colour lines wired
//
//  The canvas is also what the web interface's live view shows, so what the
//  browser sees is exactly what the panel was given.
// ============================================================

constexpr int16_t MATRIX_W = 64;
constexpr int16_t MATRIX_H = 32;

GFXcanvas16 &matrixCanvas();

// Starts the driver. False when it could not (no memory for the DMA buffers,
// say); the firmware keeps running, with the web interface, regardless.
bool matrixBegin();

// The canvas to the panel. Cheap enough to call every frame.
void matrixShow();

// 0..255, applied by the driver (PxMatrix: on-time, DMA: OE duty).
void matrixSetBrightness(uint8_t level);

// Which driver this build has, for the Status page and the log.
const char *matrixDriverName();

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
