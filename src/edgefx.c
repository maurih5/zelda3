#include <string.h>
#include "types.h"
#include "edgefx.h"

// "Edge effect", after the one in the Relic of the Past launcher: in
// widescreen the PPU leaves the columns outside the current room/area black.
// Fill them with a blurred, darkened mirror of the picture next to them, so
// the bars blend into the scene instead of being hard black edges.

enum {
  kBlurX = 6,   // horizontal box blur radius
  kBlurY = 3,   // vertical box blur radius
  kMaxBar = 128,
};

static uint32 g_tmp[kMaxBar * 2 * 256];

// Fills bar columns [x0, x0 + w) of every row, mirroring the content across
// the edge at |edge| (dir = -1: content is to the right of the bar).
static void FillBar(uint8 *px, int pitch, int width, int height, int x0, int w, int edge, int dir, uint32 *tmp) {
  // Pass 1: mirrored source with a horizontal blur, darker away from the edge.
  for (int y = 0; y < height; y++) {
    const uint32 *row = (const uint32 *)(px + (size_t)y * pitch);
    for (int i = 0; i < w; i++) {
      int x = x0 + i;
      int d = dir < 0 ? (edge - 1 - x) : (x - edge);  // distance from the edge, 0 = next to it
      int r = 0, g = 0, b = 0, n = 0;
      for (int k = -kBlurX; k <= kBlurX; k++) {
        int sx = dir < 0 ? edge + d + k : edge - 1 - d + k;
        // stay inside the content
        if (dir < 0) { if (sx < edge) sx = edge; if (sx >= width) sx = width - 1; }
        else { if (sx >= edge) sx = edge - 1; if (sx < 0) sx = 0; }
        uint32 p = row[sx];
        r += p >> 16 & 0xff, g += p >> 8 & 0xff, b += p & 0xff, n++;
      }
      // 55% brightness at the edge fading to 25% at the far side
      int f = 140 - 76 * d / (w > 1 ? w - 1 : 1);
      r = r / n * f >> 8, g = g / n * f >> 8, b = b / n * f >> 8;
      tmp[y * w + i] = r << 16 | g << 8 | b;
    }
  }
  // Pass 2: vertical blur into the frame.
  for (int y = 0; y < height; y++) {
    uint32 *row = (uint32 *)(px + (size_t)y * pitch);
    for (int i = 0; i < w; i++) {
      int r = 0, g = 0, b = 0, n = 0;
      for (int k = -kBlurY; k <= kBlurY; k++) {
        int yy = y + k;
        if (yy < 0 || yy >= height)
          continue;
        uint32 p = tmp[yy * w + i];
        r += p >> 16 & 0xff, g += p >> 8 & 0xff, b += p & 0xff, n++;
      }
      row[x0 + i] = (r / n) << 16 | (g / n) << 8 | (b / n);
    }
  }
}

void EdgeEffect_Apply(uint8 *px, int pitch, int width, int height, int left_bar, int right_bar) {
  if (height > 256)
    return;
  int content = width - left_bar - right_bar;
  if (content < 2 * kBlurX + 2 || left_bar > kMaxBar || right_bar > kMaxBar)
    return;
  if (left_bar > 0)
    FillBar(px, pitch, width - right_bar, height, 0, left_bar, left_bar, -1, g_tmp);
  if (right_bar > 0)
    FillBar(px, pitch, width, height, width - right_bar, right_bar, width - right_bar, 1, g_tmp);
}
