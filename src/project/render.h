#pragma once

#include <Arduino.h>
#include "project/matrix/matrix.h"

// ============================================================
//  Drawing on the matrix, the way v1 drew: the default 5x7 font
//  (6 px per character), rows 8 px tall, scrolling text that runs left
//  past a static prefix and snaps back. Nothing here waits: a scroll is
//  advanced one pixel per tick() from the loop.
// ============================================================

// v1's text metrics: 6 px per character for the static prefix, 5 for the
// length of a scroll.
constexpr int16_t CHAR_ADVANCE = 6;
constexpr int16_t CHAR_W_SCROLL = 5;
constexpr int16_t ROW_H = 8;

void renderBegin();   // font and wrap settings on the canvas

void fillBlankRow(int16_t y);
void drawText(int16_t x, int16_t y, const char *text, uint16_t color);
// Right edge at x, as v1's drawRightAlignedText.
void drawRightAligned(int16_t x, int16_t y, const char *text, uint16_t color);
// Static text at x=0, with what it covers cleared first.
void drawStatic(int16_t y, int16_t clear_w, const char *text, uint16_t color);
void drawIcon(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *img);

// The red pixel in the bottom right corner: offline, but powered.
void drawOffline();

// One row of "static prefix, then text scrolling past it". start() draws the
// first frame; tick() moves it on by a pixel when its step is due and
// returns true when it drew; done() once it has snapped back.
//
// hold_ms keeps the text still at its start for that long before it moves,
// so it can be read. With only_if_needed, text that fits the row is drawn
// still and the row is done at once.
class ScrollRow {
 public:
  void start(int16_t y, const char *stat, const char *scroll, uint16_t c_static,
             uint16_t c_scroll, uint32_t step_ms, uint32_t hold_ms = 0,
             bool only_if_needed = false);
  bool tick(uint32_t now);
  bool done() const { return !active_; }
  void stop() { active_ = false; }

 private:
  void drawAt(int16_t x);
  bool     active_ = false;
  int16_t  y_ = 0, x_ = 0, offset_ = 0, limit_ = 0;
  uint32_t step_ms_ = 40, next_ms_ = 0;
  uint16_t c_static_ = 0, c_scroll_ = 0;
  char     stat_[16] = "", scroll_[96] = "";
};
