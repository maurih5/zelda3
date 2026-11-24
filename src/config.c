#include "config.h"
#include "types.h"
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "features.h"
#include "util.h"

enum {
  kKeyMod_ScanCode = 0x200,
  kKeyMod_Alt = 0x400,
  kKeyMod_Shift = 0x800,
  kKeyMod_Ctrl = 0x1000,
};

Config g_config;

#define REMAP_SDL_KEYCODE(key) ((key) & SDLK_SCANCODE_MASK ? kKeyMod_ScanCode : 0) | (key) & (kKeyMod_ScanCode - 1)
#define _(x) REMAP_SDL_KEYCODE(x)
#define S(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Shift
#define A(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Alt
#define C(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Ctrl
#define N 0
static const uint16 kDefaultKbdControls[kKeys_Total] = {
  0,
  // Controls
  _(SDLK_UP), _(SDLK_DOWN), _(SDLK_LEFT), _(SDLK_RIGHT), _(SDLK_RSHIFT), _(SDLK_RETURN), _(SDLK_x), _(SDLK_z), _(SDLK_s), _(SDLK_a), _(SDLK_c), _(SDLK_v),
  // LoadState
  _(SDLK_F1), _(SDLK_F2), _(SDLK_F3), _(SDLK_F4), _(SDLK_F5), _(SDLK_F6), _(SDLK_F7), _(SDLK_F8), _(SDLK_F9), _(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // SaveState
  S(SDLK_F1), S(SDLK_F2), S(SDLK_F3), S(SDLK_F4), S(SDLK_F5), S(SDLK_F6), S(SDLK_F7), S(SDLK_F8), S(SDLK_F9), S(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Replay State
  C(SDLK_F1), C(SDLK_F2), C(SDLK_F3), C(SDLK_F4), C(SDLK_F5), C(SDLK_F6), C(SDLK_F7), C(SDLK_F8), C(SDLK_F9), C(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Load Ref State
  N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
  // Replay Ref State
  N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
  // CheatLife, CheatKeys, CheatEquipment, CheatWalkThroughWalls
  // No keys by default, so they can't be triggered by accident (WASD players).
  N, N, N, N,
  // ClearKeyLog, StopReplay, Fullscreen, Reset, Pause, PauseDimmed, Turbo, ReplayTurbo, WindowBigger, WindowSmaller, DisplayPerf, ToggleRenderer
  _(SDLK_k), _(SDLK_l), A(SDLK_RETURN), C(SDLK_r), S(SDLK_p), _(SDLK_p), _(SDLK_TAB), _(SDLK_t), N, N, _(SDLK_f), _(SDLK_r),
  // VolumeUp, VolumeDown, Rewind, SaveMenu, Quit, Screenshot
  N, N, _(SDLK_BACKSPACE), _(SDLK_ESCAPE), N, _(SDLK_F12),
};
#undef _
#undef A
#undef C
#undef S
#undef N

typedef struct KeyNameId {
  const char *name;
  uint16 id, size;
} KeyNameId;

#define M(n) {#n, kKeys_##n, kKeys_##n##_Last - kKeys_##n + 1}
#define S(n) {#n, kKeys_##n, 1}
static const KeyNameId kKeyNameId[] = {
  {"Null", kKeys_Null, 65535},
  M(Controls), M(Load), M(Save), M(Replay), M(LoadRef), M(ReplayRef),
  S(CheatLife), S(CheatKeys), S(CheatEquipment), S(CheatWalkThroughWalls),
  S(ClearKeyLog), S(StopReplay), S(Fullscreen), S(Reset),
  S(Pause), S(PauseDimmed), S(Turbo), S(ReplayTurbo), S(WindowBigger), S(WindowSmaller), S(VolumeUp), S(VolumeDown), S(DisplayPerf), S(ToggleRenderer),
  S(Rewind), S(SaveMenu), S(Quit), S(Screenshot),
};
#undef S
#undef M
typedef struct KeyMapHashEnt {
  uint16 key, cmd, next;
} KeyMapHashEnt;

static uint16 keymap_hash_first[255];
static KeyMapHashEnt *keymap_hash;
static int keymap_hash_size;

// One keyboard key and one gamepad combo per command. The lookup tables
// below are rebuilt from these whenever a binding changes, so bindings can
// be changed while the game runs.
static uint16 g_kbd_bind[kKeys_Total];
typedef struct PadBind {
  int8 button;  // kGamepadBtn_Invalid = none
  uint32 modifiers;
} PadBind;
static PadBind g_pad_bind[kKeys_Total];

static void KeyMapHash_Add(uint16 key, uint16 cmd) {
  if ((keymap_hash_size & 0xff) == 0) {
    if (keymap_hash_size > 10000)
      Die("Too many keys");
    keymap_hash = realloc(keymap_hash, sizeof(KeyMapHashEnt) * (keymap_hash_size + 256));
  }
  int i = keymap_hash_size++;
  KeyMapHashEnt *ent = &keymap_hash[i];
  ent->key = key;
  ent->cmd = cmd;
  ent->next = 0;
  int j = (uint32)key % 255;

  uint16 *cur = &keymap_hash_first[j];
  while (*cur) {
    KeyMapHashEnt *ent = &keymap_hash[*cur - 1];
    cur = &ent->next;
  }
  *cur = i + 1;
}

static int KeyMapHash_Find(uint16 key) {
  int i = keymap_hash_first[key % 255];
  while (i) {
    KeyMapHashEnt *ent = &keymap_hash[i - 1];
    if (ent->key == key)
      return ent->cmd;
    i = ent->next;
  }
  return 0;
}

static int EncodeSdlKey(SDL_Keycode code, SDL_Keymod mod) {
  if (code & ~(SDLK_SCANCODE_MASK | 0x1ff))
    return 0;
  int key = 0;
  if (code != SDLK_LALT && code != SDLK_RALT)
    key |=  mod & KMOD_ALT ? kKeyMod_Alt : 0;
  if (code != SDLK_LCTRL && code != SDLK_RCTRL)
    key |= mod & KMOD_CTRL ? kKeyMod_Ctrl : 0;
  if (code != SDLK_LSHIFT && code != SDLK_RSHIFT)
    key |= mod & KMOD_SHIFT ? kKeyMod_Shift : 0;
  key |= REMAP_SDL_KEYCODE(code);
  return key;
}

int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod) {
  int key = EncodeSdlKey(code, mod);
  return key ? KeyMapHash_Find(key) : 0;
}

// Assigns |key| to |cmd|, removing it from any other command. Returns the
// command that lost the key (or 0).
static int SetKbdBinding(int cmd, int key) {
  int stolen = 0;
  if (key) {
    for (int i = 1; i < kKeys_Total; i++) {
      if (i != cmd && g_kbd_bind[i] == key) {
        g_kbd_bind[i] = 0;
        stolen = i;
      }
    }
  }
  g_kbd_bind[cmd] = key;
  return stolen;
}

static int SetPadBinding(int cmd, int button, uint32 modifiers) {
  int stolen = 0;
  modifiers &= ~(1u << button);
  if (button != kGamepadBtn_Invalid) {
    for (int i = 1; i < kKeys_Total; i++) {
      if (i != cmd && g_pad_bind[i].button == button && g_pad_bind[i].modifiers == modifiers) {
        g_pad_bind[i].button = kGamepadBtn_Invalid;
        stolen = i;
      }
    }
  }
  g_pad_bind[cmd].button = button;
  g_pad_bind[cmd].modifiers = button != kGamepadBtn_Invalid ? modifiers : 0;
  return stolen;
}

static void ParseKeyArray(char *value, int cmd, int size) {
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    int key_with_mod = 0;
    for (;;) {
      if (StringStartsWithNoCase(s, "Shift+")) {
        key_with_mod |= kKeyMod_Shift, s += 6;
      } else if (StringStartsWithNoCase(s, "Ctrl+")) {
        key_with_mod |= kKeyMod_Ctrl, s += 5;
      } else if (StringStartsWithNoCase(s, "Alt+")) {
        key_with_mod |= kKeyMod_Alt, s += 4;
      } else {
        break;
      }
    }
    SDL_Keycode key = SDL_GetKeyFromName(s);
    if (key == SDLK_UNKNOWN) {
      fprintf(stderr, "Unknown key: '%s'\n", s);
      continue;
    }
    // If another command had this key, the last assignment wins.
    SetKbdBinding(cmd, key_with_mod | REMAP_SDL_KEYCODE(key));
  }
}

typedef struct GamepadMapEnt {
  uint32 modifiers;
  uint16 cmd, next;
} GamepadMapEnt;

static uint16 joymap_first[kGamepadBtn_Count];
static GamepadMapEnt *joymap_ents;
static int joymap_size;

static int CountBits32(uint32 n) {
  int count = 0;
  for (; n != 0; count++)
    n &= (n - 1);
  return count;
}

static void GamepadMap_Add(int button, uint32 modifiers, uint16 cmd) {
  if ((joymap_size & 0xff) == 0) {
    if (joymap_size > 1000)
      Die("Too many joypad keys");
    joymap_ents = realloc(joymap_ents, sizeof(GamepadMapEnt) * (joymap_size + 64));
    if (!joymap_ents) Die("realloc failure");
  }
  uint16 *p = &joymap_first[button];
  // Insert it as early as possible but before after any entry with more modifiers.
  int cb = CountBits32(modifiers);
  while (*p && cb < CountBits32(joymap_ents[*p - 1].modifiers))
    p = &joymap_ents[*p - 1].next;
  int i = joymap_size++;
  GamepadMapEnt *ent = &joymap_ents[i];
  ent->modifiers = modifiers;
  ent->cmd = cmd;
  ent->next = *p;
  *p = i + 1;
}

int FindCmdForGamepadButton(int button, uint32 modifiers) {
  GamepadMapEnt *ent;
  for(int e = joymap_first[button]; e != 0; e = ent->next) {
    ent = &joymap_ents[e - 1];
    if ((modifiers & ent->modifiers) == ent->modifiers)
      return ent->cmd;
  }
  return 0;
}

static int ParseGamepadButtonName(const char **value) {
  const char *s = *value;
  // Longest substring first
  static const char *const kGamepadKeyNames[] = {
    "Back", "Guide", "Start", "L3", "R3",
    "L1", "R1", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2",
    "Lb", "Rb", "A", "B", "X", "Y"
  };
  static const uint8 kGamepadKeyIds[] = {
    kGamepadBtn_Back, kGamepadBtn_Guide, kGamepadBtn_Start, kGamepadBtn_L3, kGamepadBtn_R3,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_L2, kGamepadBtn_R2,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_A, kGamepadBtn_B, kGamepadBtn_X, kGamepadBtn_Y,
  };
  for (size_t i = 0; i != countof(kGamepadKeyNames); i++) {
    const char *r = StringStartsWithNoCase(s, kGamepadKeyNames[i]);
    if (r) {
      *value = r;
      return kGamepadKeyIds[i];
    }
  }
  return kGamepadBtn_Invalid;
}

static const uint8 kDefaultGamepadCmds[] = {
  kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_Back, kGamepadBtn_Start,
  kGamepadBtn_B, kGamepadBtn_A, kGamepadBtn_Y, kGamepadBtn_X, kGamepadBtn_L1, kGamepadBtn_R1,
};

static void ParseGamepadArray(char *value, int cmd, int size) {
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    uint32 modifiers = 0;
    const char *ss = s;
    for (;;) {
      int button = ParseGamepadButtonName(&ss);
      if (button == kGamepadBtn_Invalid) BAD: {
        fprintf(stderr, "Unknown gamepad button: '%s'\n", s);
        break;
      }
      while (*ss == ' ' || *ss == '\t') ss++;
      if (*ss == '+') {
        ss++;
        modifiers |= 1 << button;
      } else if (*ss == 0) {
        SetPadBinding(cmd, button, modifiers);
        break;
      } else
        goto BAD;
    }
  }
}

static void SetDefaultKeys() {
  for (int k = 0; k < kKeys_Total; k++) {
    g_kbd_bind[k] = 0;
    g_pad_bind[k].button = kGamepadBtn_Invalid;
    g_pad_bind[k].modifiers = 0;
  }
  for (int k = 1; k < kKeys_Total; k++)
    SetKbdBinding(k, kDefaultKbdControls[k]);
  for (int i = 0; i < countof(kDefaultGamepadCmds); i++)
    SetPadBinding(kKeys_Controls + i, kDefaultGamepadCmds[i], 0);
}

static void RebuildKeyMaps() {
  keymap_hash_size = 0;
  memset(keymap_hash_first, 0, sizeof(keymap_hash_first));
  joymap_size = 0;
  memset(joymap_first, 0, sizeof(joymap_first));
  for (int k = 1; k < kKeys_Total; k++) {
    if (g_kbd_bind[k])
      KeyMapHash_Add(g_kbd_bind[k], k);
    if (g_pad_bind[k].button != kGamepadBtn_Invalid)
      GamepadMap_Add(g_pad_bind[k].button, g_pad_bind[k].modifiers, k);
  }
}

// A [KeyMap]/[GamepadMap] line replaces the whole group of keys.
static void ClearKeyGroup(int id, int size, bool gamepad) {
  for (int k = id; k < id + size && k < kKeys_Total; k++) {
    if (gamepad)
      g_pad_bind[k].button = kGamepadBtn_Invalid, g_pad_bind[k].modifiers = 0;
    else
      g_kbd_bind[k] = 0;
  }
}

static int GetIniSection(const char *s) {
  if (StringEqualsNoCase(s, "[KeyMap]"))
    return 0;
  if (StringEqualsNoCase(s, "[Graphics]"))
    return 1;
  if (StringEqualsNoCase(s, "[Sound]"))
    return 2;
  if (StringEqualsNoCase(s, "[General]"))
    return 3;
  if (StringEqualsNoCase(s, "[Features]"))
    return 4;
  if (StringEqualsNoCase(s, "[GamepadMap]"))
    return 5;
  return -1;
}

bool ParseBool(const char *value, bool *result) {
  bool rv = false;
  switch (*value++ | 32) {
  case '0': if (*value == 0) break; return false;
  case 'f': if (StringEqualsNoCase(value, "alse")) break; return false;
  case 'n': if (StringEqualsNoCase(value, "o")) break; return false;
  case 'o':
    rv = (*value | 32) == 'n';
    if (StringEqualsNoCase(value, rv ? "n" : "ff")) break;
    return false;
  case '1': rv = true; if (*value == 0) break; return false;
  case 'y': rv = true; if (StringEqualsNoCase(value, "es")) break; return false;
  case 't': rv = true; if (StringEqualsNoCase(value, "rue")) break; return false;
  default: return false;
  }
  if (result) {
    *result = rv;
    return true;
  }
  return rv;
}

static bool ParseBoolBit(const char *value, uint32 *data, uint32 mask) {
  bool tmp;
  if (!ParseBool(value, &tmp))
    return false;
  *data = *data & ~mask | (tmp ? mask : 0);
  return true;
}

static bool HandleIniConfig(int section, const char *key, char *value) {
  if (section == 0) {
    for (int i = 1; i < countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
        ClearKeyGroup(kKeyNameId[i].id, kKeyNameId[i].size, false);
        ParseKeyArray(value, kKeyNameId[i].id, kKeyNameId[i].size);
        return true;
      }
    }
  } else if (section == 5) {
    for (int i = 1; i < countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
        ClearKeyGroup(kKeyNameId[i].id, kKeyNameId[i].size, true);
        ParseGamepadArray(value, kKeyNameId[i].id, kKeyNameId[i].size);
        return true;
      }
    }
  } else if (section == 1) {
    if (StringEqualsNoCase(key, "WindowSize")) {
      char *s;
      if (StringEqualsNoCase(value, "Auto")){
        g_config.window_width  = 0;
        g_config.window_height = 0;
        return true;
      }
      while ((s = NextDelim(&value, 'x')) != NULL) {
        if(g_config.window_width == 0) {
          g_config.window_width = atoi(s);
        } else {
          g_config.window_height = atoi(s);
          return true;
        }
      }
    } else if (StringEqualsNoCase(key, "EnhancedMode7")) {
      return ParseBool(value, &g_config.enhanced_mode7);
    } else if (StringEqualsNoCase(key, "NewRenderer")) {
      return ParseBool(value, &g_config.new_renderer);
    } else if (StringEqualsNoCase(key, "IgnoreAspectRatio")) {
      return ParseBool(value, &g_config.ignore_aspect_ratio);
    } else if (StringEqualsNoCase(key, "Fullscreen")) {
      g_config.fullscreen = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "WindowScale")) {
      g_config.window_scale = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "OutputMethod")) {
      g_config.output_method = StringEqualsNoCase(value, "SDL-Software") ? kOutputMethod_SDLSoftware :
                               StringEqualsNoCase(value, "OpenGL") ? kOutputMethod_OpenGL : 
                               StringEqualsNoCase(value, "OpenGL ES") ? kOutputMethod_OpenGL_ES :
                                                                        kOutputMethod_SDL;
      return true;
    } else if (StringEqualsNoCase(key, "LinearFiltering")) {
      return ParseBool(value, &g_config.linear_filtering);
    } else if (StringEqualsNoCase(key, "NoSpriteLimits")) {
      return ParseBool(value, &g_config.no_sprite_limits);
    } else if (StringEqualsNoCase(key, "LinkGraphics")) {
      g_config.link_graphics = value;
      return true;
    } else if (StringEqualsNoCase(key, "Shader")) {
      g_config.shader = *value ? value : NULL;
      return true;
    } else if (StringEqualsNoCase(key, "DimFlashes")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_DimFlashes);
    } else if (StringEqualsNoCase(key, "EdgeEffect")) {
      return ParseBool(value, &g_config.edge_effect);
    } else if (StringEqualsNoCase(key, "WideHud")) {
      return ParseBool(value, &g_config.wide_hud);
    }
  } else if (section == 2) {
    if (StringEqualsNoCase(key, "EnableAudio")) {
      return ParseBool(value, &g_config.enable_audio);
    } else if (StringEqualsNoCase(key, "AudioFreq")) {
      g_config.audio_freq = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioChannels")) {
      g_config.audio_channels = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioSamples")) {
      g_config.audio_samples = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "EnableMSU")) {
        if (StringEqualsNoCase(value, "opuz"))
        g_config.enable_msu = kMsuEnabled_Opuz;
      else if (StringEqualsNoCase(value, "deluxe"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe;
      else if (StringEqualsNoCase(value, "deluxe-opuz"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz;
      else 
        return ParseBool(value, (bool*)&g_config.enable_msu);
      return true;
    } else if (StringEqualsNoCase(key, "MSUPath")) {
      g_config.msu_path = value;
      return true;
    } else if (StringEqualsNoCase(key, "MSUVolume")) {
      g_config.msuvolume = IntMin(IntMax(atoi(value), 0), 100);
      return true;
    } else if (StringEqualsNoCase(key, "Volume")) {
      g_config.volume = IntMin(IntMax(atoi(value), 0), 100);
      return true;
    } else if (StringEqualsNoCase(key, "SfxVolume")) {
      g_config.sfx_volume = IntMin(IntMax(atoi(value), 0), 100);
      return true;
    } else if (StringEqualsNoCase(key, "ResumeMSU")) {
      return ParseBool(value, &g_config.resume_msu);
    }
  } else if (section == 3) {
    if (StringEqualsNoCase(key, "Autosave")) {
      g_config.autosave = (bool)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "ExtendedAspectRatio")) {
      const char* s;
      int h = 224;
      bool nospr = false, novis = false;
      // todo: make it not depend on the order
      while ((s = NextDelim(&value, ',')) != NULL) {
        if (strcmp(s, "extend_y") == 0)
          h = 240, g_config.extend_y = true;
        else if (strcmp(s, "16:9") == 0)
          g_config.extended_aspect_ratio = (h * 16 / 9 - 256) / 2;
        else if (strcmp(s, "3:2") == 0)
          g_config.extended_aspect_ratio = (h * 3 / 2 - 256) / 2;
        else if (strcmp(s, "16:10") == 0)
          g_config.extended_aspect_ratio = (h * 16 / 10 - 256) / 2;
        else if (strcmp(s, "18:9") == 0)
          g_config.extended_aspect_ratio = (h * 18 / 9 - 256) / 2;
        else if (strcmp(s, "4:3") == 0)
          g_config.extended_aspect_ratio = 0;
        else if (strcmp(s, "unchanged_sprites") == 0)
          nospr = true;
        else if (strcmp(s, "no_visual_fixes") == 0)
          novis = true;
        else
          return false;
      }
      if (g_config.extended_aspect_ratio && !nospr)
        g_config.features0 |= kFeatures0_ExtendScreen64;
      if (g_config.extended_aspect_ratio && !novis)
        g_config.features0 |= kFeatures0_WidescreenVisualFixes;
      return true;
    } else if (StringEqualsNoCase(key, "DisplayPerfInTitle")) {
      return ParseBool(value, &g_config.display_perf_title);
    } else if (StringEqualsNoCase(key, "AutosaveMinutes")) {
      g_config.autosave_minutes = atoi(value);
      return true;
    } else if (StringEqualsNoCase(key, "AutosaveSlots")) {
      g_config.autosave_slots = atoi(value);
      return true;
    } else if (StringEqualsNoCase(key, "Rumble")) {
      g_config.rumble_intensity = IntMin(IntMax(atoi(value), 0), 100);
      return true;
    } else if (StringEqualsNoCase(key, "StickDeadzone")) {
      g_config.stick_deadzone = IntMin(IntMax(atoi(value), 5), 90);
      return true;
    } else if (StringEqualsNoCase(key, "WindowBorderless")) {
      return ParseBool(value, &g_config.window_borderless);
    } else if (StringEqualsNoCase(key, "NintendoMenuButtons")) {
      return ParseBool(value, &g_config.nintendo_menu_buttons);
    } else if (StringEqualsNoCase(key, "Discord")) {
      return ParseBool(value, &g_config.discord);
    } else if (StringEqualsNoCase(key, "DiscordLocation")) {
      return ParseBool(value, &g_config.discord_location);
    } else if (StringEqualsNoCase(key, "DiscordAppId")) {
      g_config.discord_app_id = value;
      return true;
    } else if (StringEqualsNoCase(key, "AchievementToasts")) {
      return ParseBool(value, &g_config.achievement_toasts);
    } else if (StringEqualsNoCase(key, "DialogSpeed")) {
      g_config.dialog_speed = IntMin(IntMax(atoi(value), 0), 4);
      return true;
    } else if (StringEqualsNoCase(key, "TransitionSpeed")) {
      g_config.transition_speed = IntMin(IntMax(atoi(value), 0), 4);
      return true;
    } else if (StringEqualsNoCase(key, "SaveHoldMs")) {
      g_config.save_hold_ms = IntMin(IntMax(atoi(value), 0), 5000);
      return true;
    } else if (StringEqualsNoCase(key, "TurboSpeed")) {
      g_config.turbo_speed = IntMin(IntMax(atoi(value), 0), 16);
      return true;
    } else if (StringEqualsNoCase(key, "RewindSeconds")) {
      g_config.rewind_seconds = atoi(value);
      return true;
    } else if (StringEqualsNoCase(key, "RewindFrameInterval")) {
      g_config.rewind_frame_interval = atoi(value);
      return true;
    } else if (StringEqualsNoCase(key, "DisableFrameDelay")) {
      return ParseBool(value, &g_config.disable_frame_delay);
    } else if (StringEqualsNoCase(key, "Language")) {
      g_config.language = value;
      return true;
    }
  } else if (section == 4) {
    if (StringEqualsNoCase(key, "ItemSwitchLR")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_SwitchLR);
    } else if (StringEqualsNoCase(key, "ItemSwitchLRLimit")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_SwitchLRLimit);
    } else if (StringEqualsNoCase(key, "TurnWhileDashing")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_TurnWhileDashing);
    } else if (StringEqualsNoCase(key, "MirrorToDarkworld")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_MirrorToDarkworld);
    } else if (StringEqualsNoCase(key, "CollectItemsWithSword")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_CollectItemsWithSword);
    } else if (StringEqualsNoCase(key, "BreakPotsWithSword")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_BreakPotsWithSword);
    } else if (StringEqualsNoCase(key, "DisableLowHealthBeep")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_DisableLowHealthBeep);
    } else if (StringEqualsNoCase(key, "SkipIntroOnKeypress")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_SkipIntroOnKeypress);
    } else if (StringEqualsNoCase(key, "ShowMaxItemsInYellow")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_ShowMaxItemsInYellow);
    } else if (StringEqualsNoCase(key, "MoreActiveBombs")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_MoreActiveBombs);
    } else if (StringEqualsNoCase(key, "CarryMoreRupees")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_CarryMoreRupees);
    } else if (StringEqualsNoCase(key, "MiscBugFixes")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_MiscBugFixes);
    } else if (StringEqualsNoCase(key, "GameChangingBugFixes")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_GameChangingBugFixes);
    } else if (StringEqualsNoCase(key, "CancelBirdTravel")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_CancelBirdTravel);
    } else if (StringEqualsNoCase(key, "FastDialog")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_FastDialog);
    }
  }
  return false;
}

// Settings changed from the in-game options menu. They're written to their own
// file, read after zelda3.ini, so the user's commented zelda3.ini is never rewritten.
static const char *const kSectionNames[] = {
  "KeyMap", "Graphics", "Sound", "General", "Features", "GamepadMap",
};
typedef struct OverrideEnt {
  int section;
  char key[32];
  char *value;
} OverrideEnt;
static OverrideEnt *g_overrides;
static int g_num_overrides;
static char *g_override_path;
static bool g_recording_overrides;

static void RecordOverride(int section, const char *key, const char *value) {
  for (int i = 0; i < g_num_overrides; i++) {
    OverrideEnt *e = &g_overrides[i];
    if (e->section == section && StringEqualsNoCase(e->key, key)) {
      free(e->value);
      e->value = strdup(value);
      return;
    }
  }
  if ((g_num_overrides & 31) == 0) {
    g_overrides = realloc(g_overrides, sizeof(OverrideEnt) * (g_num_overrides + 32));
    if (!g_overrides) Die("realloc failure");
  }
  OverrideEnt *e = &g_overrides[g_num_overrides++];
  e->section = section;
  snprintf(e->key, sizeof(e->key), "%s", key);
  e->value = strdup(value);
}

static bool WriteOverrides() {
  if (!g_override_path)
    return false;
  FILE *f = fopen(g_override_path, "wb");
  if (!f) {
    fprintf(stderr, "Unable to write %s\n", g_override_path);
    return false;
  }
  fprintf(f, "# Generado por el menu de Opciones del juego (Escape / View+Start, pestana Opciones).\n"
             "# Lo que esta aca tiene prioridad sobre zelda3.ini.\n"
             "# Borra una linea para volver a usar el valor de zelda3.ini.\n");
  for (int sec = 0; sec < countof(kSectionNames); sec++) {
    bool any = false;
    for (int i = 0; i < g_num_overrides; i++) {
      OverrideEnt *e = &g_overrides[i];
      if (e->section != sec)
        continue;
      if (!any)
        fprintf(f, "\n[%s]\n", kSectionNames[sec]);
      any = true;
      fprintf(f, "%s = %s\n", e->key, e->value);
    }
  }
  fclose(f);
  return true;
}

bool Config_SetOverride(const char *section, const char *key, const char *value) {
  for (int sec = 0; sec < countof(kSectionNames); sec++) {
    if (StringEqualsNoCase(section, kSectionNames[sec])) {
      RecordOverride(sec, key, value);
      return WriteOverrides();
    }
  }
  return false;
}

static bool ParseOneConfigFile(const char *filename, int depth) {
  char *filedata = (char*)ReadWholeFile(filename, NULL), *p;
  if (!filedata)
    return false;
  
  int section = -2;
  g_config.memory_buffer = filedata;

  for (int lineno = 1; (p = NextLineStripComments(&filedata)) != NULL; lineno++) {
    if (*p == 0)
      continue; // empty line
    if (*p == '[') {
      section = GetIniSection(p);
      if (section < 0)
        fprintf(stderr, "%s:%d: Invalid .ini section %s\n", filename, lineno, p);
    } else if (*p == '!' && SkipPrefix(p + 1, "include ")) {
      char *tt = p + 8;
      char *new_filename = ReplaceFilenameWithNewPath(filename, NextPossiblyQuotedString(&tt));
      if (depth > 10 || !ParseOneConfigFile(new_filename, depth + 1))
        fprintf(stderr, "Warning: Unable to read %s\n", new_filename);
      free(new_filename);
    } else if (section == -2) {
      fprintf(stderr, "%s:%d: Expecting [section]\n", filename, lineno);
    } else {
      char *v = SplitKeyValue(p);
      if (v == NULL) {
        fprintf(stderr, "%s:%d: Expecting 'key=value'\n", filename, lineno);
        continue;
      }
      if (section >= 0 && g_recording_overrides)
        RecordOverride(section, p, v);
      if (section >= 0 && !HandleIniConfig(section, p, v))
        fprintf(stderr, "%s:%d: Can't parse '%s'\n", filename, lineno, p);
    }
  }
  return true;
}

void ParseConfigFile(const char *filename) {
  g_config.msuvolume = 100;  // default msu volume, 100%
  g_config.volume = 100;
  g_config.sfx_volume = 100;
  g_config.rewind_seconds = 30;
  g_config.rewind_frame_interval = 2;
  g_config.autosave_minutes = 5;
  g_config.autosave_slots = 10;
  g_config.edge_effect = true;
  g_config.wide_hud = true;
  g_config.rumble_intensity = 70;
  g_config.stick_deadzone = 30;
  g_config.turbo_speed = 0;
  g_config.save_hold_ms = 1000;
  g_config.transition_speed = 2;
  g_config.dialog_speed = 1;
  g_config.achievement_toasts = true;
  g_config.discord = true;
  g_config.nintendo_menu_buttons = true;
  g_config.discord_location = true;
  g_config.discord_app_id = "1574572493212225667";
  SetDefaultKeys();

  if (filename != NULL || !ParseOneConfigFile("zelda3.user.ini", 0)) {
    if (filename == NULL)
      filename = "zelda3.ini";
    if (!ParseOneConfigFile(filename, 0))
      fprintf(stderr, "Warning: Unable to read config file %s\n", filename);
  }
  // Settings saved by the in-game options menu, next to the config file.
  g_override_path = ReplaceFilenameWithNewPath(filename ? filename : "zelda3.user.ini", "zelda3.opciones.ini");
  g_recording_overrides = true;
  ParseOneConfigFile(g_override_path, 0);
  g_recording_overrides = false;
  RebuildKeyMaps();
}

// ---------------------------------------------------------------------------
// Runtime key binding (used by the in-game controls menu)
// ---------------------------------------------------------------------------

static const char *const kPadIniNames[kGamepadBtn_Count] = {
  "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "Lb", "Rb",
  "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2",
};
static const char *const kPadNiceNames[kGamepadBtn_Count] = {
  "A", "B", "X", "Y", "View", "Guía", "Start", "LS", "RS", "LB", "RB",
  "Cruz arriba", "Cruz abajo", "Cruz izq.", "Cruz der.", "LT", "RT",
};

int Config_EncodeKey(SDL_Keycode code, SDL_Keymod mod) {
  return EncodeSdlKey(code, mod);
}

int Config_GetKeyBinding(int cmd) {
  return (cmd > 0 && cmd < kKeys_Total) ? g_kbd_bind[cmd] : 0;
}

int Config_GetPadBinding(int cmd, uint32 *modifiers) {
  if (cmd <= 0 || cmd >= kKeys_Total || g_pad_bind[cmd].button == kGamepadBtn_Invalid) {
    *modifiers = 0;
    return kGamepadBtn_Invalid;
  }
  *modifiers = g_pad_bind[cmd].modifiers;
  return g_pad_bind[cmd].button;
}

static void KeyToString(int key, char *buf, size_t n) {
  buf[0] = 0;
  if (!key)
    return;
  SDL_Keycode code = (key & 0x1ff) | (key & kKeyMod_ScanCode ? SDLK_SCANCODE_MASK : 0);
  snprintf(buf, n, "%s%s%s%s", key & kKeyMod_Ctrl ? "Ctrl+" : "", key & kKeyMod_Alt ? "Alt+" : "",
           key & kKeyMod_Shift ? "Shift+" : "", SDL_GetKeyName(code));
}

static void PadToString(int button, uint32 modifiers, const char *const *names, char *buf, size_t n) {
  buf[0] = 0;
  if (button == kGamepadBtn_Invalid)
    return;
  size_t pos = 0;
  for (int b = 0; b < kGamepadBtn_Count; b++) {
    if (modifiers & (1u << b))
      pos += snprintf(buf + pos, pos < n ? n - pos : 0, "%s+", names[b]);
  }
  snprintf(buf + pos, pos < n ? n - pos : 0, "%s", names[button]);
}

void Config_KeyName(int key, char *buf, size_t n) {
  KeyToString(key, buf, n);
}

void Config_PadName(int button, uint32 modifiers, char *buf, size_t n) {
  PadToString(button, modifiers, kPadNiceNames, buf, n);
}

const char *Config_CommandIniName(int cmd) {
  for (int i = 1; i < countof(kKeyNameId); i++)
    if (cmd >= kKeyNameId[i].id && cmd < kKeyNameId[i].id + kKeyNameId[i].size)
      return kKeyNameId[i].name;
  return NULL;
}

// Writes the whole [KeyMap] or [GamepadMap] line of the group |cmd| is part of.
static void SaveKeyGroup(int cmd, bool gamepad) {
  for (int i = 1; i < countof(kKeyNameId); i++) {
    int id = kKeyNameId[i].id, size = kKeyNameId[i].size;
    if (cmd < id || cmd >= id + size)
      continue;
    char value[1024], one[96];
    size_t pos = 0, keep = 0;
    value[0] = 0;
    for (int k = id; k < id + size; k++) {
      if (gamepad)
        PadToString(g_pad_bind[k].button, g_pad_bind[k].modifiers, kPadIniNames, one, sizeof(one));
      else
        KeyToString(g_kbd_bind[k], one, sizeof(one));
      pos += snprintf(value + pos, sizeof(value) - pos, "%s%s", k != id ? ", " : "", one);
      if (pos >= sizeof(value))
        return;
      if (one[0])
        keep = pos;
    }
    value[keep] = 0;  // drop trailing empty entries
    Config_SetOverride(gamepad ? "GamepadMap" : "KeyMap", kKeyNameId[i].name, value);
    return;
  }
}

int Config_SetKeyBinding(int cmd, int key) {
  if (cmd <= 0 || cmd >= kKeys_Total)
    return 0;
  int stolen = SetKbdBinding(cmd, key);
  RebuildKeyMaps();
  SaveKeyGroup(cmd, false);
  if (stolen)
    SaveKeyGroup(stolen, false);
  return stolen;
}

int Config_SetPadBinding(int cmd, int button, uint32 modifiers) {
  if (cmd <= 0 || cmd >= kKeys_Total)
    return 0;
  int stolen = SetPadBinding(cmd, button, modifiers);
  RebuildKeyMaps();
  SaveKeyGroup(cmd, true);
  if (stolen)
    SaveKeyGroup(stolen, true);
  return stolen;
}
