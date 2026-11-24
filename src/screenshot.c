#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "screenshot.h"
#include "savemenu.h"

#if defined(_MSC_VER)
#pragma warning(push, 1)
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

static const char kFolder[] = "capturas";

bool Screenshot_Write(const uint8 *pixels, int pitch, int w, int h, int factor, char *path, size_t path_size) {
#ifdef _WIN32
  _mkdir(kFolder);
#else
  mkdir(kFolder, 0755);
#endif
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char base[64];
  strftime(base, sizeof(base), "zelda3_%Y-%m-%d_%H-%M-%S", t);
  snprintf(path, path_size, "%s/%s.png", kFolder, base);
  for (int i = 2; i < 100; i++) {  // several in the same second
    FILE *f = fopen(path, "rb");
    if (!f)
      break;
    fclose(f);
    snprintf(path, path_size, "%s/%s_%d.png", kFolder, base, i);
  }
  if (factor < 1)
    factor = 1;
  int ow = w * factor, oh = h * factor;
  uint8 *rgb = malloc((size_t)ow * oh * 3);
  if (!rgb)
    return false;
  for (int y = 0; y < oh; y++) {
    const uint32 *src = (const uint32 *)(pixels + (size_t)(y / factor) * pitch);
    uint8 *d = rgb + (size_t)y * ow * 3;
    for (int x = 0; x < ow; x++) {
      uint32 p = src[x / factor];
      d[x * 3 + 0] = p >> 16, d[x * 3 + 1] = p >> 8, d[x * 3 + 2] = p;
    }
  }
  bool ok = stbi_write_png(path, ow, oh, 3, rgb, ow * 3) != 0;
  free(rgb);
  return ok;
}
