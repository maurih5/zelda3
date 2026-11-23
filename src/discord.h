#ifndef ZELDA3_DISCORD_H_
#define ZELDA3_DISCORD_H_

#include <stdbool.h>

// Discord Rich Presence through the local Discord client's IPC pipe.
// Nothing happens (and nothing fails) if Discord isn't running.
void Discord_Init(const char *app_id);
void Discord_Configure(bool enabled, bool show_location);
// Called every frame from the main thread; reads the game state now and then.
void Discord_Update(void);
void Discord_Shutdown(void);

#endif  // ZELDA3_DISCORD_H_
