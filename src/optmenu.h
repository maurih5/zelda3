#ifndef ZELDA3_OPTMENU_H_
#define ZELDA3_OPTMENU_H_

#include "types.h"
#include "menu_draw.h"
#include <SDL_keycode.h>

// "Opciones" and "Controles" pages of the in-game menu (see savemenu.c, which
// owns the menu and its tabs). Every change is applied immediately and saved
// to zelda3.opciones.ini.

enum {
  kOptPage_Options,
  kOptPage_Controls,
};

void OptMenu_Open();
// |action| is one of the kSaveMenu_* values. Returns false if the page wants
// the menu to close (Back pressed at the top level).
bool OptMenu_Input(int page, int action);
void OptMenu_Draw(int page, Canvas *c, int top, int bottom);
const char *OptMenu_Hint(int page, bool wide);

// While waiting for a key/button to bind, all raw input goes here.
bool OptMenu_IsCapturing();
void OptMenu_CaptureKey(SDL_Keycode key, SDL_Keymod mod, bool pressed);
void OptMenu_CaptureButton(int button, bool pressed);

// Applies the saved volumes etc at startup.
void OptMenu_ApplyStartupSettings();
// Volume hotkeys. Returns the new master volume.
int OptMenu_ChangeVolume(int delta);

// Implemented in main.c
bool Main_IsFullscreen();
void Main_SetFullscreen(bool on);
int Main_GetRenderFlags();
void Main_SetRenderFlags(int flags);
bool Main_ShaderSupported();
bool Main_SetShader(const char *path);

#endif  // ZELDA3_OPTMENU_H_
