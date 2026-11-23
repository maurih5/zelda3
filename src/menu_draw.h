#ifndef ZELDA3_MENU_DRAW_H_
#define ZELDA3_MENU_DRAW_H_

#include "types.h"
#define MENU_FONT_HEIGHT 14  // must match menu_font.h

// Drawing helpers for the in-game menus. Coordinates are in SNES pixels,
// |scale| is the size of one SNES pixel in the output buffer.

typedef struct Canvas {
  uint8 *px;
  int pitch, scale, w, h;
} Canvas;

enum {
  kCol_White = 0xf8f8f8,
  kCol_Gray = 0xa8b0c8,
  kCol_Gold = 0xf0c858,
  kCol_DarkGold = 0x987830,
  kCol_Panel = 0x101838,
  kCol_Select = 0x2c3c88,
  kCol_Black = 0x000000,
  kCol_Heart = 0xe82838,
  kCol_HeartEmpty = 0x483848,
};

static inline uint32 BlendPixel(uint32 dst, uint32 src, int a) {
  if (a >= 256)
    return src;
  uint32 rb = ((src & 0xff00ff) * a + (dst & 0xff00ff) * (256 - a)) >> 8;
  uint32 g = ((src & 0x00ff00) * a + (dst & 0x00ff00) * (256 - a)) >> 8;
  return (rb & 0xff00ff) | (g & 0x00ff00);
}

void FillRect(Canvas *c, int x, int y, int w, int h, uint32 col, int alpha);
void DrawFrame(Canvas *c, int x, int y, int w, int h, uint32 col);
int DecodeUtf8(const char **ps);
int GlyphIndex(int cp);
int MeasureText(const char *s);
void DrawGlyphs(Canvas *c, int x, int y, const char *s, uint32 col, int max_w);
void DrawText(Canvas *c, int x, int y, const char *s, uint32 col, int max_w);
void DrawHeart(Canvas *c, int x, int y, int fill /* 0, 1=half, 2=full */);
int DrawHearts(Canvas *c, int x, int y, int hearts, int max_hearts, int max_w);
void DrawImage(Canvas *c, int x, int y, int dw, int dh, const uint32 *src, int sw, int sh);

#endif  // ZELDA3_MENU_DRAW_H_
