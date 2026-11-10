#include "project/matrix/matrix.h"

GFXcanvas16 &matrixCanvas() {
  static GFXcanvas16 canvas(MATRIX_W, MATRIX_H);
  return canvas;
}
