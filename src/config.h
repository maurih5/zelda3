#pragma once
#include "types.h"
#include <SDL_keycode.h>
#include <stddef.h>

enum {
  kKeys_Null,
  kKeys_Controls,
  kKeys_Controls_Last = kKeys_Controls + 11,
  kKeys_Load,
  kKeys_Load_Last = kKeys_Load + 19,
  kKeys_Save,
  kKeys_Save_Last = kKeys_Save + 19,
  kKeys_Replay,
  kKeys_Replay_Last = kKeys_Replay + 19,
  kKeys_LoadRef,
  kKeys_LoadRef_Last = kKeys_LoadRef + 19,
  kKeys_ReplayRef,
  kKeys_ReplayRef_Last = kKeys_ReplayRef + 19,
  kKeys_CheatLife,
  kKeys_CheatKeys,
  kKeys_CheatEquipment,
  kKeys_CheatWalkThroughWalls,
  kKeys_ClearKeyLog,
  kKeys_StopReplay,
  kKeys_Fullscreen,
  kKeys_Reset,
  kKeys_Pause,
  kKeys_PauseDimmed,
  kKeys_Turbo,
  kKeys_ReplayTurbo,
  kKeys_WindowBigger,
  kKeys_WindowSmaller,
  kKeys_DisplayPerf,
  kKeys_ToggleRenderer,
  kKeys_VolumeUp,
  kKeys_VolumeDown,
  kKeys_Rewind,
  kKeys_SaveMenu,
  kKeys_Quit,
  kKeys_Total,
};

enum {
  kOutputMethod_SDL,
  kOutputMethod_SDLSoftware,
  kOutputMethod_OpenGL,
  kOutputMethod_OpenGL_ES,
};

typedef struct Config {
  int window_width;
  int window_height;
  bool enhanced_mode7;
  bool new_renderer;
  bool ignore_aspect_ratio;
  uint8 fullscreen;
  uint8 window_scale;
  bool enable_audio;
  bool linear_filtering;
  uint8 output_method;
  uint16 audio_freq;
  uint8 audio_channels;
  uint16 audio_samples;
  bool autosave;
  uint8 extended_aspect_ratio;
  bool extend_y;
  bool no_sprite_limits;
  bool display_perf_title;
  uint8 enable_msu;
  bool resume_msu;
  bool disable_frame_delay;
  uint8 msuvolume;
  uint8 volume;      // master volume 0-100
  uint8 sfx_volume;  // SPC output (sound effects, and music when not using MSU) 0-100
  uint32 features0;
  int rewind_seconds;
  int rewind_frame_interval;
  int autosave_minutes;
  int autosave_slots;
  bool edge_effect;        // fill widescreen side bars with a blurred mirror
  bool wide_hud;           // widescreen: HUD at the sides of the picture
  int rumble_intensity;    // 0-100, 0 = off
  int stick_deadzone;      // percent of the stick range
  int save_hold_ms;        // hold the save state key this long to save, 0 = instant
  int turbo_speed;         // game frames per shown frame while Turbo is held, 0 = as fast as possible

  const char *link_graphics;
  char *memory_buffer;
  const char *shader;
  const char *msu_path;
  const char *discord_app_id;
  bool discord;            // show what you're playing in Discord
  bool discord_location;   // ... including where you are
  const char *language;
} Config;

enum {
  kMsuEnabled_Msu = 1,
  kMsuEnabled_MsuDeluxe = 2,
  kMsuEnabled_Opuz = 4,
};
enum {
  kGamepadBtn_Invalid = -1,
  kGamepadBtn_A,
  kGamepadBtn_B,
  kGamepadBtn_X,
  kGamepadBtn_Y,
  kGamepadBtn_Back,
  kGamepadBtn_Guide,
  kGamepadBtn_Start,
  kGamepadBtn_L3,
  kGamepadBtn_R3,
  kGamepadBtn_L1,
  kGamepadBtn_R1,
  kGamepadBtn_DpadUp,
  kGamepadBtn_DpadDown,
  kGamepadBtn_DpadLeft,
  kGamepadBtn_DpadRight,
  kGamepadBtn_L2,
  kGamepadBtn_R2,
  kGamepadBtn_Count,
};

extern Config g_config;

void ParseConfigFile(const char *filename);
int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod);
int FindCmdForGamepadButton(int button, uint32 modifiers);

// Runtime configuration from the in-game options menu. Changes are saved to
// zelda3.opciones.ini (read after zelda3.ini).
bool Config_SetOverride(const char *section, const char *key, const char *value);
int Config_EncodeKey(SDL_Keycode code, SDL_Keymod mod);
int Config_GetKeyBinding(int cmd);
int Config_GetPadBinding(int cmd, uint32 *modifiers);  // returns button or kGamepadBtn_Invalid
// Both return the command that lost the key/button because of the new binding (or 0).
int Config_SetKeyBinding(int cmd, int key);
int Config_SetPadBinding(int cmd, int button, uint32 modifiers);
void Config_KeyName(int key, char *buf, size_t n);
void Config_PadName(int button, uint32 modifiers, char *buf, size_t n);
const char *Config_CommandIniName(int cmd);
