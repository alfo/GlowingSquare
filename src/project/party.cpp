// Party mode: the Aurora patterns v1 ran (Jason Coon, MIT; the 64x32 port
// from the ESP32-HUB75-MatrixPanel-DMA examples), unchanged. They draw
// through a `display` object; the shim below hands their pixels to the
// canvas instead of a panel driver.
//
// As v1: "chill" runs the wave pattern with 20 ms between frames, "party"
// the spiral flat out.
#include <Arduino.h>
#include <FastLED.h>

#include "project/gs_state.h"
#include "project/matrix/matrix.h"
#include "project/party.h"

#define MATRIX_WIDTH  64
#define MATRIX_HEIGHT 32

namespace {
struct CanvasShim {
  void drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
    matrixCanvas().drawPixel(x, y, rgb565(r, g, b));
  }
  void fillScreen(uint16_t c) { matrixCanvas().fillScreen(c); }
};
CanvasShim display;
}  // namespace

#include "project/patterns/Effects.h"
Effects effects;
#include "project/patterns/Drawable.h"
#include "project/patterns/Playlist.h"
#include "project/patterns/Patterns.h"
Patterns patterns;

#define PATTERN_CHILL   1   // "1: Wave"
#define PATTERN_PARTY   4   // "4: Spiral"
#define CHILL_FRAME_MS  20

static uint8_t  s_running = PARTY_OFF;
static uint32_t s_next_frame_ms = 0;

void partyBegin() {
  effects.Setup();
  patterns.setPattern(PATTERN_CHILL);
}

bool partyFrame(uint32_t now) {
  if (g_party != s_running) {
    s_running = g_party;
    if (s_running == PARTY_OFF) return false;
    // Stopped and started around the switch, which is what made v1's
    // transition clean.
    patterns.stop();
    patterns.setPattern(s_running == PARTY_CHILL ? PATTERN_CHILL : PATTERN_PARTY);
    patterns.start();
    s_next_frame_ms = now;
  }
  if (s_running == PARTY_OFF || (int32_t)(now - s_next_frame_ms) < 0) return false;
  const uint32_t pattern_delay = patterns.drawFrame();
  s_next_frame_ms = now + pattern_delay + (s_running == PARTY_CHILL ? CHILL_FRAME_MS : 0);
  return true;
}
