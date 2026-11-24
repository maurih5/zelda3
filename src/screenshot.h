#ifndef ZELDA3_SCREENSHOT_H_
#define ZELDA3_SCREENSHOT_H_

#include <stddef.h>
#include "types.h"

// Saves a 32-bit XRGB frame as capturas/zelda3_<date>_<time>.png, each pixel
// made |factor| times bigger (nearest neighbor). |path| receives the file name.
bool Screenshot_Write(const uint8 *pixels, int pitch, int w, int h, int factor, char *path, size_t path_size);

#endif  // ZELDA3_SCREENSHOT_H_
