#include <string.h>
#include "menu_draw.h"
#include "menu_font.h"

void FillRect(Canvas *c, int x, int y, int w, int h, uint32 col, int alpha) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > c->w) w = c->w - x;
  if (y + h > c->h) h = c->h - y;
  if (w <= 0 || h <= 0)
    return;
  int s = c->scale;
  for (int py = y * s; py < (y + h) * s; py++) {
    uint32 *row = (uint32 *)(c->px + py * c->pitch);
    for (int px = x * s; px < (x + w) * s; px++)
      row[px] = BlendPixel(row[px], col, alpha);
  }
}

void DrawFrame(Canvas *c, int x, int y, int w, int h, uint32 col) {
  FillRect(c, x, y, w, 1, col, 256);
  FillRect(c, x, y + h - 1, w, 1, col, 256);
  FillRect(c, x, y, 1, h, col, 256);
  FillRect(c, x + w - 1, y, 1, h, col, 256);
}

int DecodeUtf8(const char **ps) {
  const uint8 *s = (const uint8 *)*ps;
  int c = *s++;
  if (c >= 0xc0 && c < 0xe0 && (*s & 0xc0) == 0x80) {
    c = (c & 0x1f) << 6 | (*s++ & 0x3f);
  } else if (c >= 0xe0 && c < 0xf0 && (s[0] & 0xc0) == 0x80 && (s[1] & 0xc0) == 0x80) {
    c = (c & 0x0f) << 12 | (s[0] & 0x3f) << 6 | (s[1] & 0x3f);
    s += 2;
  }
  *ps = (const char *)s;
  return c;
}

int GlyphIndex(int cp) {
  if (cp >= 32 && cp < 127)
    return cp - 32;
  for (int i = 0; i < MENU_FONT_NUM_EXTRA; i++)
    if (kMenuFontExtraCp[i] == cp)
      return 95 + i;
  return '?' - 32;
}

int MeasureText(const char *s) {
  int w = 0;
  while (*s)
    w += kMenuFontWidth[GlyphIndex(DecodeUtf8(&s))];
  return w;
}

void DrawGlyphs(Canvas *c, int x, int y, const char *s, uint32 col, int max_w) {
  int x0 = x;
  while (*s) {
    int gi = GlyphIndex(DecodeUtf8(&s));
    int gw = kMenuFontWidth[gi];
    if (max_w >= 0 && x + gw - x0 > max_w)
      break;
    for (int yy = 0; yy < MENU_FONT_HEIGHT; yy++) {
      uint16 bits = kMenuFontRows[gi][yy];
      for (int xx = 0; bits; xx++, bits >>= 1)
        if (bits & 1)
          FillRect(c, x + xx, y + yy, 1, 1, col, 256);
    }
    x += gw;
  }
}

// Draws text with a drop shadow. Text wider than |max_w| (if >= 0) is cut with "...".
void DrawText(Canvas *c, int x, int y, const char *s, uint32 col, int max_w) {
  char buf[160];
  if (max_w >= 0 && MeasureText(s) > max_w) {
    int dots = MeasureText("...");
    const char *p = s, *last = s;
    int w = 0;
    while (*p) {
      const char *q = p;
      int gw = kMenuFontWidth[GlyphIndex(DecodeUtf8(&q))];
      if (w + gw + dots > max_w)
        break;
      w += gw;
      p = q;
      last = p;
    }
    size_t n = last - s;
    if (n > sizeof(buf) - 4) n = sizeof(buf) - 4;
    memcpy(buf, s, n);
    memcpy(buf + n, "...", 4);
    s = buf;
  }
  DrawGlyphs(c, x + 1, y + 1, s, kCol_Black, -1);
  DrawGlyphs(c, x, y, s, col, -1);
}

void DrawHeart(Canvas *c, int x, int y, int fill /* 0, 1=half, 2=full */) {
  static const char *const kHeart[6] = {
    ".XX.XX.",
    "XXXXXXX",
    "XXXXXXX",
    ".XXXXX.",
    "..XXX..",
    "...X...",
  };
  for (int yy = 0; yy < 6; yy++)
    for (int xx = 0; xx < 7; xx++)
      if (kHeart[yy][xx] == 'X') {
        bool red = fill == 2 || (fill == 1 && xx < 4);
        FillRect(c, x + xx + 1, y + yy + 1, 1, 1, kCol_Black, 160);
        FillRect(c, x + xx, y + yy, 1, 1, red ? kCol_Heart : kCol_HeartEmpty, 256);
      }
}

// hearts/max are in 1/8 heart units as in the game RAM. Returns height used.
int DrawHearts(Canvas *c, int x, int y, int hearts, int max_hearts, int max_w) {
  int n = (max_hearts + 7) / 8;
  if (n <= 0 || n > 30)
    return 0;
  int per_row = max_w / 8;
  if (per_row < 1) per_row = 1;
  if (per_row > 10) per_row = 10;
  for (int i = 0; i < n; i++) {
    int v = hearts - i * 8;
    int fill = v >= 8 ? 2 : v >= 4 ? 1 : 0;
    DrawHeart(c, x + (i % per_row) * 8, y + (i / per_row) * 8, fill);
  }
  return ((n + per_row - 1) / per_row) * 8;
}

void DrawImage(Canvas *c, int x, int y, int dw, int dh, const uint32 *src, int sw, int sh) {
  for (int yy = 0; yy < dh; yy++) {
    const uint32 *srow = src + (size_t)(yy * sh / dh) * sw;
    for (int xx = 0; xx < dw; xx++)
      FillRect(c, x + xx, y + yy, 1, 1, srow[xx * sw / dw], 256);
  }
}

