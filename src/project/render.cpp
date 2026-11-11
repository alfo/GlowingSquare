#include "project/render.h"

#include <string.h>

static GFXcanvas16 &cv() { return matrixCanvas(); }

void renderBegin() {
  cv().setTextWrap(false);
  cv().setTextSize(1);
  cv().setRotation(0);
}

void fillBlankRow(int16_t y) { cv().fillRect(0, y, MATRIX_W, ROW_H, 0); }

void drawText(int16_t x, int16_t y, const char *text, uint16_t color) {
  cv().setTextColor(color);
  cv().setCursor(x, y);
  cv().print(text);
}

void drawRightAligned(int16_t x, int16_t y, const char *text, uint16_t color) {
  drawText(x - (int16_t)strlen(text) * CHAR_ADVANCE, y, text, color);
}

void drawStatic(int16_t y, int16_t clear_w, const char *text, uint16_t color) {
  if (clear_w > 0) cv().fillRect(0, y, clear_w, ROW_H, 0);
  drawText(0, y, text, color);
}

void drawIcon(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *img) {
  for (int16_t yy = 0; yy < h; yy++)
    for (int16_t xx = 0; xx < w; xx++)
      cv().drawPixel(x + xx, y + yy, pgm_read_word(&img[yy * w + xx]));
}

void drawOffline() {
  cv().fillScreen(0);
  cv().drawPixel(MATRIX_W - 1, MATRIX_H - 1, rgb565(255, 0, 0));
}

// ---------------------------------------------------------------------------

void ScrollRow::drawAt(int16_t x) {
  fillBlankRow(y_);
  drawText(x, y_, scroll_, c_scroll_);
  drawStatic(y_, offset_, stat_, c_static_);
}

void ScrollRow::start(int16_t y, const char *stat, const char *scroll, uint16_t c_static,
                      uint16_t c_scroll, uint32_t step_ms, uint32_t hold_ms,
                      bool only_if_needed) {
  snprintf(stat_, sizeof(stat_), "%s", stat);
  snprintf(scroll_, sizeof(scroll_), "%s", scroll);
  y_ = y;
  c_static_ = c_static;
  c_scroll_ = c_scroll;
  step_ms_ = step_ms;
  // v1: the scroll starts right after the prefix and runs until
  // -(width + length * 5) + 50, then the text snaps back to the start.
  offset_ = (int16_t)strlen(stat_) * CHAR_ADVANCE;
  limit_ = -(MATRIX_W + (int16_t)strlen(scroll_) * CHAR_W_SCROLL) + 50;
  x_ = offset_;
  drawAt(x_);
  const bool fits = offset_ + (int16_t)strlen(scroll_) * CHAR_ADVANCE <= MATRIX_W;
  if (only_if_needed && fits) {
    active_ = false;
    return;
  }
  next_ms_ = millis() + hold_ms + step_ms_;
  active_ = true;
}

bool ScrollRow::tick(uint32_t now) {
  if (!active_ || (int32_t)(now - next_ms_) < 0) return false;
  next_ms_ = now + step_ms_;
  x_--;
  if (x_ > limit_) {
    drawAt(x_);
    return true;
  }
  // Done: the text back at its start, so it stays readable until the next
  // update scrolls it again.
  drawAt(offset_);
  active_ = false;
  return true;
}
