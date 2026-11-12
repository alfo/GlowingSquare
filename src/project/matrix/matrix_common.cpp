#include <math.h>
#include "project/matrix/matrix.h"

GFXcanvas16 &matrixCanvas() {
  static GFXcanvas16 canvas(MATRIX_W, MATRIX_H);
  return canvas;
}

uint8_t matrixPerceptual(uint8_t level) {
  if (level == 0) return 0;
  const float out = powf(level / 255.0f, 2.2f) * 255.0f;
  return out < 1.0f ? 1 : (uint8_t)lroundf(out);
}
