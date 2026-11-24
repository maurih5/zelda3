#ifndef ZELDA3_STATS_H_
#define ZELDA3_STATS_H_

#include "types.h"

struct Canvas;

// Play time and statistics of each save file (kept in saves/estadisticas.ini,
// separate from the game's save), achievement notices and the "Progreso" tab
// of the menu.

void Stats_Init();
// After every game frame that was really played (not rewinds).
void Stats_OnFrame();
// The game state jumped (state loaded, rewind, reset): don't treat the
// differences as things that just happened.
void Stats_Resync();
void Stats_Shutdown();

// Called from the game code.
void Stats_OnHurt();
void Stats_OnEnemyDefeated();
// Save file management on the file select screen (slot = 0-2).
void Stats_OnFileCreated(int slot);
void Stats_OnFileCopied(int from, int to);
void Stats_OnFileErased(int slot);

// Save file being played (0-2), or -1.
int Stats_CurrentFile();
// Total play time of a save file, in seconds.
uint32 Stats_PlaySeconds(int slot);
// "12 h 05 min", "35 min"
void Stats_FormatTime(uint32 seconds, char *buf, size_t n);

// "Progreso" tab
bool Stats_MenuInput(int action);  // false = close the menu
void Stats_MenuDraw(struct Canvas *c, int top, int bottom);

#endif  // ZELDA3_STATS_H_
