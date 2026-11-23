#ifndef ZELDA3_SAVEMENU_H_
#define ZELDA3_SAVEMENU_H_

#include "types.h"

// Autosave (save states written periodically to saves/auto/) and an in-game
// menu, drawn on top of the game, to pick which save state to load.

enum {
  kSaveMenu_Up,
  kSaveMenu_Down,
  kSaveMenu_PageUp,
  kSaveMenu_PageDown,
  kSaveMenu_Ok,
  kSaveMenu_Back,
  kSaveMenu_Left,
  kSaveMenu_Right,
  kSaveMenu_PrevTab,
  kSaveMenu_NextTab,
  kSaveMenu_Clear,
};

// Tabs of the in-game menu
enum {
  kMenuTab_Saves,
  kMenuTab_Options,
  kMenuTab_Controls,
  kMenuTab_Count,
};

void SaveMenu_Init(int autosave_minutes, int autosave_slots);

// Called with the freshly rendered game frame (before any overlay is drawn).
void SaveMenu_CaptureFrame(const uint8 *pixels, int pitch, int scale, int width, int height);
// Draws notifications ("Guardado automático") on top of the game frame.
void SaveMenu_DrawOverlay(uint8 *pixels, int pitch, int scale, int width, int height);

// Call after each normally emulated frame (with the audio lock held). May autosave.
void SaveMenu_OnFramePlayed();
void SaveMenu_OnManualSave(int slot);
void SaveMenu_OnQuit();
void SaveMenu_ShowToast(const char *msg);
// Shows a message with a progress bar (permille 0..1000); NULL hides it.
void SaveMenu_SetProgress(const char *msg, int permille);

bool SaveMenu_IsOpen();
void SaveMenu_Toggle();
void SaveMenu_OpenTab(int tab);
int SaveMenu_GetTab();
// Call with the audio lock held: kSaveMenu_Ok loads a save state.
void SaveMenu_Input(int action);
void SaveMenu_Draw(uint8 *pixels, int pitch, int width, int height);

#endif  // ZELDA3_SAVEMENU_H_
