// Discord Rich Presence ("Jugando a ..." with the current place, hearts and
// progress), spoken directly over Discord's local IPC pipe. The pipe work
// runs on its own thread so a slow or missing Discord never stalls the game.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "discord.h"
#include "zelda_rtl.h"
#include "variables.h"


enum {
  kDiscord_MinSendMs = 5000,     // Discord allows ~5 updates per 20 s
  kDiscord_RetryMs = 20000,      // how often to look for Discord again
  kDiscord_SampleFrames = 60,    // how often the game state is read
};

static char g_app_id[32];
static bool g_enabled, g_show_location = true;
static SDL_mutex *g_lock;
static SDL_Thread *g_thread;
static volatile int g_quit;
static char g_details[128], g_state[128];  // protected by g_lock
static int g_version;                      // bumped when the text changes
static int64_t g_start_time;
static int g_frame;

// ---------------------------------------------------------------------------
// What to show
// ---------------------------------------------------------------------------

static const char *const kDungeonNames[14] = {
  "Alcantarillas del castillo", "Castillo de Hyrule", "Palacio del Este", "Palacio del Desierto",
  "Torre de Agahnim", "Palacio del Pantano", "Palacio de la Oscuridad", "Ciénaga de la Miseria",
  "Bosque de los Cráneos", "Palacio de Hielo", "Torre de Hera", "Pueblo de los Ladrones",
  "Roca Tortuga", "Torre de Ganon",
};

static const char *OverworldPlace(int screen) {
  switch (screen) {
  case 0x00: return "Bosque Perdido";
  case 0x03: case 0x05: case 0x07: case 0x0a: return "Montaña de la Muerte";
  case 0x13: return "Santuario";
  case 0x14: return "Cementerio";
  case 0x18: return "Kakariko";
  case 0x1b: return "Castillo de Hyrule";
  case 0x1e: return "Palacio del Este";
  case 0x2c: return "Casa de Link";
  case 0x30: return "Desierto";
  case 0x35: return "Lago Hylia";
  case 0x40: return "Bosque de los Cráneos";
  case 0x43: case 0x45: case 0x47: return "Montaña de la Muerte oscura";
  case 0x58: return "Pueblo de los Ladrones";
  case 0x5b: return "Pirámide";
  case 0x5e: return "Palacio de la Oscuridad";
  case 0x70: return "Ciénaga de la Miseria";
  case 0x75: return "Lago de Hielo";
  case 0x80: return "Pedestal de la Espada Maestra";
  case 0x81: return "Cascada de Zora";
  }
  return NULL;
}

static int CountBits(int v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}

static void Describe(char *details, size_t dn, char *state, size_t sn) {
  int mod = main_module_index;
  state[0] = 0;
  if (mod == 0 || mod == 0x14) {
    snprintf(details, dn, "Viendo la intro");
    return;
  }
  if (mod >= 1 && mod <= 4) {
    snprintf(details, dn, "Eligiendo partida");
    return;
  }
  if (mod == 0x19 || mod == 0x1a) {
    snprintf(details, dn, "Viendo el final");
    return;
  }

  int hearts = link_health_capacity / 8, cur = (link_health_current + 7) / 8;
  int pendants = CountBits(link_which_pendants & 7), crystals = CountBits(link_has_crystals & 0x7f);
  if (crystals || pendants == 3)
    snprintf(state, sn, "%d/%d corazones · %d/7 cristales", cur, hearts, crystals);
  else
    snprintf(state, sn, "%d/%d corazones · %d/3 colgantes", cur, hearts, pendants);

  if (!g_show_location) {
    snprintf(details, dn, "Jugando");
    return;
  }
  if (mod == 0x12) {
    snprintf(details, dn, "Game over");
    return;
  }
  bool dark = savegame_is_darkworld != 0;
  if (player_is_indoors) {
    int d = (uint8)cur_palace_index_x2;
    if (d != 0xff && d / 2 < 14)
      snprintf(details, dn, "%s", kDungeonNames[d / 2]);
    else
      snprintf(details, dn, "%s · bajo techo", dark ? "Mundo Oscuro" : "Mundo de la Luz");
    return;
  }
  const char *place = OverworldPlace(overworld_screen_index);
  const char *world = overworld_screen_index & 0x40 ? "Mundo Oscuro" : "Mundo de la Luz";
  if (place)
    snprintf(details, dn, "%s · %s", world, place);
  else
    snprintf(details, dn, "%s", world);
}

// ---------------------------------------------------------------------------
// IPC
// ---------------------------------------------------------------------------

static void JsonString(char *dst, size_t n, const char *s) {
  size_t j = 0;
  if (n < 3) { dst[0] = 0; return; }
  dst[j++] = '"';
  for (; *s && j + 3 < n; s++) {
    if (*s == '"' || *s == '\\')
      dst[j++] = '\\';
    dst[j++] = *s;
  }
  dst[j++] = '"';
  dst[j] = 0;
}

#ifdef _WIN32
static HANDLE g_pipe = INVALID_HANDLE_VALUE;

static void PipeClose(void) {
  if (g_pipe != INVALID_HANDLE_VALUE)
    CloseHandle(g_pipe);
  g_pipe = INVALID_HANDLE_VALUE;
}

static bool PipeWrite(uint32 op, const char *json) {
  uint32 len = (uint32)strlen(json);
  uint8 *buf = malloc(8 + len);
  if (!buf) return false;
  memcpy(buf, &op, 4);
  memcpy(buf + 4, &len, 4);
  memcpy(buf + 8, json, len);
  DWORD written = 0;
  BOOL ok = WriteFile(g_pipe, buf, 8 + len, &written, NULL);
  free(buf);
  return ok && written == 8 + len;
}

// Reads and drops whatever Discord sent. Returns false if the pipe is gone.
static bool PipeDrain(void) {
  for (;;) {
    DWORD avail = 0;
    if (!PeekNamedPipe(g_pipe, NULL, 0, NULL, &avail, NULL))
      return false;
    if (avail == 0)
      return true;
    char buf[1025];
    DWORD got = 0;
    if (!ReadFile(g_pipe, buf, avail < sizeof(buf) - 1 ? avail : sizeof(buf) - 1, &got, NULL))
      return false;
    buf[got] = 0;
    for (DWORD i = 0; i < got; i++)
      if (buf[i] == 0) buf[i] = ' ';  // frame headers
    if (strstr(buf, "\"evt\":\"ERROR\""))
      printf("Discord: error: %s\n", buf + (got > 8 ? 8 : 0));
    else if (getenv("ZELDA3_DISCORD_DEBUG"))
      printf("Discord: <- %s\n", buf + (got > 8 ? 8 : 0));
  }
}

static bool PipeConnect(void) {
  for (int i = 0; i < 10; i++) {
    char name[64];
    snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
    g_pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (g_pipe != INVALID_HANDLE_VALUE)
      break;
  }
  if (g_pipe == INVALID_HANDLE_VALUE)
    return false;
  char hello[96];
  snprintf(hello, sizeof(hello), "{\"v\":1,\"client_id\":\"%s\"}", g_app_id);
  if (!PipeWrite(0, hello)) {
    PipeClose();
    return false;
  }
  // Wait (briefly) for the READY reply.
  for (int t = 0; t < 50; t++) {
    DWORD avail = 0;
    if (!PeekNamedPipe(g_pipe, NULL, 0, NULL, &avail, NULL)) {
      PipeClose();
      return false;
    }
    if (avail)
      break;
    SDL_Delay(20);
  }
  if (!PipeDrain()) {
    PipeClose();
    return false;
  }
  return true;
}
#define PipeIsOpen() (g_pipe != INVALID_HANDLE_VALUE)
#else
static bool PipeConnect(void) { return false; }
static bool PipeWrite(uint32 op, const char *json) { return false; }
static bool PipeDrain(void) { return false; }
static void PipeClose(void) {}
#define PipeIsOpen() false
#endif

static bool SendActivity(const char *details, const char *state, bool clear) {
  static int nonce;
  char body[1024], d[160], s[160];
  int pid = 0;
#ifdef _WIN32
  pid = (int)GetCurrentProcessId();
#endif
  if (clear) {
    snprintf(body, sizeof(body),
             "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d},\"nonce\":\"%d\"}", pid, ++nonce);
  } else {
    JsonString(d, sizeof(d), details);
    char state_part[200] = "";
    if (state[0]) {
      JsonString(s, sizeof(s), state);
      snprintf(state_part, sizeof(state_part), "\"state\":%s,", s);
    }
    snprintf(body, sizeof(body),
             "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":{"
             "\"details\":%s,%s"
             "\"timestamps\":{\"start\":%lld},"
             "\"assets\":{\"large_image\":\"logo\",\"large_text\":\"A Link to the Past\"}"
             "}},\"nonce\":\"%d\"}",
             pid, d, state_part, (long long)g_start_time, ++nonce);
  }
  return PipeWrite(1, body);
}

static int DiscordThread(void *arg) {
  uint32 last_try = 0, last_send = 0;
  int sent_version = -1;
  bool shown = false;
  bool first_try = true;
  while (!g_quit) {
    SDL_Delay(100);
    uint32 now = SDL_GetTicks();
    SDL_LockMutex(g_lock);
    bool enabled = g_enabled;
    int version = g_version;
    char details[128], state[128];
    memcpy(details, g_details, sizeof(details));
    memcpy(state, g_state, sizeof(state));
    SDL_UnlockMutex(g_lock);

    if (!enabled) {
      if (PipeIsOpen()) {
        if (shown)
          SendActivity(NULL, NULL, true);
        PipeClose();
      }
      shown = false;
      sent_version = -1;
      continue;
    }
    if (!PipeIsOpen()) {
      if (!first_try && now - last_try < kDiscord_RetryMs)
        continue;
      first_try = false;
      last_try = now;
      if (!PipeConnect())
        continue;
      printf("Discord: conectado\n");
      sent_version = -1;
    }
    if (!PipeDrain()) {
      PipeClose();
      shown = false;
      continue;
    }
    if (details[0] && version != sent_version && (now - last_send >= kDiscord_MinSendMs || sent_version < 0)) {
      if (!SendActivity(details, state, false)) {
        PipeClose();
        shown = false;
        continue;
      }
      printf("Discord: %s | %s\n", details, state);
      fflush(stdout);
      shown = true;
      last_send = now;
      sent_version = version;
    }
  }
  PipeClose();  // closing the pipe also clears the status
  return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Discord_Init(const char *app_id) {
  snprintf(g_app_id, sizeof(g_app_id), "%s", app_id ? app_id : "");
  for (char *p = g_app_id; *p; p++)
    if (*p < '0' || *p > '9') { g_app_id[0] = 0; break; }
  if (!g_app_id[0])
    return;
#if defined(SAVEMENU_SELFTEST) || defined(REWIND_SELFTEST)
  if (!getenv("ZELDA3_SELFTEST_DISCORD"))
    return;  // automated tests must not touch the real Discord
#endif
  g_start_time = (int64_t)time(NULL);
  g_lock = SDL_CreateMutex();
  g_thread = SDL_CreateThread(DiscordThread, "discord", NULL);
}

void Discord_Configure(bool enabled, bool show_location) {
  if (!g_lock) {
    g_enabled = enabled, g_show_location = show_location;
    return;
  }
  SDL_LockMutex(g_lock);
  g_enabled = enabled;
  if (g_show_location != show_location) {
    g_show_location = show_location;
    g_frame = kDiscord_SampleFrames;  // describe again on the next frame
  }
  SDL_UnlockMutex(g_lock);
}

void Discord_Update(void) {
  if (!g_lock || !g_enabled || ++g_frame < kDiscord_SampleFrames)
    return;
  g_frame = 0;
  char details[128], state[128];
  Describe(details, sizeof(details), state, sizeof(state));
  SDL_LockMutex(g_lock);
  if (strcmp(details, g_details) || strcmp(state, g_state)) {
    memcpy(g_details, details, sizeof(details));
    memcpy(g_state, state, sizeof(state));
    g_version++;
  }
  SDL_UnlockMutex(g_lock);
}

void Discord_Shutdown(void) {
  if (!g_thread)
    return;
  g_quit = 1;
  SDL_WaitThread(g_thread, NULL);
  g_thread = NULL;
}
