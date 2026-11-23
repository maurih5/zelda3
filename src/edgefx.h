#ifndef ZELDA3_EDGEFX_H_
#define ZELDA3_EDGEFX_H_

#include "types.h"

// Fills the black side bars that widescreen leaves outside the current room
// with a blurred, darkened mirror of the picture. Pixels are 0x00RRGGBB.
void EdgeEffect_Apply(uint8 *px, int pitch, int width, int height, int left_bar, int right_bar);

#endif  // ZELDA3_EDGEFX_H_
