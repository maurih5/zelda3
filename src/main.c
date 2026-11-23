#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>
#ifdef _WIN32
#include "platform/win32/volume_control.h"
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "snes/ppu.h"

#include "types.h"
#include "variables.h"

#include "zelda_rtl.h"
#include "zelda_cpu_infra.h"

#include "config.h"
#include "assets.h"
#include "load_gfx.h"
#include "util.h"
#include "audio.h"
#include "savemenu.h"
#include "optmenu.h"
#include "haptics.h"
#include "edgefx.h"

static bool g_run_without_emu = 0;

#if defined(SAVEMENU_SELFTEST) || defined(REWIND_SELFTEST)
void SaveMenu_SelftestSetInterval(int frames);
const char *SaveMenu_SelftestSelectedPath();
int SaveMenu_SelftestNumEntries();
static int g_st_frame, g_st_dump_frame, g_st_toast_dump_at = -1, g_st_failures;
static char g_st_dump_name[64] = "selftest_toast.bmp";
static struct { char path[64]; uint32 hash; } g_st_saves[128];
static int g_st_nsaves;

static uint32 SelftestHashGame() {
  uint32 h = 2166136261u;
  for (int i = 0; i < 0x20000; i++) {
    if (i == 0x654 || (i >= 0x1DB20 && i < 0x1DBA0))
      continue;  // msu state, depends on audio thread timing
    h = (h ^ g_ram[i]) * 16777619u;
  }
  return h;
}

static uint32 SelftestHashSram() {
  uint32 h = 2166136261u;
  for (int i = 0; i < 0x1ffe; i++)
    h = (h ^ g_zenv.sram[i]) * 16777619u;
  return h;
}

void SelftestRecordSave(const char *path) {
  int i = 0;
  while (i < g_st_nsaves && strcmp(g_st_saves[i].path, path) != 0)
    i++;
  if (i == g_st_nsaves) {
    if (g_st_nsaves == 128) return;
    g_st_nsaves++;
  }
  snprintf(g_st_saves[i].path, sizeof(g_st_saves[i].path), "%s", path);
  g_st_saves[i].hash = SelftestHashGame();
  if (g_st_toast_dump_at < 0)
    g_st_toast_dump_at = g_st_frame + 40;
}

static void SelftestWriteBmp(const char *name, const uint8 *px, int pitch, int w, int h) {
  FILE *f = fopen(name, "wb");
  if (!f) return;
  uint32 img = w * h * 4;
  uint8 hdr[54] = { 'B', 'M' };
  *(uint32 *)(hdr + 2) = 54 + img;
  *(uint32 *)(hdr + 10) = 54;
  *(uint32 *)(hdr + 14) = 40;
  *(int32 *)(hdr + 18) = w;
  *(int32 *)(hdr + 22) = -h;  // top-down
  *(uint16 *)(hdr + 26) = 1;
  *(uint16 *)(hdr + 28) = 32;
  *(uint32 *)(hdr + 34) = img;
  fwrite(hdr, 1, 54, f);
  for (int y = 0; y < h; y++)
    fwrite(px + (size_t)y * pitch, 4, w, f);
  fclose(f);
}

static int g_snes_width, g_snes_height;  // tentative definition, defined below
static void SelftestDumpMenu(const char *name) {
  static uint8 buf[512 * 256 * 4];
  SaveMenu_Draw(buf, g_snes_width * 4, g_snes_width, g_snes_height);
  SelftestWriteBmp(name, buf, g_snes_width * 4, g_snes_width, g_snes_height);
}

static void SelftestCheck(bool ok, const char *what) {
  printf("  [%s] %s\n", ok ? "OK" : "FALLA", what);
  if (!ok) g_st_failures++;
}
#endif

#ifdef REWIND_SELFTEST
uint32 ZeldaSelftestRamHash();
bool ZeldaSelftestIsReplaying();
uint32 ZeldaSelftestTotalFrames();
extern int g_rewind_selftest_mismatches, g_rewind_selftest_checks;
static uint8 *g_st_ram_copy;
static double g_st_cap_time, g_st_step_time, g_st_cap_max, g_st_step_max;
static int g_st_cap_n, g_st_step_n;
// Hash of the image drawn for each game frame, to check that rewinding shows the same picture.
static uint32 g_st_img_hash[65536], g_st_img_frame[65536];
static int g_st_img_checks, g_st_img_mismatches;
static bool g_st_rewinding_now;
static double StNow() { return (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency(); }
#endif

// Forwards
static bool LoadRom(const char *filename);
static void LoadLinkGraphics();
static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big);
static void HandleInput(int keyCode, int modCode, bool pressed);
static void HandleCommand(uint32 j, bool pressed);
static void HandleSaveMenuKey(int key, int mod);
static bool HandleSaveMenuKeyRepeats(int key, int mod);
static void HandleSaveMenuGamepad(int button);
static int RemapSdlButton(int button);
static void HandleGamepadInput(int button, bool pressed);
static void HandleGamepadAxisInput(int gamepad_id, int axis, int value);
static void OpenOneGamepad(int i);
static void HandleVolumeAdjustment(int volume_adjustment);
static void LoadAssets();
static void SwitchDirectory();

enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
};

static const char kWindowTitle[] = "The Legend of Zelda: A Link to the Past";
static uint32 g_win_flags = SDL_WINDOW_RESIZABLE;
static SDL_Window *g_window;

static uint8 g_paused, g_turbo, g_rewind_held, g_rewinding, g_replay_turbo = true, g_cursor = true;
static uint8 g_current_window_scale;
static uint8 g_gamepad_buttons;
static int g_input1_state;
static bool g_display_perf;
static int g_curr_fps;
static int g_ppu_render_flags = 0;
static int g_snes_width, g_snes_height;
static int g_sdl_audio_mixer_volume = SDL_MIX_MAXVOLUME;
static struct RendererFuncs g_renderer_funcs;
static uint32 g_gamepad_modifiers;
static uint16 g_gamepad_last_cmd[kGamepadBtn_Count];
static uint32 g_menu_last_stick;

void NORETURN Die(const char *error) {
#if defined(NDEBUG) && defined(_WIN32)
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, error, NULL);
#endif
  fprintf(stderr, "Error: %s\n", error);
  exit(1);
}

void ChangeWindowScale(int scale_step) {
  if ((SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0)
    return;
  int screen = SDL_GetWindowDisplayIndex(g_window);
  if (screen < 0) screen = 0;
  int max_scale = kMaxWindowScale;
  SDL_Rect bounds;
  int bt = -1, bl, bb, br;
  // note this takes into effect Windows display scaling, i.e., resolution is divided by scale factor
  if (SDL_GetDisplayUsableBounds(screen, &bounds) == 0) {
    // this call may take a while before it is reported by Windows (or not at all in my testing)
    if (SDL_GetWindowBordersSize(g_window, &bt, &bl, &bb, &br) != 0) {
      // guess based on Windows 10/11 defaults
      bl = br = bb = 1;
      bt = 31;
    }
    // Allow a scale level slightly above the max that fits on screen
    int mw = (bounds.w - bl - br + g_snes_width / 4) / g_snes_width;
    int mh = (bounds.h - bt - bb + g_snes_height / 4) / g_snes_height;
    max_scale = IntMin(mw, mh);
  }
  int new_scale = IntMax(IntMin(g_current_window_scale + scale_step, max_scale), 1);
  g_current_window_scale = new_scale;
  int w = new_scale * g_snes_width;
  int h = new_scale * g_snes_height;

  //SDL_RenderSetLogicalSize(g_renderer, w, h);
  SDL_SetWindowSize(g_window, w, h);
  if (bt >= 0) {
    // Center the window on top of the mouse
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = IntMax(IntMin(mx - w / 2, bounds.x + bounds.w - bl - br - w), bounds.x + bl);
    int wy = IntMax(IntMin(my - h / 2, bounds.y + bounds.h - bt - bb - h), bounds.y + bt);
    SDL_SetWindowPosition(g_window, wx, wy);
  } else {
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

#define RESIZE_BORDER 20
static SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *pt, void *data) {
  uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 || (flags & SDL_WINDOW_FULLSCREEN) != 0)
    return SDL_HITTEST_NORMAL;

  if ((SDL_GetModState() & KMOD_CTRL) != 0)
    return SDL_HITTEST_DRAGGABLE;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  if (pt->y < RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPRIGHT : SDL_HITTEST_RESIZE_TOP;
  } else if (pt->y >= h - RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMRIGHT : SDL_HITTEST_RESIZE_BOTTOM;
  } else {
    if (pt->x < RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_LEFT;
    } else if (pt->x >= w - RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_RIGHT;
    }
  }
  return SDL_HITTEST_NORMAL;
}

static void DrawPpuFrameWithPerf() {
  int render_scale = PpuGetCurrentRenderScale(g_zenv.ppu, g_ppu_render_flags);
  uint8 *pixel_buffer = 0;
  int pitch = 0;

  g_renderer_funcs.BeginDraw(g_snes_width * render_scale,
                             g_snes_height * render_scale,
                             &pixel_buffer, &pitch);
  if (g_display_perf || g_config.display_perf_title) {
    static float history[64], average;
    static int history_pos;
    uint64 before = SDL_GetPerformanceCounter();
    ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
    uint64 after = SDL_GetPerformanceCounter();
    float v = (double)SDL_GetPerformanceFrequency() / (after - before);
    average += v - history[history_pos];
    history[history_pos] = v;
    history_pos = (history_pos + 1) & 63;
    g_curr_fps = average * (1.0f / 64);
  } else {
    ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
  }
#ifdef REWIND_SELFTEST
  {
    uint32 hh = 2166136261u;
    int w = g_snes_width * render_scale, hgt = g_snes_height * render_scale;
    for (int y = 0; y < hgt; y++) {
      const uint32 *row = (const uint32 *)(pixel_buffer + (size_t)y * pitch);
      for (int x = 0; x < w; x++)
        hh = (hh ^ (row[x] & 0xffffff)) * 16777619u;
    }
    uint32 fr = ZeldaSelftestTotalFrames(), slot = fr & 0xffff;
    if (!g_st_rewinding_now) {
      g_st_img_hash[slot] = hh;
      g_st_img_frame[slot] = fr + 1;
    } else if (g_st_img_frame[slot] == fr + 1) {
      g_st_img_checks++;
      if (g_st_img_hash[slot] != hh) {
        if (g_st_img_mismatches++ < 3) {
          char name[64];
          snprintf(name, sizeof(name), "rewind_mismatch_%d.bmp", g_st_img_mismatches);
          SelftestWriteBmp(name, pixel_buffer, pitch, w, hgt);
        }
      }
    }
  }
#endif
  if (g_config.edge_effect && render_scale == 1 && g_zenv.ppu->extraLeftRight && !g_zenv.ppu->forcedBlank) {
    Ppu *ppu = g_zenv.ppu;
    int lb = ppu->extraLeftRight - ppu->extraLeftCur, rb = ppu->extraLeftRight - ppu->extraRightCur;
    EdgeEffect_Apply(pixel_buffer, pitch, g_snes_width, g_snes_height, lb, rb);
    // During an iris/spotlight, the filled bars stay black outside the circle.
    for (int y = 0; y < g_snes_height && y < 256; y++) {
      if (!g_iris_drawn[y].valid)
        continue;
      uint32 *row = (uint32 *)(pixel_buffer + (size_t)y * pitch);
      int l = g_iris_drawn[y].left + ppu->extraLeftRight, r = g_iris_drawn[y].right + ppu->extraLeftRight;
      for (int x = 0; x < lb; x++)
        if (x < l || x > r) row[x] = 0;
      for (int x = g_snes_width - rb; x < g_snes_width; x++)
        if (x < l || x > r) row[x] = 0;
    }
  }
  // The widescreen HUD goes on top, after the side bars are filled.
  if (render_scale == 1)
    PpuRedrawHudOverlay(g_zenv.ppu, pixel_buffer, pitch);
  SaveMenu_CaptureFrame(pixel_buffer, pitch, render_scale, g_snes_width, g_snes_height);
  SaveMenu_DrawOverlay(pixel_buffer, pitch, render_scale, g_snes_width, g_snes_height);
  if (g_display_perf)
    RenderNumber(pixel_buffer + pitch * render_scale, pitch, g_curr_fps, render_scale == 4);
#ifdef SAVEMENU_SELFTEST
  if (g_st_dump_frame) {
    g_st_dump_frame = 0;
    SelftestWriteBmp(g_st_dump_name, pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
  }
#endif
  g_renderer_funcs.EndDraw();
}

static SDL_mutex *g_audio_mutex;
static uint8 *g_audiobuffer, *g_audiobuffer_cur, *g_audiobuffer_end;
static int g_frames_per_block;
static uint8 g_audio_channels;

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      ZeldaRenderAudio((int16*)g_audiobuffer, g_frames_per_block, g_audio_channels);
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16);
    }
    int n = IntMin(len, g_audiobuffer_end - g_audiobuffer_cur);
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_audiobuffer_cur, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_audiobuffer_cur, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }

  ZeldaDiscardUnusedAudioFrames();
  SDL_UnlockMutex(g_audio_mutex);
}

// State for sdl renderer
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Rect g_sdl_renderer_rect;

static bool SdlRenderer_Init(SDL_Window *window) {

  if (g_config.shader)
    fprintf(stderr, "Warning: Shaders are supported only with the OpenGL backend\n");

  SDL_Renderer *renderer = SDL_CreateRenderer(g_window, -1,
                                              g_config.output_method == kOutputMethod_SDLSoftware ? SDL_RENDERER_SOFTWARE :
                                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return false;
  }
  SDL_RendererInfo renderer_info;
  SDL_GetRendererInfo(renderer, &renderer_info);
  if (kDebugFlag) {
    printf("Supported texture formats:");
    for (int i = 0; i < renderer_info.num_texture_formats; i++)
      printf(" %s", SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
    printf("\n");
  }
  g_renderer = renderer;
  if (!g_config.ignore_aspect_ratio)
    SDL_RenderSetLogicalSize(renderer, g_snes_width, g_snes_height);
  if (g_config.linear_filtering)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

  int tex_mult = (g_ppu_render_flags & kPpuRenderFlags_4x4Mode7) ? 4 : 1;
  g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                g_snes_width * tex_mult, g_snes_height * tex_mult);
  if (g_texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return false;
  }
  return true;
}

static void SdlRenderer_Destroy() {
  SDL_DestroyTexture(g_texture);
  SDL_DestroyRenderer(g_renderer);
}

static void SdlRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  g_sdl_renderer_rect.w = width;
  g_sdl_renderer_rect.h = height;
  if (SDL_LockTexture(g_texture, &g_sdl_renderer_rect, (void **)pixels, pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    return;
  }
}

static void SdlRenderer_EndDraw() {

//  uint64 before = SDL_GetPerformanceCounter();
  SDL_UnlockTexture(g_texture);
//  uint64 after = SDL_GetPerformanceCounter();
//  float v = (double)(after - before) / SDL_GetPerformanceFrequency();
//  printf("%f ms\n", v * 1000);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, &g_sdl_renderer_rect, NULL);
  SDL_RenderPresent(g_renderer); // vsyncs to 60 FPS?
}

static const struct RendererFuncs kSdlRendererFuncs  = {
  &SdlRenderer_Init,
  &SdlRenderer_Destroy,
  &SdlRenderer_BeginDraw,
  &SdlRenderer_EndDraw,
};

void OpenGLRenderer_Create(struct RendererFuncs *funcs, bool use_opengl_es);

#undef main
int main(int argc, char** argv) {
  argc--, argv++;
  const char *config_file = NULL;
  if (argc >= 2 && strcmp(argv[0], "--config") == 0) {
    config_file = argv[1];
    argc -= 2, argv += 2;
  } else {
    SwitchDirectory();
  }
  ParseConfigFile(config_file);
  LoadAssets();
  LoadLinkGraphics();

  ZeldaInitialize();
  g_zenv.ppu->extraLeftRight = UintMin(g_config.extended_aspect_ratio, kPpuExtraLeftRight);
  g_snes_width = (g_config.extended_aspect_ratio * 2 + 256);
  g_snes_height = (g_config.extend_y ? 240 : 224);


  // Delay actually setting those features in ram until any snapshots finish playing.
  g_wanted_zelda_features = g_config.features0;

  g_ppu_render_flags = g_config.new_renderer * kPpuRenderFlags_NewRenderer |
                       g_config.enhanced_mode7 * kPpuRenderFlags_4x4Mode7 |
                       g_config.extend_y * kPpuRenderFlags_Height240 |
                       g_config.no_sprite_limits * kPpuRenderFlags_NoSpriteLimits;
  ZeldaEnableMsu(g_config.enable_msu);
  ZeldaSetLanguage(g_config.language);
  ZeldaRewindConfigure(g_config.rewind_seconds, g_config.rewind_frame_interval);
  SaveMenu_Init(g_config.autosave_minutes, g_config.autosave_slots);
  Haptics_Configure(g_config.rumble_intensity);
  g_wide_hud = g_config.wide_hud;

  if (g_config.fullscreen == 1)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else if (g_config.fullscreen == 2)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN;

  // Window scale (1=100%, 2=200%, 3=300%, etc.)
  g_current_window_scale = (g_config.window_scale == 0) ? 2 : IntMin(g_config.window_scale, kMaxWindowScale);

  // audio_freq: Use common sampling rates (see user config file. values higher than 48000 are not supported.)
  if (g_config.audio_freq < 11025 || g_config.audio_freq > 48000)
    g_config.audio_freq = kDefaultFreq;

  // Currently, the SPC/DSP implementation only supports up to stereo.
  if (g_config.audio_channels < 1 || g_config.audio_channels > 2)
    g_config.audio_channels = kDefaultChannels;

  // audio_samples: power of 2
  if (g_config.audio_samples <= 0 || ((g_config.audio_samples & (g_config.audio_samples - 1)) != 0))
    g_config.audio_samples = kDefaultSamples;

  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }

  bool custom_size  = g_config.window_width != 0 && g_config.window_height != 0;
  int window_width  = custom_size ? g_config.window_width  : g_current_window_scale * g_snes_width;
  int window_height = custom_size ? g_config.window_height : g_current_window_scale * g_snes_height;

  if (g_config.output_method == kOutputMethod_OpenGL ||
      g_config.output_method == kOutputMethod_OpenGL_ES) {
    g_win_flags |= SDL_WINDOW_OPENGL;
    OpenGLRenderer_Create(&g_renderer_funcs, (g_config.output_method == kOutputMethod_OpenGL_ES));
  } else {
    g_renderer_funcs = kSdlRendererFuncs;
  }

  SDL_Window* window = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, g_win_flags);
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }
  g_window = window;
  SDL_SetWindowHitTest(window, HitTestCallback, NULL);

  if (!g_renderer_funcs.Initialize(window))
    return 1;

  SDL_AudioDeviceID device = 0;
  SDL_AudioSpec want = { 0 }, have;
  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");
  OptMenu_ApplyStartupSettings();

  if (g_config.enable_audio) {
    want.freq = g_config.audio_freq;
    want.format = AUDIO_S16;
    want.channels = g_config.audio_channels;
    want.samples = g_config.audio_samples;
    want.callback = &AudioCallback;
    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (device == 0) {
      printf("Failed to open audio device: %s\n", SDL_GetError());
      return 1;
    }
    g_audio_channels = have.channels;
    g_frames_per_block = (534 * have.freq) / 32000;
    g_audiobuffer = malloc(g_frames_per_block * have.channels * sizeof(int16));
  }

  if (argc >= 1 && !g_run_without_emu)
    LoadRom(argv[0]);

#if defined(_WIN32)
  _mkdir("saves");
#else
  mkdir("saves", 0755);
#endif

  ZeldaReadSram();

  for (int i = 0; i < SDL_NumJoysticks(); i++)
    OpenOneGamepad(i);

  bool running = true;
  SDL_Event event;
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
  uint32 frameCtr = 0;
  bool audiopaused = true;

  if (g_config.autosave)
    HandleCommand(kKeys_Load + 0, true);

  while(running) {
    while(SDL_PollEvent(&event)) {
      switch(event.type) {
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_CONTROLLERAXISMOTION:
        HandleGamepadAxisInput(event.caxis.which, event.caxis.axis, event.caxis.value);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int b = RemapSdlButton(event.cbutton.button);
        if (b >= 0)
          HandleGamepadInput(b, event.type == SDL_CONTROLLERBUTTONDOWN);
        break;
      }
      case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_CTRL && event.wheel.y != 0)
          ChangeWindowScale(event.wheel.y > 0 ? 1 : -1);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && (g_win_flags & SDL_WINDOW_FULLSCREEN) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0);
          }
        }
        break;
      case SDL_KEYDOWN:
        if (SaveMenu_IsOpen()) {
          if (OptMenu_IsCapturing()) {
            if (!event.key.repeat) {
              SDL_LockMutex(g_audio_mutex);
              OptMenu_CaptureKey(event.key.keysym.sym, event.key.keysym.mod, true);
              SDL_UnlockMutex(g_audio_mutex);
            }
          } else if (!event.key.repeat || HandleSaveMenuKeyRepeats(event.key.keysym.sym, event.key.keysym.mod)) {
            HandleSaveMenuKey(event.key.keysym.sym, event.key.keysym.mod);
          }
          break;
        }
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        break;
      case SDL_KEYUP:
        if (SaveMenu_IsOpen() && OptMenu_IsCapturing()) {
          SDL_LockMutex(g_audio_mutex);
          OptMenu_CaptureKey(event.key.keysym.sym, event.key.keysym.mod, false);
          SDL_UnlockMutex(g_audio_mutex);
        }
        HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_QUIT:
        running = false;
        break;
      }
    }

    // Audio is muted while rewinding.
    bool want_audio_paused = g_paused || g_rewinding || SaveMenu_IsOpen();
    if (want_audio_paused != audiopaused) {
      audiopaused = want_audio_paused;
      if (device)
        SDL_PauseAudioDevice(device, audiopaused);
    }

    if (SaveMenu_IsOpen()) {
      // Analog stick navigation (edge triggered)
      uint32 pad = g_gamepad_buttons;
      uint32 pressed = pad & ~g_menu_last_stick;
      g_menu_last_stick = pad;
      SDL_LockMutex(g_audio_mutex);
      if (!OptMenu_IsCapturing()) {
        if (pressed & (1 << 4)) SaveMenu_Input(kSaveMenu_Up);
        if (pressed & (1 << 5)) SaveMenu_Input(kSaveMenu_Down);
        if (pressed & (1 << 6)) SaveMenu_Input(kSaveMenu_Left);
        if (pressed & (1 << 7)) SaveMenu_Input(kSaveMenu_Right);
      }
      SDL_UnlockMutex(g_audio_mutex);
      if (SaveMenu_IsOpen()) {
        uint8 *px = NULL;
        int pitch = 0;
        g_renderer_funcs.BeginDraw(g_snes_width, g_snes_height, &px, &pitch);
        if (px)
          SaveMenu_Draw(px, pitch, g_snes_width, g_snes_height);
        g_renderer_funcs.EndDraw();
      }
      SDL_Delay(16);
      lastTick = SDL_GetTicks();
      continue;
    }

    if (g_paused) {
      SDL_Delay(16);
      continue;
    }

    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    int inputs = g_input1_state;
    if (g_input1_state & 0xf0)
      g_gamepad_buttons = 0;
    inputs |= g_gamepad_buttons;

#ifdef REWIND_SELFTEST
    {
      static uint32 rng = 12345, cur_in, saved_hash, saved_frames;
      static int f = 0, exit_code = 1;
      static bool norewind, replay_started;
      if (f == 0) {
        norewind = getenv("ZELDA3_SELFTEST_NOREWIND") != NULL;
        g_config.disable_frame_delay = true;
        printf("SELFTEST start (%s)\n", norewind ? "no rewind" : "with rewind");
        int kb = FindCmdForSdlKey(SDLK_BACKSPACE, 0);
        int gp = FindCmdForGamepadButton(kGamepadBtn_L2, 1 << kGamepadBtn_Back | 1 << kGamepadBtn_L2);
        int gp_alone = FindCmdForGamepadButton(kGamepadBtn_L2, 1 << kGamepadBtn_L2);
        int turbo = FindCmdForGamepadButton(kGamepadBtn_R2, 1 << kGamepadBtn_Back | 1 << kGamepadBtn_R2);
        printf("Mapping: Backspace->%d Back+L2->%d L2 solo->%d Back+R2->%d (Rewind=%d Turbo=%d)\n",
               kb, gp, gp_alone, turbo, kKeys_Rewind, kKeys_Turbo);
        if (kb != kKeys_Rewind || gp != kKeys_Rewind || gp_alone != 0 || turbo != kKeys_Turbo) {
          printf("SELFTEST FAIL: mapping\n");
          exit(3);
        }
        HandleCommand(kKeys_Load + 0, true);
        HandleCommand(kKeys_ClearKeyLog, true);
      }
      if (f % 16 == 0) {
        static const int kDirs[8] = { 1 << 4, 1 << 5, 1 << 6, 1 << 7, 1 << 4 | 1 << 7, 1 << 5 | 1 << 6, 0, 1 << 6 };
        rng = rng * 1103515245 + 12345;
        cur_in = kDirs[(rng >> 16) & 7] | (((rng >> 20) & 3) == 0 ? 1 : 0);
      }
      inputs = cur_in;
      bool rw_phase = (f >= 1200 && f < 1500) || (f >= 2100 && f < 4500) || (f >= 4700 && f < 4800);
      if (f == 4600) {
        HandleCommand(kKeys_Load + 0, true);
        HandleCommand(kKeys_ClearKeyLog, true);
      }
      g_rewind_held = rw_phase && !norewind;
      if (f == 1199 || f == 1499 || f == 2099 || f == 4499 || f == 4699 || f == 4799) {
        int n; size_t b;
        ZeldaRewindGetStats(&n, &b);
        printf("f=%d: rewind buffer %d states, %.2f MB, game frames=%d\n", f, n, b / 1048576.0, (int)ZeldaSelftestTotalFrames());
      }
      if (f == 5000) {
        saved_hash = ZeldaSelftestRamHash();
        saved_frames = ZeldaSelftestTotalFrames();
        g_st_ram_copy = malloc(0x20000);
        memcpy(g_st_ram_copy, g_ram, 0x20000);
        HandleCommand(kKeys_Save + 9, true);
        HandleCommand(kKeys_Replay + 9, true);
        replay_started = true;
        printf("Saved slot 9 (total_frames=%d), replaying it...\n", (int)saved_frames);
      } else if (replay_started && !ZeldaSelftestIsReplaying()) {
        uint32 h = ZeldaSelftestRamHash();
        int game_diffs = 0;
        for (int i = 0; i < 0x20000; i++) {
          if (g_ram[i] != g_st_ram_copy[i]) {
            bool msu = (i >= 0x1DB20 && i < 0x1DBA0) || i == 0x654;
            if (!msu) game_diffs++;
            printf("  ram diff at 0x%05X: saved %02X replay %02X%s\n", i, g_st_ram_copy[i], g_ram[i], msu ? " (msu state)" : "");
          }
        }
        printf("Non-MSU RAM differences: %d\n", game_diffs);
        if (game_diffs == 0) h = saved_hash;
        printf("Replay finished: total_frames=%d ram hash %s\n", (int)ZeldaSelftestTotalFrames(),
               h == saved_hash ? "MATCHES" : "DIFFERS");
        printf("Rewind checks: %d, delta mismatches: %d\n", g_rewind_selftest_checks, g_rewind_selftest_mismatches);
        printf("Rewind image checks: %d, images different from the original frame: %d\n", g_st_img_checks, g_st_img_mismatches);
        if (g_st_cap_n) printf("Capture: avg %.3f ms, max %.3f ms (%d)\n", g_st_cap_time / g_st_cap_n * 1000, g_st_cap_max * 1000, g_st_cap_n);
        if (g_st_step_n) printf("Step: avg %.3f ms, max %.3f ms (%d)\n", g_st_step_time / g_st_step_n * 1000, g_st_step_max * 1000, g_st_step_n);
        exit_code = (h == saved_hash && g_rewind_selftest_mismatches == 0 && g_st_img_checks > 0 && g_st_img_mismatches == 0) ? 0 : 2;
        printf("SELFTEST %s\n", exit_code == 0 ? "PASS" : "FAIL");
        fflush(stdout);
        exit(exit_code);
      }
      f++;
    }
#endif

#ifdef SAVEMENU_SELFTEST
    {
      static uint32 rng = 777, cur_in;
      int f = g_st_frame;
      if (getenv("ZELDA3_SELFTEST_SCRIPT")) {
        // Scripted run to look at specific moments. ';' separated:
        //   load            load saves/save0.sav at the start (otherwise the game boots)
        //   press:BTN@N-M   hold BTN (A B X Y L R Start Select Up Down Left Right) from frame N to M
        //   kill@N          set Link's health to 0
        //   dump:NAME@N     save the drawn frame N to NAME.bmp
        //   end@N           quit
        static const char *const kBtn[12] = { "B", "Y", "Select", "Start", "Up", "Down", "Left", "Right", "A", "X", "L", "R" };
        char spec[1024];
        snprintf(spec, sizeof(spec), "%s", getenv("ZELDA3_SELFTEST_SCRIPT"));
        if (f == 0)
          g_config.disable_frame_delay = true;
        inputs = 0;
        for (char *tok = strtok(spec, ";"); tok; tok = strtok(NULL, ";")) {
          int a = -1, b = -1;
          char name[64];
          if (f == 0 && strcmp(tok, "load") == 0) {
            HandleCommand(kKeys_Load + 0, true);
            HandleCommand(kKeys_ClearKeyLog, true);
          } else if (sscanf(tok, "press:%63[^@]@%d-%d", name, &a, &b) == 3) {
            for (int i = 0; i < 12; i++)
              if (strcmp(name, kBtn[i]) == 0 && f >= a && f <= b)
                inputs |= 1 << i;
          } else if (sscanf(tok, "kill@%d", &a) == 1 && f == a) {
            link_give_damage = 0xa0;  // more than any health
          } else if (sscanf(tok, "dump:%40[^@]@%d", name, &a) == 2 && f == a) {
            snprintf(g_st_dump_name, sizeof(g_st_dump_name), "%s.bmp", name);
            g_st_dump_frame = 1;
          } else if (sscanf(tok, "end@%d", &a) == 1 && f >= a) {
            printf("SCRIPT end at %d (module %d)\n", f, main_module_index);
            fflush(stdout);
            exit(0);
          }
        }
        g_st_frame++;
        goto SELFTEST_SCRIPT_DONE;
      }
      if (f == 0 && getenv("ZELDA3_SELFTEST_SEED")) rng = atoi(getenv("ZELDA3_SELFTEST_SEED"));
      if (f == 0) {
        g_config.disable_frame_delay = true;
        printf("SAVEMENU SELFTEST start\n");
        int kb = FindCmdForSdlKey(SDLK_ESCAPE, 0);
        int gp = FindCmdForGamepadButton(kGamepadBtn_Start, 1 << kGamepadBtn_Back | 1 << kGamepadBtn_Start);
        int start = FindCmdForGamepadButton(kGamepadBtn_Start, 1 << kGamepadBtn_Start);
        int save = FindCmdForGamepadButton(kGamepadBtn_R1, 1 << kGamepadBtn_Back | 1 << kGamepadBtn_R1);
        printf("Mapeo: Escape->%d View+Start->%d Start solo->%d View+RB->%d (SaveMenu=%d Start=%d Save=%d)\n",
               kb, gp, start, save, kKeys_SaveMenu, kKeys_Controls + 5, kKeys_Save);
        SelftestCheck(kb == kKeys_SaveMenu && gp == kKeys_SaveMenu && start == kKeys_Controls + 5 && save == kKeys_Save,
                      "mapeo de Escape / View+Start / Start / View+RB");
        // #331: stick fully in a corner must still give a direction.
        HandleGamepadAxisInput(0, SDL_CONTROLLER_AXIS_LEFTX, -32768);
        HandleGamepadAxisInput(0, SDL_CONTROLLER_AXIS_LEFTY, -32768);
        printf("Stick arriba-izquierda a fondo -> botones %02x\n", g_gamepad_buttons);
        SelftestCheck(g_gamepad_buttons == (1 << 4 | 1 << 6), "stick a fondo en diagonal (sin overflow)");
        HandleGamepadAxisInput(0, SDL_CONTROLLER_AXIS_LEFTX, 0);
        HandleGamepadAxisInput(0, SDL_CONTROLLER_AXIS_LEFTY, 0);
        SelftestCheck(g_gamepad_buttons == 0, "stick centrado");
        HandleCommand(kKeys_Load + 0, true);
        HandleCommand(kKeys_ClearKeyLog, true);
        SaveMenu_SelftestSetInterval(300);
      }
      if (f % 16 == 0) {
        static const int kDirs[8] = { 1 << 4, 1 << 5, 1 << 6, 1 << 7, 1 << 4 | 1 << 7, 1 << 5 | 1 << 6, 0, 1 << 6 };
        rng = rng * 1103515245 + 12345;
        cur_in = kDirs[(rng >> 16) & 7] | (((rng >> 20) & 3) == 0 ? 1 : 0);
      }
      inputs = cur_in;
      if (getenv("ZELDA3_SELFTEST_STILL")) inputs = 0;
      if (getenv("ZELDA3_SELFTEST_DIALOG")) {
        // Opens a message and counts frames until it waits for the player.
        // Modes: off (normal), hold (A held), b (tapping B).
        extern void Sprite_ShowMessageUnconditional(uint16 msg);
        extern bool Text_IsWaitingForPlayer();
        static int start = -1, done_at = -1;
        const char *m = getenv("ZELDA3_SELFTEST_DIALOG");
        bool fast = strcmp(m, "off") != 0;
        inputs = 0;
        if (f == 150)
          g_wanted_zelda_features = fast ? (g_wanted_zelda_features | kFeatures0_FastDialog) : (g_wanted_zelda_features & ~kFeatures0_FastDialog);
        if (f == 200) {
          printf("DIALOG modulo antes: %d\n", main_module_index);
          Sprite_ShowMessageUnconditional(getenv("ZELDA3_SELFTEST_MSG") ? atoi(getenv("ZELDA3_SELFTEST_MSG")) : 0x18c);
          start = f;
        }
        if (start >= 0 && done_at < 0) {
          if (!strcmp(m, "hold")) inputs = 1 << 8;          // A
          if (!strcmp(m, "b")) inputs = (f & 1) ? 1 : 0;    // tap B
          if (Text_IsWaitingForPlayer()) {
            printf("DIALOG %s: %d cuadros hasta esperar al jugador\n", m, f - start);
            done_at = f;
            g_st_dump_frame = 1;
          } else if (f - start > 3000) {
            printf("DIALOG %s: TIMEOUT\n", m);
            exit(3);
          }
        }
        if (done_at >= 0 && f > done_at + 2)
          exit(0);
      }
      if (f == g_st_toast_dump_at)
        g_st_dump_frame = 1;
      if (f == 2400) {
        printf("Autosaves hechos: %d\n", g_st_nsaves);
        SelftestCheck(g_st_nsaves >= 3, "se hicieron autoguardados periodicos");
        HandleCommand(kKeys_SaveMenu, true);
        SelftestCheck(SaveMenu_IsOpen(), "el menu abre");
        printf("Entradas en el menu: %d\n", SaveMenu_SelftestNumEntries());
        SelftestDumpMenu("selftest_menu1.bmp");
        SDL_LockMutex(g_audio_mutex);
        SaveMenu_Input(kSaveMenu_Down);
        SaveMenu_Input(kSaveMenu_Down);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestDumpMenu("selftest_menu2.bmp");
        char path[64];
        snprintf(path, sizeof(path), "%s", SaveMenu_SelftestSelectedPath());
        uint32 expect = 0;
        bool known = false;
        for (int i = 0; i < g_st_nsaves; i++)
          if (strcmp(g_st_saves[i].path, path) == 0) expect = g_st_saves[i].hash, known = true;
        printf("Cargando %s\n", path);
        uint32 sram_before = SelftestHashSram();
        int nsaves_before = g_st_nsaves;
        SDL_LockMutex(g_audio_mutex);
        SaveMenu_Input(kSaveMenu_Ok);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestCheck(!SaveMenu_IsOpen(), "el menu se cierra al cargar");
        SelftestCheck(known && SelftestHashGame() == expect, "el estado cargado es identico al autoguardado");
        SelftestCheck(SelftestHashSram() == sram_before, "la SRAM (partida del juego) no cambio");
        SelftestCheck(g_st_nsaves >= nsaves_before, "se hizo el respaldo 'Antes de cargar'");
      }
      if (f == 2401) {
        HandleCommand(kKeys_SaveMenu, true);
        SelftestDumpMenu("selftest_menu3.bmp");
        SDL_LockMutex(g_audio_mutex);
        SaveMenu_Input(kSaveMenu_Back);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestCheck(!SaveMenu_IsOpen(), "B / Volver cierra el menu");
      }
      if (f == 2700) {
        SDL_LockMutex(g_audio_mutex);
        HandleCommand(kKeys_Save + 2, true);
        SDL_UnlockMutex(g_audio_mutex);
        HandleCommand(kKeys_SaveMenu, true);
        SelftestDumpMenu("selftest_menu4.bmp");
        HandleCommand(kKeys_SaveMenu, true);  // already open: does nothing
        SDL_LockMutex(g_audio_mutex);
        for (int i = 0; i < 30; i++) SaveMenu_Input(kSaveMenu_Down);
        for (int i = 0; i < 7; i++) SaveMenu_Input(kSaveMenu_PageDown);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestDumpMenu("selftest_menu5.bmp");
        SDL_LockMutex(g_audio_mutex);
        SaveMenu_Input(kSaveMenu_Back);
        SDL_UnlockMutex(g_audio_mutex);
      }
      if (f == 0 && getenv("ZELDA3_SELFTEST_OPTCHECK")) {
        // Second run: everything changed from the menu must come back from zelda3.opciones.ini
        g_st_failures = 0;  // the first-run mapping check above is expected to differ now
        printf("OPTCHECK: volume=%d features0=%x\n", g_config.volume, g_config.features0);
        SelftestCheck(g_config.volume == 80, "volumen guardado (80)");
        SelftestCheck((g_config.features0 & kFeatures0_TurnWhileDashing) != 0, "opcion de juego guardada");
        SelftestCheck(FindCmdForSdlKey(SDLK_UP, 0) == kKeys_Controls + 0, "Arriba = flecha arriba guardado");
        SelftestCheck(FindCmdForSdlKey(SDLK_w, 0) == 0, "W quedo libre");
        SelftestCheck(FindCmdForSdlKey(SDLK_s, 0) == kKeys_Controls + 1, "el resto de los controles sigue igual");
        SelftestCheck(FindCmdForGamepadButton(kGamepadBtn_Y, 1 << kGamepadBtn_Back | 1 << kGamepadBtn_Y) == kKeys_SaveMenu, "View+Y = menu guardado");
        SelftestCheck(FindCmdForGamepadButton(kGamepadBtn_Start, 1 << kGamepadBtn_Back | 1 << kGamepadBtn_Start) != kKeys_SaveMenu, "View+Start ya no es el menu");
        SelftestCheck(FindCmdForGamepadButton(kGamepadBtn_R1, 1 << kGamepadBtn_Back | 1 << kGamepadBtn_R1) == kKeys_Save, "View+RB sigue guardando");
        SelftestCheck(FindCmdForSdlKey(SDLK_F1, KMOD_SHIFT) == kKeys_Save, "Shift+F1 sigue guardando");
        SelftestCheck(FindCmdForSdlKey(SDLK_F5, KMOD_SHIFT) == kKeys_Save + 4, "Shift+F5 sigue guardando en la ranura 5");
        printf("OPTCHECK %s (%d fallas)\n", g_st_failures ? "FAIL" : "PASS", g_st_failures);
        exit(g_st_failures ? 2 : 0);
      }
      if (getenv("ZELDA3_SELFTEST_GL") && f >= 60) {
        // Switch through every shader listed in the file, 3 frames each.
        static FILE *list;
        static int n_ok, n_fail, n;
        if (f == 60) {
          list = fopen(getenv("ZELDA3_SELFTEST_GL"), "rb");
          printf("GLTEST start, gl shader supported=%d\n", Main_ShaderSupported());
        }
        if ((f - 60) % 3 == 0) {
          char line[512];
          if (!list || !fgets(line, sizeof(line), list)) {
            bool back = Main_SetShader(g_config.shader);
            printf("GLTEST done: %d ok, %d failed of %d; restore original=%d\n", n_ok, n_fail, n, back);
            fflush(stdout);
            exit(0);
          }
          line[strcspn(line, "\r\n")] = 0;
          printf("GLTEST %s ... ", line);
          fflush(stdout);
          uint64 t0 = SDL_GetPerformanceCounter();
          bool ok = Main_SetShader(line);
          double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000 / SDL_GetPerformanceFrequency();
          printf("%s %.0f ms\n", ok ? "ok" : "FAILED", ms);
          fflush(stdout);
          n++, ok ? n_ok++ : n_fail++;
        }
      }
      if (f == 2750) {
        HandleCommand(kKeys_SaveMenu, true);
        SDL_LockMutex(g_audio_mutex);
        SaveMenu_Input(kSaveMenu_NextTab);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestCheck(SaveMenu_GetTab() == kMenuTab_Options, "RB pasa a Opciones");
        SelftestDumpMenu("selftest_opt1.bmp");
        int vol0 = g_config.volume;
        SDL_LockMutex(g_audio_mutex);
        for (int i = 0; i < 4; i++) SaveMenu_Input(kSaveMenu_Left);
        SDL_UnlockMutex(g_audio_mutex);
        printf("Volumen %d -> %d\n", vol0, g_config.volume);
        SelftestCheck(g_config.volume == vol0 - 20, "izquierda baja el volumen");
        SDL_LockMutex(g_audio_mutex);
        for (int i = 0; i < 21; i++) SaveMenu_Input(kSaveMenu_Down);
        uint32 feat0 = g_config.features0;
        SaveMenu_Input(kSaveMenu_Ok);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestCheck((g_config.features0 ^ feat0) == kFeatures0_TurnWhileDashing &&
                      (g_wanted_zelda_features & kFeatures0_TurnWhileDashing), "A activa 'Girar al correr'");
        SelftestDumpMenu("selftest_opt2.bmp");
        SDL_LockMutex(g_audio_mutex);
        for (int i = 0; i < 18; i++) SaveMenu_Input(kSaveMenu_Up);
        SaveMenu_Input(kSaveMenu_Ok);  // Shader (needs OpenGL)
        SDL_UnlockMutex(g_audio_mutex);
        SelftestDumpMenu("selftest_opt3.bmp");
        SDL_LockMutex(g_audio_mutex);
        SaveMenu_Input(kSaveMenu_NextTab);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestCheck(SaveMenu_GetTab() == kMenuTab_Controls, "RB pasa a Controles");
        SelftestDumpMenu("selftest_ctl1.bmp");
        SDL_LockMutex(g_audio_mutex);
        SaveMenu_Input(kSaveMenu_Ok);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestCheck(OptMenu_IsCapturing(), "A espera una tecla");
        SelftestDumpMenu("selftest_ctl2.bmp");
        OptMenu_CaptureKey(SDLK_RETURN, 0, false);  // release of the key that started it: ignored
        SelftestCheck(OptMenu_IsCapturing(), "soltar Enter no asigna nada");
        OptMenu_CaptureKey(SDLK_UP, 0, true);
        SelftestCheck(!OptMenu_IsCapturing() && FindCmdForSdlKey(SDLK_UP, 0) == kKeys_Controls + 0 &&
                      FindCmdForSdlKey(SDLK_w, 0) == 0, "Arriba reasignada a la flecha");
        SDL_LockMutex(g_audio_mutex);
        SaveMenu_Input(kSaveMenu_Right);
        for (int i = 0; i < 12; i++) SaveMenu_Input(kSaveMenu_Down);
        SaveMenu_Input(kSaveMenu_Ok);
        SDL_UnlockMutex(g_audio_mutex);
        OptMenu_CaptureButton(kGamepadBtn_A, false);  // release of A: ignored
        OptMenu_CaptureButton(kGamepadBtn_Back, true);
        OptMenu_CaptureButton(kGamepadBtn_Y, true);
        SelftestDumpMenu("selftest_ctl3.bmp");
        OptMenu_CaptureButton(kGamepadBtn_Y, false);
        OptMenu_CaptureButton(kGamepadBtn_Back, false);
        SelftestCheck(!OptMenu_IsCapturing() &&
                      FindCmdForGamepadButton(kGamepadBtn_Y, 1 << kGamepadBtn_Back | 1 << kGamepadBtn_Y) == kKeys_SaveMenu,
                      "menu reasignado a View+Y");
        SelftestDumpMenu("selftest_ctl4.bmp");
        SDL_LockMutex(g_audio_mutex);
        for (int i = 0; i < 40; i++) SaveMenu_Input(kSaveMenu_Down);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestDumpMenu("selftest_ctl5.bmp");
        SDL_LockMutex(g_audio_mutex);
        SaveMenu_Input(kSaveMenu_Back);
        SDL_UnlockMutex(g_audio_mutex);
        SelftestCheck(!SaveMenu_IsOpen(), "B cierra el menu desde Controles");
        FILE *of = fopen("zelda3.opciones.ini", "rb");
        SelftestCheck(of != NULL, "se escribio zelda3.opciones.ini");
        if (of) {
          char line[512];
          while (fgets(line, sizeof(line), of)) printf("  | %s", line);
          fclose(of);
        }
      }
      if (f == 2800) {
        printf("Al salir: modulo 0x%02X, submodulo %d\n", main_module_index, submodule_index);
        printf("SAVEMENU SELFTEST %s (%d fallas)\n", g_st_failures ? "FAIL" : "PASS", g_st_failures);
        fflush(stdout);
        HandleCommand(kKeys_Quit, true);  // must end the main loop through SDL_QUIT
      }
      g_st_frame++;
    }
SELFTEST_SCRIPT_DONE:;
#endif

    bool is_replay = false;
    bool rewound = false;
    SDL_LockMutex(g_audio_mutex);
#ifdef REWIND_SELFTEST
    double st_t0 = StNow();
#endif
    if (g_rewind_held)
      rewound = ZeldaRewindStep();
#ifdef REWIND_SELFTEST
    if (rewound) {
      double d = StNow() - st_t0;
      g_st_step_time += d, g_st_step_n++;
      if (d > g_st_step_max) g_st_step_max = d;
    }
#endif
    if (!rewound) {
      if (g_rewinding) {
        ZeldaRewindFinish();
        g_rewinding = false;
      }
      is_replay = ZeldaRunFrame(inputs);
#ifdef REWIND_SELFTEST
      double st_t1 = StNow();
#endif
      ZeldaRewindCapture();
      SaveMenu_OnFramePlayed();
#ifdef REWIND_SELFTEST
      double d = StNow() - st_t1;
      if (d > 0.00005) {  // only count real captures
        g_st_cap_time += d, g_st_cap_n++;
        if (d > g_st_cap_max) g_st_cap_max = d;
      }
#endif
    } else {
      g_rewinding = true;
    }
#ifdef REWIND_SELFTEST
    g_st_rewinding_now = rewound;
#endif
    SDL_UnlockMutex(g_audio_mutex);

    frameCtr++;

    if (!rewound && (g_turbo ^ (is_replay & g_replay_turbo))) {
      // TurboSpeed = N shows one of every N frames at the normal pace (N times faster);
      // 0 runs as fast as possible.
      uint32 every = (g_turbo && g_config.turbo_speed >= 2) ? g_config.turbo_speed : g_turbo ? 16 : 128;
      if (frameCtr % every != 0)
        continue;
    }

    DrawPpuFrameWithPerf();

    if (g_config.display_perf_title) {
      char title[60];
      snprintf(title, sizeof(title), "%s | FPS: %d", kWindowTitle, g_curr_fps);
      SDL_SetWindowTitle(g_window, title);
    }

    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    if (!g_config.disable_frame_delay) {
      static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
      lastTick += delays[frameCtr % 3];

      if (lastTick > curTick) {
        uint32 delta = lastTick - curTick;
        if (delta > 500) {
          lastTick = curTick - 500;
          delta = 500;
        }
//        printf("Sleeping %d\n", delta);
        SDL_Delay(delta);
      } else if (curTick - lastTick > 500) {
        lastTick = curTick;
      }
    }
  }
  SDL_LockMutex(g_audio_mutex);
  SaveMenu_OnQuit();
  SDL_UnlockMutex(g_audio_mutex);
  if (g_config.autosave)
    HandleCommand(kKeys_Save + 0, true);

  // clean sdl
  if (g_config.enable_audio) {
    SDL_PauseAudioDevice(device, 1);
    SDL_CloseAudioDevice(device);
  }

  SDL_DestroyMutex(g_audio_mutex);
  free(g_audiobuffer);

  g_renderer_funcs.Destroy();

  SDL_DestroyWindow(window);
  SDL_Quit();
  //SaveConfigFile();
  return 0;
}

static void RenderDigit(uint8 *dst, size_t pitch, int digit, uint32 color, bool big) {
  static const uint8 kFont[] = {
    0x1c, 0x36, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1c,
    0x18, 0x1c, 0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e,
    0x3e, 0x63, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x63, 0x7f,
    0x3e, 0x63, 0x60, 0x60, 0x3c, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x30, 0x38, 0x3c, 0x36, 0x33, 0x7f, 0x30, 0x30, 0x30, 0x78,
    0x7f, 0x03, 0x03, 0x03, 0x3f, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x1c, 0x06, 0x03, 0x03, 0x3f, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x7f, 0x63, 0x60, 0x60, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c,
    0x3e, 0x63, 0x63, 0x63, 0x3e, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x3e, 0x63, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x60, 0x30, 0x1e,
  };
  const uint8 *p = kFont + digit * 10;
  if (!big) {
    for (int y = 0; y < 10; y++, dst += pitch) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1)
          ((uint32 *)dst)[x] = color;
      }
    }
  } else {
    for (int y = 0; y < 10; y++, dst += pitch * 2) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1) {
          ((uint32 *)dst)[x * 2 + 1] = ((uint32 *)dst)[x * 2] = color;
          ((uint32 *)(dst+pitch))[x * 2 + 1] = ((uint32 *)(dst + pitch))[x * 2] = color;
        }
      }
    }
  }
}

static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big) {
  char buf[32], *s;
  int i;
  sprintf(buf, "%d", n);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + ((pitch + i + 4) << big), pitch, *s - '0', 0x404040, big);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + (i << big), pitch, *s - '0', 0xffffff, big);
}

static void HandleCommand_Locked(uint32 j, bool pressed);

static void HandleCommand(uint32 j, bool pressed) {
  if (j <= kKeys_Controls_Last) {
    static const uint8 kKbdRemap[] = { 0, 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
    if (pressed)
      g_input1_state |= 1 << kKbdRemap[j];
    else
      g_input1_state &= ~(1 << kKbdRemap[j]);
    return;
  }

  if (j == kKeys_Turbo) {
    g_turbo = pressed;
    return;
  }

  if (j == kKeys_Rewind) {
    g_rewind_held = pressed;
    return;
  }

  if (j == kKeys_SaveMenu) {
    if (pressed && !SaveMenu_IsOpen()) {
      SaveMenu_Toggle();
      // Release game buttons so nothing stays held while the menu is open.
      g_input1_state = 0;
      g_rewind_held = 0;
      g_turbo = 0;
      g_menu_last_stick = g_gamepad_buttons;
    }
    return;
  }

  // Everything that might access audio state
  // (like SaveLoad and Reset) must have the lock.
  SDL_LockMutex(g_audio_mutex);
  HandleCommand_Locked(j, pressed);
  SDL_UnlockMutex(g_audio_mutex);
}

void ZeldaApuLock() {
  SDL_LockMutex(g_audio_mutex);
}

void ZeldaApuUnlock() {
  SDL_UnlockMutex(g_audio_mutex);
}


static void HandleCommand_Locked(uint32 j, bool pressed) {
  if (!pressed)
    return;
  if (j <= kKeys_Load_Last) {
    SaveLoadSlot(kSaveLoad_Load, j - kKeys_Load);
  } else if (j <= kKeys_Save_Last) {
    SaveLoadSlot(kSaveLoad_Save, j - kKeys_Save);
    SaveMenu_OnManualSave(j - kKeys_Save);
  } else if (j <= kKeys_Replay_Last) {
    SaveLoadSlot(kSaveLoad_Replay, j - kKeys_Replay);
  } else if (j <= kKeys_LoadRef_Last) {
    SaveLoadSlot(kSaveLoad_Load, 256 + j - kKeys_LoadRef);
  } else if (j <= kKeys_ReplayRef_Last) {
    SaveLoadSlot(kSaveLoad_Replay, 256 + j - kKeys_ReplayRef);
  } else {
    switch (j) {
    case kKeys_CheatLife: PatchCommand('w'); break;
    case kKeys_CheatEquipment: PatchCommand('W'); break;
    case kKeys_CheatKeys: PatchCommand('o'); break;
    case kKeys_CheatWalkThroughWalls: PatchCommand('E'); break;
    case kKeys_ClearKeyLog: PatchCommand('k'); break;
    case kKeys_StopReplay: PatchCommand('l'); break;
    case kKeys_Fullscreen:
      Main_SetFullscreen(!Main_IsFullscreen());
      break;
    case kKeys_Reset:
      ZeldaReset(true);
      break;
    case kKeys_Pause: g_paused = !g_paused; break;
    case kKeys_PauseDimmed:
      g_paused = !g_paused;
      // SDL_RenderPresent may not be called more than once per frame.
      // Seems to work on Windows still. Temporary measure until it's fixed.
#ifdef _WIN32
      if (g_paused) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 159);
        SDL_RenderFillRect(g_renderer, NULL);
        SDL_RenderPresent(g_renderer);
      }
#endif
      break;
    case kKeys_ReplayTurbo: g_replay_turbo = !g_replay_turbo; break;
    case kKeys_WindowBigger: ChangeWindowScale(1); break;
    case kKeys_WindowSmaller: ChangeWindowScale(-1); break;
    case kKeys_DisplayPerf: g_display_perf ^= 1; break;
    case kKeys_ToggleRenderer: g_ppu_render_flags ^= kPpuRenderFlags_NewRenderer; break;
    case kKeys_VolumeUp:
    case kKeys_VolumeDown: HandleVolumeAdjustment(j == kKeys_VolumeUp ? 1 : -1); break;
    case kKeys_Quit: {
      // Same as closing the window (autosaves on the way out).
      SDL_Event ev = { .type = SDL_QUIT };
      SDL_PushEvent(&ev);
      break;
    }
    default: assert(0);
    }
  }
}

// ---------------------------------------------------------------------------
// Used by the options menu
// ---------------------------------------------------------------------------

bool Main_IsFullscreen() {
  return (g_win_flags & SDL_WINDOW_FULLSCREEN) != 0;
}

void Main_SetFullscreen(bool on) {
  if (on == Main_IsFullscreen())
    return;
  if (on)
    g_win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else
    g_win_flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
  g_cursor = !on;
  SDL_ShowCursor(g_cursor);
}

int Main_GetRenderFlags() {
  return g_ppu_render_flags;
}

void Main_SetRenderFlags(int flags) {
  g_ppu_render_flags = flags;
}

bool Main_ShaderSupported() {
  return g_config.output_method == kOutputMethod_OpenGL || g_config.output_method == kOutputMethod_OpenGL_ES;
}

bool OpenGLRenderer_SetShader(const char *path);

bool Main_SetShader(const char *path) {
  return Main_ShaderSupported() && OpenGLRenderer_SetShader(path);
}

static void SaveMenuAction(int action) {
  SDL_LockMutex(g_audio_mutex);
  SaveMenu_Input(action);
  SDL_UnlockMutex(g_audio_mutex);
}

static int SaveMenuActionForKey(int key, int mod) {
  int cmd = FindCmdForSdlKey(key, mod);
  if (key == SDLK_TAB)
    return (mod & KMOD_SHIFT) ? kSaveMenu_PrevTab : kSaveMenu_NextTab;
  if (key == SDLK_UP || cmd == kKeys_Controls + 0)
    return kSaveMenu_Up;
  if (key == SDLK_DOWN || cmd == kKeys_Controls + 1)
    return kSaveMenu_Down;
  if (key == SDLK_LEFT || cmd == kKeys_Controls + 2)
    return kSaveMenu_Left;
  if (key == SDLK_RIGHT || cmd == kKeys_Controls + 3)
    return kSaveMenu_Right;
  if (key == SDLK_PAGEUP)
    return kSaveMenu_PageUp;
  if (key == SDLK_PAGEDOWN)
    return kSaveMenu_PageDown;
  if (cmd == kKeys_Controls + 10)
    return kSaveMenu_PrevTab;
  if (cmd == kKeys_Controls + 11)
    return kSaveMenu_NextTab;
  if (key == SDLK_RETURN || key == SDLK_KP_ENTER || cmd == kKeys_Controls + 5 || cmd == kKeys_Controls + 6)
    return kSaveMenu_Ok;
  if (key == SDLK_DELETE || cmd == kKeys_Controls + 8)
    return kSaveMenu_Clear;
  if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE || cmd == kKeys_Controls + 7 || cmd == kKeys_SaveMenu)
    return kSaveMenu_Back;
  return -1;
}

static bool HandleSaveMenuKeyRepeats(int key, int mod) {
  int a = SaveMenuActionForKey(key, mod);
  return a == kSaveMenu_Up || a == kSaveMenu_Down || a == kSaveMenu_Left || a == kSaveMenu_Right ||
         a == kSaveMenu_PageUp || a == kSaveMenu_PageDown;
}

static void HandleSaveMenuKey(int key, int mod) {
  int a = SaveMenuActionForKey(key, mod);
  if (a >= 0)
    SaveMenuAction(a);
}

// Uses the Xbox button convention: A accepts, B goes back.
static void HandleSaveMenuGamepad(int button) {
  // The combo that opens the menu also closes it.
  if (button != kGamepadBtn_A && FindCmdForGamepadButton(button, g_gamepad_modifiers) == kKeys_SaveMenu) {
    SaveMenuAction(kSaveMenu_Back);
    return;
  }
  switch (button) {
  case kGamepadBtn_DpadUp: SaveMenuAction(kSaveMenu_Up); break;
  case kGamepadBtn_DpadDown: SaveMenuAction(kSaveMenu_Down); break;
  case kGamepadBtn_DpadLeft: SaveMenuAction(kSaveMenu_Left); break;
  case kGamepadBtn_DpadRight: SaveMenuAction(kSaveMenu_Right); break;
  case kGamepadBtn_L1: SaveMenuAction(kSaveMenu_PrevTab); break;
  case kGamepadBtn_R1: SaveMenuAction(kSaveMenu_NextTab); break;
  case kGamepadBtn_A: SaveMenuAction(kSaveMenu_Ok); break;
  case kGamepadBtn_B: SaveMenuAction(kSaveMenu_Back); break;
  case kGamepadBtn_X: SaveMenuAction(kSaveMenu_Clear); break;
  case kGamepadBtn_Start:
    // The same combo that opens the menu (View+Start) closes it.
    SaveMenuAction((g_gamepad_modifiers & (1 << kGamepadBtn_Back)) ? kSaveMenu_Back : kSaveMenu_Ok);
    break;
  }
}

static void HandleInput(int keyCode, int keyMod, bool pressed) {
  int j = FindCmdForSdlKey(keyCode, keyMod);
  if (j != 0)
    HandleCommand(j, pressed);
}

static void OpenOneGamepad(int i) {
  if (SDL_IsGameController(i)) {
    SDL_GameController *controller = SDL_GameControllerOpen(i);
    if (!controller)
      fprintf(stderr, "Could not open gamepad %d: %s\n", i, SDL_GetError());
  }
}

static int RemapSdlButton(int button) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: return kGamepadBtn_A;
  case SDL_CONTROLLER_BUTTON_B: return kGamepadBtn_B;
  case SDL_CONTROLLER_BUTTON_X: return kGamepadBtn_X;
  case SDL_CONTROLLER_BUTTON_Y: return kGamepadBtn_Y;
  case SDL_CONTROLLER_BUTTON_BACK: return kGamepadBtn_Back;
  case SDL_CONTROLLER_BUTTON_GUIDE: return kGamepadBtn_Guide;
  case SDL_CONTROLLER_BUTTON_START: return kGamepadBtn_Start;
  case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kGamepadBtn_L3;
  case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kGamepadBtn_R3;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kGamepadBtn_L1;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kGamepadBtn_R1;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return kGamepadBtn_DpadUp;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kGamepadBtn_DpadDown;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kGamepadBtn_DpadLeft;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kGamepadBtn_DpadRight;
  default: return -1;
  }
}

static void HandleGamepadInput(int button, bool pressed) {
  if (!!(g_gamepad_modifiers & (1 << button)) == pressed)
    return;
  g_gamepad_modifiers ^= 1 << button;
  if (SaveMenu_IsOpen()) {
    if (OptMenu_IsCapturing()) {
      SDL_LockMutex(g_audio_mutex);
      OptMenu_CaptureButton(button, pressed);
      SDL_UnlockMutex(g_audio_mutex);
      if (pressed) {
        g_gamepad_last_cmd[button] = 0;
        return;
      }
    } else if (pressed) {
      g_gamepad_last_cmd[button] = 0;
      HandleSaveMenuGamepad(button);
      return;
    }
  }
  if (pressed)
    g_gamepad_last_cmd[button] = FindCmdForGamepadButton(button, g_gamepad_modifiers);
  if (g_gamepad_last_cmd[button] != 0)
    HandleCommand(g_gamepad_last_cmd[button], pressed);
}

// Changes the game's own master volume (same as in the options menu).
static void HandleVolumeAdjustment(int volume_adjustment) {
  int v = OptMenu_ChangeVolume(volume_adjustment);
  char msg[48];
  snprintf(msg, sizeof(msg), "Volumen: %d%%", v);
  SaveMenu_ShowToast(msg);
}

// Approximates atan2(y, x) normalized to the [0,4) range
// with a maximum error of 0.1620 degrees
// normalized_atan(x) ~ (b x + x^2) / (1 + 2 b x + x^2)
static float ApproximateAtan2(float y, float x) {
  uint32 sign_mask = 0x80000000;
  float b = 0.596227f;
  // Extract the sign bits
  uint32 ux_s = sign_mask & *(uint32 *)&x;
  uint32 uy_s = sign_mask & *(uint32 *)&y;
  // Determine the quadrant offset
  float q = (float)((~ux_s & uy_s) >> 29 | ux_s >> 30);
  // Calculate the arctangent in the first quadrant
  float bxy_a = b * x * y;
  if (bxy_a < 0.0f) bxy_a = -bxy_a;  // avoid fabs
  float num = bxy_a + y * y;
  float atan_1q = num / (x * x + bxy_a + num + 0.000001f);
  // Translate it to the proper quadrant
  uint32_t uatan_2q = (ux_s ^ uy_s) | *(uint32 *)&atan_1q;
  return q + *(float *)&uatan_2q;
}

static void HandleGamepadAxisInput(int gamepad_id, int axis, int value) {
  static int last_gamepad_id, last_x, last_y;
  if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
    // ignore other gamepads unless they have a big input
    if (last_gamepad_id != gamepad_id) {
      if (value > -16000 && value < 16000)
        return;
      last_gamepad_id = gamepad_id;
      last_x = last_y = 0;
    }
    *(axis == SDL_CONTROLLER_AXIS_LEFTX ? &last_x : &last_y) = value;
    int buttons = 0;
    // 64-bit: with the stick fully in a corner x*x + y*y doesn't fit in an int.
    int64 dz = (int64)g_config.stick_deadzone * 32767 / 100;
    if ((int64)last_x * last_x + (int64)last_y * last_y >= dz * dz) {
      // in the non deadzone part, divide the circle into eight 45 degree
      // segments rotated by 22.5 degrees that control which direction to move.
      // todo: do this without floats?
      static const uint8 kSegmentToButtons[8] = {
        1 << 4,           // 0 = up
        1 << 4 | 1 << 7,  // 1 = up, right
        1 << 7,           // 2 = right
        1 << 7 | 1 << 5,  // 3 = right, down
        1 << 5,           // 4 = down
        1 << 5 | 1 << 6,  // 5 = down, left
        1 << 6,           // 6 = left
        1 << 6 | 1 << 4,  // 7 = left, up
      };
      uint8 angle = (uint8)(int)(ApproximateAtan2(last_y, last_x) * 64.0f + 0.5f);
      buttons = kSegmentToButtons[(uint8)(angle + 16 + 64) >> 5];
    }
    g_gamepad_buttons = buttons;
  } else if ((axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
    if (value < 12000 || value >= 16000)  // hysteresis
      HandleGamepadInput(axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? kGamepadBtn_L2 : kGamepadBtn_R2, value >= 12000);
  }
}

static bool LoadRom(const char *filename) {
  size_t length = 0;
  uint8 *file = ReadWholeFile(filename, &length);
  if(!file) Die("Failed to read file");
  bool result = EmuInitialize(file, length);
  free(file);
  return result;
}

static bool ParseLinkGraphics(uint8 *file, size_t length) {
  if (length < 27 || memcmp(file, "ZSPR", 4) != 0)
    return false;
  uint32 pixel_offs = DWORD(file[9]);
  uint32 pixel_length = WORD(file[13]);
  uint32 palette_offs = DWORD(file[15]);
  uint32 palette_length = WORD(file[19]);
  if ((uint64)pixel_offs + pixel_length > length ||
      (uint64)palette_offs + palette_length > length ||
      pixel_length != 0x7000)
    return false;
  if (kPalette_ArmorAndGloves_SIZE != 150 || kLinkGraphics_SIZE != 0x7000)
    Die("ParseLinkGraphics: Invalid asset sizes");
  memcpy(kLinkGraphics, file + pixel_offs, 0x7000);
  if (palette_length >= 120)
    memcpy(kPalette_ArmorAndGloves, file + palette_offs, 120);
  if (palette_length >= 124)
    memcpy(kGlovesColor, file + palette_offs + 120, 4);
  return true;
}

static void LoadLinkGraphics() {
  if (g_config.link_graphics) {
    fprintf(stderr, "Loading Link Graphics: %s\n", g_config.link_graphics);
    size_t length = 0;
    uint8 *file = ReadWholeFile(g_config.link_graphics, &length);
    if (file == NULL || !ParseLinkGraphics(file, length))
      Die("Unable to load file");
    free(file);
  }
}


const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];

static void LoadAssets() {
  size_t length = 0;
  uint8 *data = ReadWholeFile("zelda3_assets.dat", &length);
  if (!data) {
    size_t bps_length, bps_src_length;
    uint8 *bps, *bps_src;
    bps = ReadWholeFile("zelda3_assets.bps", &bps_length);
    if (!bps)
      Die("Failed to read zelda3_assets.dat. Please see the README for information about how you get this file.");
    bps_src = ReadWholeFile("zelda3.sfc", &bps_src_length);
    if (!bps_src)
      Die("Missing file: zelda3.sfc");
    data = ApplyBps(bps_src, bps_src_length, bps, bps_length, &length);
    if (!data)
      Die("Unable to apply zelda3_assets.bps. Please make sure you got the right version of 'zelda3.sfc'");
  }

  static const char kAssetsSig[] = { kAssets_Sig };

  if (length < 16 + 32 + 32 + 8 + kNumberOfAssets * 4 ||
      memcmp(data, kAssetsSig, 48) != 0 ||
      *(uint32*)(data + 80) != kNumberOfAssets)
    Die("Invalid assets file");

  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);

  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if ((uint64)offset + size > length)
      Die("Assets file corruption");
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }

  if (g_config.features0 & kFeatures0_DimFlashes) { // patch dungeon floor palettes
    kPalette_DungBgMain[0x484] = 0x70;
    kPalette_DungBgMain[0x485] = 0x95;
    kPalette_DungBgMain[0x486] = 0x57;
  }
}

// Go some steps up and find zelda3.ini
static void SwitchDirectory() {
  char buf[4096];
  if (!getcwd(buf, sizeof(buf) - 32))
    return;
  size_t pos = strlen(buf);

  for (int step = 0; pos != 0 && step < 3; step++) {
    memcpy(buf + pos, "/zelda3.ini", 12);
    FILE *f = fopen(buf, "rb");
    if (f) {
      fclose(f);
      buf[pos] = 0;
      if (step != 0) {
        printf("Found zelda3.ini in %s\n", buf);
        int err = chdir(buf);
        (void)err;
      }
      return;
    }
    pos--;
    while (pos != 0 && buf[pos] != '/' && buf[pos] != '\\')
      pos--;
  }
}

MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
