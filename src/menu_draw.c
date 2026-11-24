#include <stdio.h>
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


// ---------------------------------------------------------------------------
// Button prompts (console style: round face buttons, L/R pills, key caps)
// ---------------------------------------------------------------------------

enum { kGlyphH = 13 };
static const uint8 kCircleHalfW[kGlyphH] = { 2, 4, 5, 6, 6, 6, 6, 6, 6, 6, 5, 4, 2 };

static void FillPill(Canvas *c, int x, int y, int w, uint32 col) {
  // Rounded rectangle kGlyphH high with 3 px corners.
  static const uint8 kInset[kGlyphH] = { 3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3 };
  for (int r = 0; r < kGlyphH; r++)
    FillRect(c, x + kInset[r], y + r, w - 2 * kInset[r], 1, col, 256);
}

static void DrawCentered(Canvas *c, int x, int y, int w, const char *s, uint32 col) {
  DrawText(c, x + (w - MeasureText(s) + 1) / 2, y - 1, s, col, -1);
}

int ButtonGlyphWidth(const char *name, bool keyboard) {
  if (keyboard)
    return MeasureText(name) + 8;
  if (!strcmp(name, "DPAD"))
    return 13;
  if (strlen(name) == 1 && strchr("ABXY", name[0]))
    return 13;
  return MeasureText(name) + 10;
}

int DrawButtonGlyph(Canvas *c, int x, int y, const char *name, bool keyboard) {
  const uint32 kFace = 0xe8eaf0, kInk = 0x182038;
  int w = ButtonGlyphWidth(name, keyboard);
  if (keyboard) {
    // Key cap: light outline, dark inside.
    FillPill(c, x, y, w, kCol_Gray);
    FillRect(c, x + 1, y + 1, w - 2, kGlyphH - 3, kCol_Panel, 256);
    DrawCentered(c, x, y, w, name, kCol_White);
    return w;
  }
  if (!strcmp(name, "DPAD")) {
    FillRect(c, x + 4, y, 5, kGlyphH, kFace, 256);
    FillRect(c, x, y + 4, kGlyphH, 5, kFace, 256);
    FillRect(c, x + 5, y + 5, 3, 3, kInk, 256);
    return w;
  }
  if (w == 13) {
    for (int r = 0; r < kGlyphH; r++)
      FillRect(c, x + 6 - kCircleHalfW[r], y + r, 2 * kCircleHalfW[r] + 1, 1, kFace, 256);
    DrawCentered(c, x, y, w, name, kInk);
    return w;
  }
  FillPill(c, x, y, w, kFace);
  DrawCentered(c, x, y, w, name, kInk);
  return w;
}

// |spec| is "BUTTON:Label|BUTTON:Label|..." with BUTTON one of A B X Y L R
// LR DPAD. Drawn right-aligned ending at |right|; items that don't fit are
// dropped from the left.
void DrawHintBar(Canvas *c, int left, int right, int y, const char *spec, bool keyboard) {
  enum { kMax = 8 };
  char btn[kMax][8], label[kMax][32];
  int n = 0;
  for (const char *p = spec; *p && n < kMax;) {
    const char *colon = strchr(p, ':'), *bar = strchr(p, '|');
    if (!colon || (bar && bar < colon))
      break;
    const char *end = bar ? bar : p + strlen(p);
    snprintf(btn[n], sizeof(btn[n]), "%.*s", (int)(colon - p), p);
    snprintf(label[n], sizeof(label[n]), "%.*s", (int)(end - colon - 1), colon + 1);
    n++;
    p = bar ? bar + 1 : end;
  }
  // Keyboard names for the buttons.
  const char *names[kMax][2];
  int item_w[kMax];
  for (int i = 0; i < n; i++) {
    const char *b = btn[i];
    names[i][0] = b, names[i][1] = NULL;
    if (!strcmp(b, "LR")) names[i][0] = "L", names[i][1] = "R";
    if (keyboard) {
      names[i][1] = NULL;
      names[i][0] = !strcmp(b, "A") ? "Enter" : !strcmp(b, "B") ? "Esc" : !strcmp(b, "X") ? "Supr" :
                    !strcmp(b, "LR") ? "Tab" : !strcmp(b, "DPAD") ? "Flechas" : b;
    }
    item_w[i] = ButtonGlyphWidth(names[i][0], keyboard) + 3 + MeasureText(label[i]);
    if (names[i][1])
      item_w[i] += 2 + ButtonGlyphWidth(names[i][1], keyboard);
  }
  const int kGap = 12;
  int first = 0, total;
  for (;;) {
    total = 0;
    for (int i = first; i < n; i++)
      total += item_w[i] + (i > first ? kGap : 0);
    if (total <= right - left || first >= n - 1)
      break;
    first++;
  }
  int x = right - total;
  for (int i = first; i < n; i++) {
    x += DrawButtonGlyph(c, x, y, names[i][0], keyboard);
    if (names[i][1]) {
      x += 2;
      x += DrawButtonGlyph(c, x, y, names[i][1], keyboard);
    }
    x += 3;
    DrawText(c, x, y - 1, label[i], kCol_White, -1);
    x += MeasureText(label[i]) + kGap;
  }
}
