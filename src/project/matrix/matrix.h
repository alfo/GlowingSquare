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

// 0..255 as people see it: 128 looks half as bright as 255. The driver gets
// matrixPerceptual(level), since LED light output is linear in on-time but
// the eye is not: halving the on-time barely shows at the top of the range.
void matrixSetBrightness(uint8_t level);

// The curve: a 2.2 power, the gamma of the eye. 0 stays off; any other level
// stays at least 1, so a low setting never goes dark.
uint8_t matrixPerceptual(uint8_t level);

// Panel refreshes since boot (PxMatrix: one colour plane each), for the log.
// 0 on drivers that refresh by DMA.
uint32_t matrixRefreshCount();

// PxMatrix builds: apply a changed refresh period (g_px_period_us) now.
void matrixApplyTiming();

// While firmware is written. A flash write stalls the CPU, and a panel the
// CPU multiplexes strobes through every stall. Held at row y, the PxMatrix
// build stops multiplexing and keeps rows y and y + 16 lit from the hardware
// alone, at the brightness set; matrixShow() then updates those two rows and
// nothing else. y < 0 lets go and the whole canvas shows again. The DMA
// build refreshes without the CPU, so it shows the whole canvas throughout.
// False when the panel could not be held: it is then best left dark.
bool matrixHold(int16_t y);

// Starts the refresh again when it has stopped (the refresh watch in
// project.cpp asks). On 11.11.2026 a running PxMatrix refresh stopped for
// good, panel black; releasing the update hold, which restarts its timer,
// brought it back. The DMA build refreshes by itself: nothing to do.
void matrixRestartRefresh();

// Which driver this build has, for the Status page and the log.
const char *matrixDriverName();

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
