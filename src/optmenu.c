#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "types.h"
#include "config.h"
#include "features.h"
#include "audio.h"
#include "zelda_rtl.h"
#include "savemenu.h"
#include "optmenu.h"
#include "snes/ppu.h"
#include "haptics.h"
#include "discord.h"

// ---------------------------------------------------------------------------
// Option definitions
// ---------------------------------------------------------------------------

enum {
  kT_Header,
  kT_Slider,
  kT_Toggle,
  kT_Shader,
  kT_Choice,
};

enum {
  kO_None,
  kO_Volume,
  kO_Music,
  kO_Sfx,
  kO_Shader,
  kO_Fullscreen,
  kO_KeepAspect,
  kO_Linear,
  kO_NewRenderer,
  kO_Mode7,
  kO_NoSpriteLimits,
  kO_RewindSeconds,
  kO_AutosaveMinutes,
  kO_AutosaveSlots,
  kO_Aspect,
  kO_EdgeEffect,
  kO_WideHud,
  kO_Rumble,
  kO_Discord,
  kO_DiscordLocation,
  kO_Deadzone,
  kO_TurboSpeed,
  kO_SaveHold,
  kO_Feature,  // + index in kFeatureDefs
};

typedef struct FeatureDef {
  uint32 bit;
  const char *ini;
} FeatureDef;

static const FeatureDef kFeatureDefs[] = {
  { kFeatures0_SwitchLR, "ItemSwitchLR" },
  { kFeatures0_SwitchLRLimit, "ItemSwitchLRLimit" },
  { kFeatures0_TurnWhileDashing, "TurnWhileDashing" },
  { kFeatures0_MirrorToDarkworld, "MirrorToDarkworld" },
  { kFeatures0_CollectItemsWithSword, "CollectItemsWithSword" },
  { kFeatures0_BreakPotsWithSword, "BreakPotsWithSword" },
  { kFeatures0_DisableLowHealthBeep, "DisableLowHealthBeep" },
  { kFeatures0_SkipIntroOnKeypress, "SkipIntroOnKeypress" },
  { kFeatures0_ShowMaxItemsInYellow, "ShowMaxItemsInYellow" },
  { kFeatures0_MoreActiveBombs, "MoreActiveBombs" },
  { kFeatures0_CarryMoreRupees, "CarryMoreRupees" },
  { kFeatures0_CancelBirdTravel, "CancelBirdTravel" },
  { kFeatures0_MiscBugFixes, "MiscBugFixes" },
  { kFeatures0_GameChangingBugFixes, "GameChangingBugFixes" },
  { kFeatures0_FastDialog, "FastDialog" },
};

typedef struct OptDef {
  uint8 type, id;
  const char *label;
  const char *help;
} OptDef;

static const OptDef kOptions[] = {
  { kT_Header, 0, "Sonido", NULL },
  { kT_Slider, kO_Volume, "Volumen general", "Volumen de todo el juego. También se cambia con las teclas de volumen." },
  { kT_Slider, kO_Music, "Música (MSU)", "Volumen de la música MSU. Sin MSU, la música del juego sale junto con los efectos." },
  { kT_Slider, kO_Sfx, "Efectos", "Volumen de los efectos de sonido (y de la música original si no hay MSU)." },
  { kT_Header, 0, "Imagen", NULL },
  { kT_Shader, kO_Shader, "Shader", "A: elegir un shader de la carpeta glsl-shaders. Se aplica en el momento." },
  { kT_Choice, kO_Aspect, "Formato de pantalla", "Ancho de la imagen. Se aplica al reiniciar el juego." },
  { kT_Toggle, kO_WideHud, "HUD en los bordes", "En pantalla ancha, lleva la magia y los ítems a la izquierda y los corazones a la derecha." },
  { kT_Toggle, kO_EdgeEffect, "Rellenar bordes", "En pantalla ancha, rellena las franjas negras con un reflejo borroso de la escena." },
  { kT_Toggle, kO_Fullscreen, "Pantalla completa", "Alternar entre ventana y pantalla completa." },
  { kT_Toggle, kO_KeepAspect, "Mantener proporción", "Si se desactiva, la imagen se estira para llenar la pantalla." },
  { kT_Toggle, kO_Linear, "Filtro suave", "Suaviza los píxeles. Sin efecto mientras haya un shader activo." },
  { kT_Toggle, kO_NewRenderer, "Renderizador rápido", "Renderizador optimizado del PPU. Desactivalo si ves errores gráficos." },
  { kT_Toggle, kO_Mode7, "Mapa en alta resolución", "Dibuja el mapa del mundo (Mode 7) con 4 veces más resolución." },
  { kT_Toggle, kO_NoSpriteLimits, "Sin límite de sprites", "Evita que los sprites parpadeen o desaparezcan cuando hay muchos juntos." },
  { kT_Header, 0, "Joystick", NULL },
  { kT_Slider, kO_Rumble, "Vibración", "Fuerza de la vibración con espadazos, golpes, daño, ítems, bombas y cofres. 0 la apaga." },
  { kT_Slider, kO_Deadzone, "Zona muerta del stick", "Cuánto hay que mover el stick antes de que Link camine. Subila si Link se mueve solo." },
  { kT_Header, 0, "Discord", NULL },
  { kT_Toggle, kO_Discord, "Mostrar en Discord", "Muestra en tu perfil de Discord que estás jugando, con corazones, cristales y tiempo de juego." },
  { kT_Toggle, kO_DiscordLocation, "Mostrar dónde estás", "Incluye en Discord el dungeon o la zona del mapa en la que estás." },
  { kT_Header, 0, "Rebobinado, avance y autoguardado", NULL },
  { kT_Slider, kO_TurboSpeed, "Avance rápido", "Velocidad mientras mantenés la tecla de avance rápido." },
  { kT_Slider, kO_RewindSeconds, "Rebobinado", "Cuántos segundos se pueden rebobinar. Cambiarlo borra el historial actual." },
  { kT_Slider, kO_AutosaveMinutes, "Autoguardar cada", "Cada cuánto se guarda un state solo. Igual se guarda al salir y al entrar o salir de dungeons." },
  { kT_Slider, kO_SaveHold, "Guardar state manteniendo", "Cuánto hay que mantener la tecla de guardar state para que guarde. Soltar antes cancela. 0 guarda al instante." },
  { kT_Slider, kO_AutosaveSlots, "Autoguardados", "Cuántos autoguardados se conservan en saves/auto. Con 0 se desactiva el autoguardado." },
  { kT_Header, 0, "Juego", NULL },
  { kT_Toggle, kO_Feature + 0, "Cambiar ítem con L/R", "L y R cambian el ítem equipado. En el inventario, mantené X, L o R para asignar ítems." },
  { kT_Toggle, kO_Feature + 1, "L/R solo primeros 4", "Con el cambio de ítem con L/R, recorre solo los primeros 4 ítems." },
  { kT_Toggle, kO_Feature + 2, "Girar al correr", "Permite cambiar de dirección mientras corrés con las botas." },
  { kT_Toggle, kO_Feature + 3, "Espejo al Mundo Oscuro", "El espejo también lleva del Mundo de la Luz al Mundo Oscuro." },
  { kT_Toggle, kO_Feature + 4, "Agarrar con la espada", "Agarrá corazones, rupias y demás tocándolos con la espada." },
  { kT_Toggle, kO_Feature + 5, "Romper vasijas con espada", "Con la espada nivel 2 o más se pueden romper vasijas." },
  { kT_Toggle, kO_Feature + 6, "Sin pitido de vida baja", "Quita el pitido que suena cuando te queda poca vida." },
  { kT_Toggle, kO_Feature + 7, "Saltar intro", "Cualquier botón saltea la intro desde el arranque y va directo a elegir partida." },
  { kT_Toggle, kO_Feature + 8, "Máximos en amarillo", "Muestra en amarillo las rupias, bombas y flechas al máximo." },
  { kT_Toggle, kO_Feature + 9, "Más bombas a la vez", "Permite tener cuatro bombas activas en vez de dos." },
  { kT_Toggle, kO_Feature + 10, "Hasta 9999 rupias", "La billetera llega a 9999 rupias en vez de 999." },
  { kT_Toggle, kO_Feature + 11, "Cancelar viaje del pájaro", "Permite cancelar el viaje en el pájaro con X." },
  { kT_Toggle, kO_Feature + 12, "Arreglos de bugs", "Corrige varios bugs del juego original." },
  { kT_Toggle, kO_Feature + 13, "Arreglos que cambian el juego", "Corrige bugs que cambian cómo se juega (por ejemplo, algunos trucos dejan de funcionar)." },
  { kT_Toggle, kO_Feature + 14, "Diálogos rápidos", "Mantené A para que el texto vaya al doble de velocidad; B escribe el resto del cuadro de una." },
};

typedef struct CtlDef {
  uint8 header;
  uint16 cmd;
  const char *label;
} CtlDef;

static const CtlDef kControls[] = {
  { 1, 0, "Controles del juego" },
  { 0, kKeys_Controls + 0, "Arriba" },
  { 0, kKeys_Controls + 1, "Abajo" },
  { 0, kKeys_Controls + 2, "Izquierda" },
  { 0, kKeys_Controls + 3, "Derecha" },
  { 0, kKeys_Controls + 6, "A (acción)" },
  { 0, kKeys_Controls + 7, "B (espada)" },
  { 0, kKeys_Controls + 8, "X (mapa)" },
  { 0, kKeys_Controls + 9, "Y (ítem)" },
  { 0, kKeys_Controls + 10, "L" },
  { 0, kKeys_Controls + 11, "R" },
  { 0, kKeys_Controls + 5, "Start" },
  { 0, kKeys_Controls + 4, "Select" },
  { 1, 0, "Atajos" },
  { 0, kKeys_SaveMenu, "Este menú" },
  { 0, kKeys_Rewind, "Rebobinar (mantener)" },
  { 0, kKeys_Turbo, "Avance rápido (mantener)" },
  { 0, kKeys_Save + 0, "Guardar state 1" },
  { 0, kKeys_Load + 0, "Cargar state 1" },
  { 0, kKeys_PauseDimmed, "Pausa" },
  { 0, kKeys_Pause, "Pausa sin oscurecer" },
  { 0, kKeys_Fullscreen, "Pantalla completa" },
  { 0, kKeys_VolumeUp, "Subir volumen" },
  { 0, kKeys_VolumeDown, "Bajar volumen" },
  { 0, kKeys_WindowBigger, "Ventana más grande" },
  { 0, kKeys_WindowSmaller, "Ventana más chica" },
  { 0, kKeys_DisplayPerf, "Mostrar FPS" },
  { 0, kKeys_Reset, "Reiniciar el juego" },
  { 0, kKeys_Quit, "Salir del juego" },
  { 1, 0, "Más ranuras de states" },
  { 0, kKeys_Save + 1, "Guardar state 2" },
  { 0, kKeys_Load + 1, "Cargar state 2" },
  { 0, kKeys_Save + 2, "Guardar state 3" },
  { 0, kKeys_Load + 2, "Cargar state 3" },
  { 0, kKeys_Save + 3, "Guardar state 4" },
  { 0, kKeys_Load + 3, "Cargar state 4" },
  { 0, kKeys_Save + 4, "Guardar state 5" },
  { 0, kKeys_Load + 4, "Cargar state 5" },
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

typedef struct ShaderFolder {
  char name[64];
  int first, count;
} ShaderFolder;

enum {
  kCaptureTimeoutMs = 6000,
  kRowH = 15,
  kCtlRowH = 14,
};

static struct {
  int sel[2], scroll[2];
  int col;  // controls page: 0 = keyboard, 1 = gamepad

  // Shader picker: 0 = closed, 1 = folders, 2 = files of a folder
  int picker;
  char **files;  // paths relative to kShaderRoot, sorted
  int num_files;
  ShaderFolder folders[128];
  int num_folders;
  int fsel, fscroll, psel, pscroll;

  bool capturing, cap_pad, cap_got;
  int cap_cmd;
  uint32 cap_start;
  uint32 cap_held, cap_mods;
  int cap_last;
  SDL_Keycode cap_mod_key;

  char msg[128];
  uint32 msg_until;
} g_om;

static const char kShaderRoot[] = "glsl-shaders";

static void SetMsg(const char *fmt, const char *arg) {
  snprintf(g_om.msg, sizeof(g_om.msg), fmt, arg ? arg : "");
  g_om.msg_until = SDL_GetTicks() + 3500;
}

static void SaveInt(const char *section, const char *key, int v) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", v);
  Config_SetOverride(section, key, buf);
}

static int Clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

static void ApplyVolumes() {
  ZeldaSetVolumes(g_config.volume, g_config.msuvolume, g_config.sfx_volume);
}

void OptMenu_ApplyStartupSettings() {
  ApplyVolumes();
}

int OptMenu_ChangeVolume(int delta) {
  g_config.volume = Clamp(g_config.volume + delta * 5, 0, 100);
  ApplyVolumes();
  SaveInt("Sound", "Volume", g_config.volume);
  return g_config.volume;
}

static bool IsOpenGL() {
  return Main_ShaderSupported();
}

// Aspect ratios offered in the menu (ExtendedAspectRatio). Changing it needs a restart,
// so the chosen one is remembered here until then.
static const char *const kAspectNames[] = { "4:3", "16:9", "16:10", "18:9", "3:2" };
static int g_aspect_choice = -1;

static int CurrentAspect() {
  if (g_aspect_choice >= 0)
    return g_aspect_choice;
  int h = g_config.extend_y ? 240 : 224;
  int ear = g_config.extended_aspect_ratio;
  static const int kNum[] = { 4, 16, 16, 18, 3 }, kDen[] = { 3, 9, 10, 9, 2 };
  for (int i = 0; i < 5; i++) {
    int e = i == 0 ? 0 : (h * kNum[i] / kDen[i] - 256) / 2;
    if (e == ear)
      return g_aspect_choice = i;
  }
  return g_aspect_choice = 1;
}

static void SaveAspect(int choice) {
  char buf[96];
  bool wide = choice != 0;
  snprintf(buf, sizeof(buf), "%s%s%s%s", g_config.extend_y ? "extend_y, " : "", kAspectNames[choice],
           wide && g_config.extended_aspect_ratio && !(g_config.features0 & kFeatures0_ExtendScreen64) ? ", unchanged_sprites" : "",
           wide && g_config.extended_aspect_ratio && !(g_config.features0 & kFeatures0_WidescreenVisualFixes) ? ", no_visual_fixes" : "");
  Config_SetOverride("General", "ExtendedAspectRatio", buf);
}

static int GetValue(int id) {
  switch (id) {
  case kO_Volume: return g_config.volume;
  case kO_Music: return g_config.msuvolume;
  case kO_Sfx: return g_config.sfx_volume;
  case kO_Fullscreen: return Main_IsFullscreen();
  case kO_KeepAspect: return !g_config.ignore_aspect_ratio;
  case kO_Linear: return g_config.linear_filtering;
  case kO_NewRenderer: return g_config.new_renderer;
  case kO_Mode7: return g_config.enhanced_mode7;
  case kO_NoSpriteLimits: return g_config.no_sprite_limits;
  case kO_RewindSeconds: return g_config.rewind_seconds;
  case kO_AutosaveMinutes: return g_config.autosave_minutes;
  case kO_AutosaveSlots: return g_config.autosave_slots;
  case kO_EdgeEffect: return g_config.edge_effect;
  case kO_WideHud: return g_config.wide_hud;
  case kO_Discord: return g_config.discord;
  case kO_DiscordLocation: return g_config.discord_location;
  case kO_Rumble: return g_config.rumble_intensity;
  case kO_Deadzone: return g_config.stick_deadzone;
  case kO_TurboSpeed: return g_config.turbo_speed < 2 ? 1 : g_config.turbo_speed;
  case kO_SaveHold: return g_config.save_hold_ms;
  case kO_Aspect: return CurrentAspect();
  default:
    if (id >= kO_Feature && id < kO_Feature + countof(kFeatureDefs))
      return (g_config.features0 & kFeatureDefs[id - kO_Feature].bit) != 0;
    return 0;
  }
}

static void SliderRange(int id, int *lo, int *hi, int *step) {
  *lo = 0, *hi = 100, *step = 5;
  if (id == kO_RewindSeconds) *hi = 300, *step = 10;
  if (id == kO_AutosaveMinutes) *hi = 60, *step = 1;
  if (id == kO_AutosaveSlots) *hi = 50, *step = 1;
  if (id == kO_Rumble) *hi = 100, *step = 10;
  if (id == kO_Deadzone) *lo = 10, *hi = 60, *step = 5;
  if (id == kO_TurboSpeed) *lo = 1, *hi = 10, *step = 1;
  if (id == kO_SaveHold) *hi = 5000, *step = 500;
}

static void FormatValue(int id, char *buf, size_t n) {
  int v = GetValue(id);
  switch (id) {
  case kO_Volume: case kO_Music: case kO_Sfx:
    snprintf(buf, n, "%d%%", v);
    break;
  case kO_RewindSeconds:
    if (v <= 0) snprintf(buf, n, "Desactivado");
    else if (v % 60 == 0) snprintf(buf, n, "%d min", v / 60);
    else snprintf(buf, n, "%d s", v);
    break;
  case kO_AutosaveMinutes:
    if (v <= 0) snprintf(buf, n, "Sin tiempo fijo");
    else snprintf(buf, n, "%d min", v);
    break;
  case kO_AutosaveSlots:
    if (v <= 0) snprintf(buf, n, "Desactivado");
    else snprintf(buf, n, "%d", v);
    break;
  case kO_Rumble:
    if (v <= 0) snprintf(buf, n, "Apagada");
    else snprintf(buf, n, "%d%%", v);
    break;
  case kO_Deadzone:
    snprintf(buf, n, "%d%%", v);
    break;
  case kO_TurboSpeed:
    if (v <= 1) snprintf(buf, n, "Máxima");
    else snprintf(buf, n, "x%d", v);
    break;
  case kO_SaveHold:
    if (v <= 0) snprintf(buf, n, "Al instante");
    else if (v % 1000 == 0) snprintf(buf, n, "%d s", v / 1000);
    else snprintf(buf, n, "%d,%d s", v / 1000, v % 1000 / 100);
    break;
  case kO_Aspect:
    snprintf(buf, n, "%s", kAspectNames[v]);
    break;
  default:
    snprintf(buf, n, "%s", v ? "Sí" : "No");
    break;
  }
}

static void SetRenderFlag(int flag, bool on, bool *cfg, const char *ini) {
  *cfg = on;
  SaveInt("Graphics", ini, on);
  // The SDL renderer sizes its texture for the Mode 7 setting at startup.
  if (flag == kPpuRenderFlags_4x4Mode7 && !IsOpenGL()) {
    SetMsg("Se aplica al reiniciar el juego%s", NULL);
    return;
  }
  int f = Main_GetRenderFlags();
  Main_SetRenderFlags(on ? (f | flag) : (f & ~flag));
}

static void ChangeOption(int id, int dir) {
  int type = 0;
  for (int i = 0; i < countof(kOptions); i++)
    if (kOptions[i].id == id)
      type = kOptions[i].type;

  if (type == kT_Slider) {
    int lo, hi, step;
    SliderRange(id, &lo, &hi, &step);
    if (dir == 0)
      return;
    int v = GetValue(id);
    int nv = Clamp((v + dir * step) / step * step, lo, hi);
    if (nv == v)
      return;
    switch (id) {
    case kO_Volume:
      g_config.volume = nv, ApplyVolumes(), SaveInt("Sound", "Volume", nv);
      break;
    case kO_Music:
      g_config.msuvolume = nv, ApplyVolumes(), SaveInt("Sound", "MSUVolume", nv);
      break;
    case kO_Sfx:
      g_config.sfx_volume = nv, ApplyVolumes(), SaveInt("Sound", "SfxVolume", nv);
      break;
    case kO_RewindSeconds:
      g_config.rewind_seconds = nv;
      ZeldaRewindConfigure(nv, g_config.rewind_frame_interval);
      SaveInt("General", "RewindSeconds", nv);
      break;
    case kO_AutosaveMinutes:
      g_config.autosave_minutes = nv;
      SaveMenu_Init(g_config.autosave_minutes, g_config.autosave_slots);
      SaveInt("General", "AutosaveMinutes", nv);
      break;
    case kO_Rumble:
      g_config.rumble_intensity = nv;
      Haptics_Configure(nv);
      SaveInt("General", "Rumble", nv);
      Haptic_Event(kHaptic_SwordHitEnemy, 0);  // let the player feel the new strength
      break;
    case kO_Deadzone:
      g_config.stick_deadzone = nv;
      SaveInt("General", "StickDeadzone", nv);
      break;
    case kO_TurboSpeed:
      g_config.turbo_speed = nv <= 1 ? 0 : nv;
      SaveInt("General", "TurboSpeed", g_config.turbo_speed);
      break;
    case kO_SaveHold:
      g_config.save_hold_ms = nv;
      SaveInt("General", "SaveHoldMs", nv);
      break;
    case kO_AutosaveSlots:
      g_config.autosave_slots = nv;
      SaveMenu_Init(g_config.autosave_minutes, g_config.autosave_slots);
      SaveInt("General", "AutosaveSlots", nv);
      break;
    }
    return;
  }

  if (type == kT_Choice && id == kO_Aspect) {
    if (dir == 0) dir = 1;
    int c = (CurrentAspect() + dir + 5) % 5;
    g_aspect_choice = c;
    SaveAspect(c);
    SetMsg("Formato %s: se aplica al reiniciar el juego", kAspectNames[c]);
    return;
  }
  if (type != kT_Toggle)
    return;
  bool on = !GetValue(id);
  switch (id) {
  case kO_Fullscreen:
    Main_SetFullscreen(on);
    SaveInt("Graphics", "Fullscreen", on);
    break;
  case kO_KeepAspect:
    g_config.ignore_aspect_ratio = !on;
    SaveInt("Graphics", "IgnoreAspectRatio", !on);
    if (!IsOpenGL())
      SetMsg("Se aplica al reiniciar el juego%s", NULL);
    break;
  case kO_Linear:
    g_config.linear_filtering = on;
    SaveInt("Graphics", "LinearFiltering", on);
    if (!IsOpenGL())
      SetMsg("Se aplica al reiniciar el juego%s", NULL);
    break;
  case kO_NewRenderer:
    SetRenderFlag(kPpuRenderFlags_NewRenderer, on, &g_config.new_renderer, "NewRenderer");
    break;
  case kO_Mode7:
    SetRenderFlag(kPpuRenderFlags_4x4Mode7, on, &g_config.enhanced_mode7, "EnhancedMode7");
    break;
  case kO_EdgeEffect:
    g_config.edge_effect = on;
    SaveInt("Graphics", "EdgeEffect", on);
    break;
  case kO_WideHud:
    g_config.wide_hud = g_wide_hud = on;
    SaveInt("Graphics", "WideHud", on);
    break;
  case kO_Discord:
    g_config.discord = on;
    Discord_Configure(g_config.discord, g_config.discord_location);
    SaveInt("General", "Discord", on);
    break;
  case kO_DiscordLocation:
    g_config.discord_location = on;
    Discord_Configure(g_config.discord, g_config.discord_location);
    SaveInt("General", "DiscordLocation", on);
    break;
  case kO_NoSpriteLimits:
    SetRenderFlag(kPpuRenderFlags_NoSpriteLimits, on, &g_config.no_sprite_limits, "NoSpriteLimits");
    break;
  default:
    if (id >= kO_Feature && id < kO_Feature + countof(kFeatureDefs)) {
      const FeatureDef *fd = &kFeatureDefs[id - kO_Feature];
      g_config.features0 = (g_config.features0 & ~fd->bit) | (on ? fd->bit : 0);
      // Applied to the game RAM on the next frame (not during replays).
      g_wanted_zelda_features = (g_wanted_zelda_features & ~fd->bit) | (on ? fd->bit : 0);
      SaveInt("Features", fd->ini, on);
    }
    break;
  }
}

// ---------------------------------------------------------------------------
// Shader list
// ---------------------------------------------------------------------------

static bool HasSuffixNoCase(const char *s, const char *suffix) {
  size_t a = strlen(s), b = strlen(suffix);
  return a >= b && SDL_strcasecmp(s + a - b, suffix) == 0;
}

static void AddShaderFile(const char *rel) {
  if ((g_om.num_files & 63) == 0) {
    char **nf = realloc(g_om.files, sizeof(char *) * (g_om.num_files + 64));
    if (!nf)
      return;
    g_om.files = nf;
  }
  g_om.files[g_om.num_files++] = strdup(rel);
}

static void ScanShaderDir(const char *rel, int depth) {
  if (depth > 4 || g_om.num_files > 4000)
    return;
  char path[512];
#ifdef _WIN32
  snprintf(path, sizeof(path), "%s/%s%s*", kShaderRoot, rel, *rel ? "/" : "");
  struct _finddata_t fd;
  intptr_t h = _findfirst(path, &fd);
  if (h == -1)
    return;
  do {
    if (fd.name[0] == '.')
      continue;
    char sub[512];
    snprintf(sub, sizeof(sub), "%s%s%s", rel, *rel ? "/" : "", fd.name);
    if (fd.attrib & _A_SUBDIR)
      ScanShaderDir(sub, depth + 1);
    else if (HasSuffixNoCase(fd.name, ".glslp"))
      AddShaderFile(sub);
  } while (_findnext(h, &fd) == 0);
  _findclose(h);
#else
  snprintf(path, sizeof(path), "%s/%s", kShaderRoot, rel);
  DIR *dir = opendir(path);
  if (!dir)
    return;
  struct dirent *de;
  while ((de = readdir(dir)) != NULL) {
    if (de->d_name[0] == '.')
      continue;
    char sub[512], full[1024];
    snprintf(sub, sizeof(sub), "%s%s%s", rel, *rel ? "/" : "", de->d_name);
    snprintf(full, sizeof(full), "%s/%s", kShaderRoot, sub);
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
      ScanShaderDir(sub, depth + 1);
    else if (HasSuffixNoCase(de->d_name, ".glslp"))
      AddShaderFile(sub);
  }
  closedir(dir);
#endif
}

static int CompareStrings(const void *a, const void *b) {
  return SDL_strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

static void FolderOf(const char *rel, char *buf, size_t n) {
  const char *slash = strchr(rel, '/');
  if (!slash)
    snprintf(buf, n, "(sueltos)");
  else
    snprintf(buf, n, "%.*s", (int)(slash - rel), rel);
}

static void LoadShaderList() {
  if (g_om.files)
    return;
  ScanShaderDir("", 0);
  if (!g_om.files)
    return;
  qsort(g_om.files, g_om.num_files, sizeof(char *), CompareStrings);
  // Loose files first, then the folders in order.
  g_om.num_folders = 0;
  for (int i = 0; i < g_om.num_files; i++) {
    char folder[64];
    FolderOf(g_om.files[i], folder, sizeof(folder));
    ShaderFolder *f = g_om.num_folders ? &g_om.folders[g_om.num_folders - 1] : NULL;
    if (!f || strcmp(f->name, folder) != 0) {
      if (g_om.num_folders >= countof(g_om.folders))
        break;
      f = &g_om.folders[g_om.num_folders++];
      snprintf(f->name, sizeof(f->name), "%s", folder);
      f->first = i;
      f->count = 0;
    }
    f->count++;
  }
}

// Returns the path relative to kShaderRoot of the current shader, or NULL.
static const char *CurrentShaderRel() {
  const char *s = g_config.shader;
  if (!s || !*s)
    return NULL;
  size_t n = strlen(kShaderRoot);
  if (SDL_strncasecmp(s, kShaderRoot, n) == 0 && (s[n] == '/' || s[n] == '\\'))
    return s + n + 1;
  return s;
}

static bool SamePath(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    int ca = *a == '\\' ? '/' : *a, cb = *b == '\\' ? '/' : *b;
    if (SDL_tolower(ca) != SDL_tolower(cb))
      return false;
  }
  return *a == *b;
}

static void ShaderDisplayName(const char *path, char *buf, size_t n) {
  if (!path || !*path) {
    snprintf(buf, n, "Ninguno");
    return;
  }
  const char *base = path;
  for (const char *p = path; *p; p++)
    if (*p == '/' || *p == '\\')
      base = p + 1;
  size_t len = strlen(base);
  if (HasSuffixNoCase(base, ".glslp")) len -= 6;
  else if (HasSuffixNoCase(base, ".glsl")) len -= 5;
  snprintf(buf, n, "%.*s", (int)len, base);
}

static void OpenShaderPicker() {
  if (!IsOpenGL()) {
    SetMsg("Los shaders necesitan OutputMethod = OpenGL en zelda3.ini%s", NULL);
    return;
  }
  LoadShaderList();
  if (!g_om.num_files) {
    SetMsg("No encontré shaders en la carpeta %s", kShaderRoot);
    return;
  }
  g_om.picker = 1;
  g_om.fsel = 0;
  // Start at the folder of the current shader.
  const char *cur = CurrentShaderRel();
  if (cur) {
    for (int i = 0; i < g_om.num_folders; i++) {
      ShaderFolder *f = &g_om.folders[i];
      for (int j = f->first; j < f->first + f->count; j++)
        if (SamePath(g_om.files[j], cur))
          g_om.fsel = i + 1;
    }
  }
}

static void EnterShaderFolder() {
  ShaderFolder *f = &g_om.folders[g_om.fsel - 1];
  g_om.picker = 2;
  g_om.psel = 0;
  g_om.pscroll = 0;
  const char *cur = CurrentShaderRel();
  for (int j = 0; cur && j < f->count; j++)
    if (SamePath(g_om.files[f->first + j], cur))
      g_om.psel = j;
}

static void ApplyShader(const char *rel) {
  char path[512] = "";
  if (rel)
    snprintf(path, sizeof(path), "%s/%s", kShaderRoot, rel);
  char name[96];
  ShaderDisplayName(path, name, sizeof(name));
  if (!Main_SetShader(path)) {
    SetMsg("No se pudo cargar \"%s\" (quedó el anterior)", name);
    return;
  }
  g_config.shader = rel ? strdup(path) : NULL;
  Config_SetOverride("Graphics", "Shader", path);
  SetMsg("Shader: %s", name);
}

// ---------------------------------------------------------------------------
// Key capture
// ---------------------------------------------------------------------------

static const char *ControlLabel(int cmd) {
  for (int i = 0; i < countof(kControls); i++)
    if (!kControls[i].header && kControls[i].cmd == cmd)
      return kControls[i].label;
  const char *n = Config_CommandIniName(cmd);
  return n ? n : "?";
}

static void FinishCapture() {
  g_om.capturing = false;
  g_om.cap_held = 0;
}

static void ReportStolen(int stolen) {
  if (stolen) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s", ControlLabel(stolen));
    SetMsg("Se lo saqué a: %s", buf);
  } else {
    g_om.msg_until = 0;
  }
}

bool OptMenu_IsCapturing() {
  if (g_om.capturing && SDL_GetTicks() - g_om.cap_start > kCaptureTimeoutMs) {
    FinishCapture();
    SetMsg("Cancelado%s", NULL);
  }
  return g_om.capturing;
}

static bool IsModifierKey(SDL_Keycode k) {
  return k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LCTRL || k == SDLK_RCTRL ||
         k == SDLK_LALT || k == SDLK_RALT;
}

static void BindKey(SDL_Keycode key, SDL_Keymod mod) {
  int code = Config_EncodeKey(key, mod);
  FinishCapture();
  if (!code || key == SDLK_COMMA) {
    SetMsg("Esa tecla no se puede usar%s", NULL);
    return;
  }
  ReportStolen(Config_SetKeyBinding(g_om.cap_cmd, code));
}

void OptMenu_CaptureKey(SDL_Keycode key, SDL_Keymod mod, bool pressed) {
  if (!g_om.capturing)
    return;
  if (g_om.cap_pad) {
    // Any key cancels waiting for a gamepad button.
    if (pressed) {
      FinishCapture();
      SetMsg("Cancelado%s", NULL);
    }
    return;
  }
  if (pressed) {
    if (IsModifierKey(key)) {
      g_om.cap_mod_key = key;  // bound on release if no other key comes
    } else {
      BindKey(key, mod);
    }
  } else if (key == g_om.cap_mod_key) {
    BindKey(key, 0);
  }
}

void OptMenu_CaptureButton(int button, bool pressed) {
  if (!g_om.capturing || button < 0 || button >= kGamepadBtn_Count)
    return;
  if (!g_om.cap_pad) {
    // Any button cancels waiting for a key.
    if (pressed) {
      FinishCapture();
      SetMsg("Cancelado%s", NULL);
    }
    return;
  }
  if (pressed) {
    g_om.cap_mods = g_om.cap_held;
    g_om.cap_held |= 1u << button;
    g_om.cap_last = button;
    g_om.cap_got = true;
  } else {
    g_om.cap_held &= ~(1u << button);
    // The combo is complete when the last pressed button is released.
    if (g_om.cap_got && button == g_om.cap_last) {
      int cmd = g_om.cap_cmd;
      uint32 mods = g_om.cap_mods;
      FinishCapture();
      ReportStolen(Config_SetPadBinding(cmd, button, mods));
    }
  }
}

static void StartCapture(int cmd, bool pad) {
  g_om.capturing = true;
  g_om.cap_pad = pad;
  g_om.cap_cmd = cmd;
  g_om.cap_start = SDL_GetTicks();
  g_om.cap_held = 0;
  g_om.cap_mods = 0;
  g_om.cap_got = false;
  g_om.cap_last = -1;
  g_om.cap_mod_key = SDLK_UNKNOWN;
  g_om.msg_until = 0;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void OptMenu_Open() {
  g_om.picker = 0;
  g_om.capturing = false;
  g_om.msg_until = 0;
}

static int NumRows(int page) {
  return page == kOptPage_Options ? countof(kOptions) : countof(kControls);
}

static bool IsHeader(int page, int i) {
  return page == kOptPage_Options ? kOptions[i].type == kT_Header : kControls[i].header;
}

static void MoveSel(int page, int dir) {
  int n = NumRows(page), s = g_om.sel[page];
  for (int k = 0; k < n; k++) {
    s = (s + dir + n) % n;
    if (!IsHeader(page, s))
      break;
  }
  g_om.sel[page] = s;
}

static void MoveList(int *sel, int n, int delta) {
  if (n <= 0)
    return;
  if (delta == 1 || delta == -1)
    *sel = (*sel + delta + n) % n;
  else
    *sel = Clamp(*sel + delta, 0, n - 1);
}

static bool PickerInput(int action) {
  if (g_om.picker == 1) {
    int n = g_om.num_folders + 1;
    switch (action) {
    case kSaveMenu_Up: MoveList(&g_om.fsel, n, -1); break;
    case kSaveMenu_Down: MoveList(&g_om.fsel, n, 1); break;
    case kSaveMenu_Left: case kSaveMenu_PageUp: MoveList(&g_om.fsel, n, -8); break;
    case kSaveMenu_Right: case kSaveMenu_PageDown: MoveList(&g_om.fsel, n, 8); break;
    case kSaveMenu_Ok:
      if (g_om.fsel == 0)
        ApplyShader(NULL);
      else
        EnterShaderFolder();
      break;
    case kSaveMenu_Back: g_om.picker = 0; break;
    }
  } else {
    ShaderFolder *f = &g_om.folders[g_om.fsel - 1];
    switch (action) {
    case kSaveMenu_Up: MoveList(&g_om.psel, f->count, -1); break;
    case kSaveMenu_Down: MoveList(&g_om.psel, f->count, 1); break;
    case kSaveMenu_Left: case kSaveMenu_PageUp: MoveList(&g_om.psel, f->count, -8); break;
    case kSaveMenu_Right: case kSaveMenu_PageDown: MoveList(&g_om.psel, f->count, 8); break;
    case kSaveMenu_Ok: ApplyShader(g_om.files[f->first + g_om.psel]); break;
    case kSaveMenu_Back: g_om.picker = 1; break;
    }
  }
  return true;
}

bool OptMenu_Input(int page, int action) {
  if (g_om.capturing)
    return true;
  if (page == kOptPage_Options && g_om.picker)
    return PickerInput(action);
  if (IsHeader(page, g_om.sel[page]))
    MoveSel(page, 1);
  int i = g_om.sel[page];

  switch (action) {
  case kSaveMenu_Up: MoveSel(page, -1); return true;
  case kSaveMenu_Down: MoveSel(page, 1); return true;
  case kSaveMenu_PageUp: for (int k = 0; k < 5; k++) MoveSel(page, -1); return true;
  case kSaveMenu_PageDown: for (int k = 0; k < 5; k++) MoveSel(page, 1); return true;
  case kSaveMenu_Back: return false;
  }

  if (page == kOptPage_Options) {
    const OptDef *o = &kOptions[i];
    if (action == kSaveMenu_Left || action == kSaveMenu_Right) {
      if (o->type == kT_Shader)
        OpenShaderPicker();
      else
        ChangeOption(o->id, action == kSaveMenu_Left ? -1 : 1);
    } else if (action == kSaveMenu_Ok) {
      if (o->type == kT_Shader)
        OpenShaderPicker();
      else if (o->type == kT_Toggle || o->type == kT_Choice)
        ChangeOption(o->id, 0);
    }
  } else {
    const CtlDef *ct = &kControls[i];
    if (action == kSaveMenu_Left || action == kSaveMenu_Right) {
      g_om.col ^= 1;
    } else if (action == kSaveMenu_Ok) {
      StartCapture(ct->cmd, g_om.col == 1);
    } else if (action == kSaveMenu_Clear) {
      if (g_om.col == 0)
        Config_SetKeyBinding(ct->cmd, 0);
      else
        Config_SetPadBinding(ct->cmd, kGamepadBtn_Invalid, 0);
      SetMsg("Sin asignar: %s", ct->label);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Word wraps |text| into at most |max_lines| lines.
static void DrawWrapped(Canvas *c, int x, int y, int w, const char *text, uint32 col, int max_lines) {
  char line[256];
  const char *p = text;
  for (int ln = 0; ln < max_lines && *p; ln++) {
    size_t best = 0;
    const char *q = p;
    while (*q) {
      const char *e = q;
      while (*e == ' ') e++;
      while (*e && *e != ' ') e++;
      size_t len = e - p;
      if (len >= sizeof(line))
        break;
      memcpy(line, p, len);
      line[len] = 0;
      if (MeasureText(line) > w && best)
        break;
      best = len;
      q = e;
    }
    if (!best)
      best = strlen(p) < sizeof(line) - 1 ? strlen(p) : sizeof(line) - 1;
    memcpy(line, p, best);
    line[best] = 0;
    bool last = ln == max_lines - 1;
    DrawText(c, x, y + ln * 13, line, col, last ? w : -1);
    p += best;
    while (*p == ' ') p++;
    if (last)
      break;
  }
}

static void DrawArrow(Canvas *c, int x, int y, int dir, uint32 col) {
  // Small triangle 4 px wide, 7 px high pointing left (dir < 0) or right.
  for (int k = 0; k < 4; k++) {
    int h = 7 - 2 * k;
    int xx = dir < 0 ? x + 3 - k : x + k;
    FillRect(c, xx, y + k, 1, h, col, 256);
  }
}

static void DrawScrollMarks(Canvas *c, int x, int top, int bottom, bool up, bool down) {
  if (up)
    for (int k = 0; k < 4; k++) FillRect(c, x - k, top - 4 + k, 2 * k + 1, 1, kCol_Gold, 256);
  if (down)
    for (int k = 0; k < 4; k++) FillRect(c, x - k, bottom + 3 - k, 2 * k + 1, 1, kCol_Gold, 256);
}

static void FixScroll(int *scroll, int sel, int rows, int n) {
  if (sel < *scroll + 1) *scroll = sel - 1;
  if (sel > *scroll + rows - 2) *scroll = sel - rows + 2;
  if (*scroll > n - rows) *scroll = n - rows;
  if (*scroll < 0) *scroll = 0;
}

static void DrawRowBg(Canvas *c, int x, int y, int w, int h, bool selected) {
  if (selected) {
    FillRect(c, x, y, w, h, kCol_Select, 220);
    DrawFrame(c, x, y, w, h, kCol_Gold);
  } else {
    FillRect(c, x, y, w, h, kCol_Panel, 120);
  }
}

static void DrawHeaderRow(Canvas *c, int x, int y, int w, const char *label) {
  DrawText(c, x + 2, y + 1, label, kCol_Gold, w - 4);
  int tw = MeasureText(label) + 8;
  if (tw < w)
    FillRect(c, x + tw, y + 8, w - tw, 1, kCol_DarkGold, 256);
}

static bool HasMsg() {
  return g_om.msg_until && (int32)(g_om.msg_until - SDL_GetTicks()) > 0;
}

static void DrawHelp(Canvas *c, int x, int y, int w, const char *help) {
  FillRect(c, x, y - 3, w, 1, kCol_DarkGold, 256);
  if (HasMsg())
    DrawWrapped(c, x + 2, y, w - 4, g_om.msg, kCol_Gold, 2);
  else if (help)
    DrawWrapped(c, x + 2, y, w - 4, help, kCol_Gray, 2);
}

static void DrawPicker(Canvas *c, int x, int w, int top, int bottom) {
  const char *cur = CurrentShaderRel();
  char title[128], buf[160];
  int n;
  if (g_om.picker == 1) {
    snprintf(title, sizeof(title), "Shader: elegí una carpeta");
    n = g_om.num_folders + 1;
  } else {
    ShaderFolder *f = &g_om.folders[g_om.fsel - 1];
    snprintf(title, sizeof(title), "Shader: %s", f->name);
    n = f->count;
  }
  DrawText(c, x + 2, top, title, kCol_Gold, w - 4);
  int list_top = top + 16;
  int help_y = bottom - 28;
  int rows = (help_y - 6 - list_top) / 14;
  if (rows < 1) rows = 1;
  int *sel = g_om.picker == 1 ? &g_om.fsel : &g_om.psel;
  int *scroll = g_om.picker == 1 ? &g_om.fscroll : &g_om.pscroll;
  FixScroll(scroll, *sel, rows, n);
  for (int r = 0; r < rows && *scroll + r < n; r++) {
    int i = *scroll + r, y = list_top + r * 14;
    bool is_cur = false;
    if (g_om.picker == 1) {
      if (i == 0) {
        snprintf(buf, sizeof(buf), "Ninguno (sin shader)");
        is_cur = cur == NULL;
      } else {
        ShaderFolder *f = &g_om.folders[i - 1];
        snprintf(buf, sizeof(buf), "%s  (%d)", f->name, f->count);
        for (int j = f->first; cur && j < f->first + f->count; j++)
          is_cur |= SamePath(g_om.files[j], cur);
      }
    } else {
      ShaderFolder *f = &g_om.folders[g_om.fsel - 1];
      const char *rel = g_om.files[f->first + i];
      const char *shown = rel;
      size_t fl = strlen(f->name);
      if (strncmp(rel, f->name, fl) == 0 && rel[fl] == '/')
        shown = rel + fl + 1;
      size_t len = strlen(shown);
      if (HasSuffixNoCase(shown, ".glslp")) len -= 6;
      snprintf(buf, sizeof(buf), "%.*s", (int)len, shown);
      is_cur = cur && SamePath(rel, cur);
    }
    DrawRowBg(c, x, y, w, 13, i == *sel);
    if (is_cur)
      DrawText(c, x + 3, y, "·", kCol_Gold, -1);
    DrawText(c, x + 10, y, buf, i == *sel ? kCol_Gold : kCol_White, w - 14);
  }
  DrawScrollMarks(c, x + w / 2, list_top - 1, list_top + rows * 14, *scroll > 0, *scroll + rows < n);
  DrawHelp(c, x, help_y, w, g_om.picker == 1 ?
      "A: abrir carpeta (o quitar el shader). B: volver. El punto marca el shader actual." :
      "A: aplicar este shader (se ve al instante). B: volver a las carpetas.");
}

static void DrawOptions(Canvas *c, int x, int w, int top, int bottom) {
  if (g_om.picker) {
    DrawPicker(c, x, w, top, bottom);
    return;
  }
  if (IsHeader(kOptPage_Options, g_om.sel[0]))
    MoveSel(kOptPage_Options, 1);
  int help_y = bottom - 28;
  int rows = (help_y - 4 - top) / kRowH;
  int n = countof(kOptions);
  FixScroll(&g_om.scroll[0], g_om.sel[0], rows, n);
  int value_w = w * 4 / 10;
  if (value_w > 150) value_w = 150;
  int vx = x + w - value_w;
  char buf[128];

  for (int r = 0; r < rows && g_om.scroll[0] + r < n; r++) {
    int i = g_om.scroll[0] + r, y = top + r * kRowH;
    const OptDef *o = &kOptions[i];
    if (o->type == kT_Header) {
      DrawHeaderRow(c, x, y + 1, w, o->label);
      continue;
    }
    bool sel = i == g_om.sel[0];
    DrawRowBg(c, x, y, w, kRowH - 1, sel);
    DrawText(c, x + 5, y, o->label, sel ? kCol_Gold : kCol_White, vx - x - 10);

    if (o->type == kT_Shader) {
      ShaderDisplayName(g_config.shader, buf, sizeof(buf));
      if (!IsOpenGL())
        snprintf(buf, sizeof(buf), "Requiere OpenGL");
      DrawText(c, vx, y, buf, IsOpenGL() ? kCol_White : kCol_Gray, value_w - 6);
    } else if (o->type == kT_Slider || o->type == kT_Choice) {
      int lo, hi, step;
      SliderRange(o->id, &lo, &hi, &step);
      int v = GetValue(o->id);
      FormatValue(o->id, buf, sizeof(buf));
      bool is_vol = o->id == kO_Volume || o->id == kO_Music || o->id == kO_Sfx;
      if (is_vol) {
        int bar_w = value_w - 8 - 6 - MeasureText("100%") - 10;
        if (bar_w < 20) bar_w = 20;
        int bx = vx + 8, by = y + 5;
        FillRect(c, bx, by, bar_w, 4, kCol_Black, 200);
        FillRect(c, bx, by, bar_w * (v - lo) / (hi - lo), 4, sel ? kCol_Gold : kCol_Gray, 256);
        DrawText(c, bx + bar_w + 6, y, buf, kCol_White, -1);
        if (sel) {
          DrawArrow(c, vx + 1, y + 4, -1, kCol_Gold);
          DrawArrow(c, bx + bar_w + 6 + MeasureText(buf) + 3, y + 4, 1, kCol_Gold);
        }
      } else {
        int tw = MeasureText(buf);
        DrawText(c, vx + 8, y, buf, kCol_White, -1);
        if (sel) {
          DrawArrow(c, vx + 1, y + 4, -1, kCol_Gold);
          DrawArrow(c, vx + 8 + tw + 3, y + 4, 1, kCol_Gold);
        }
      }
    } else {
      bool on = GetValue(o->id) != 0;
      int bx = vx + 8, by = y + 2;
      // On/off switch
      FillRect(c, bx, by, 20, 9, on ? 0x2f8f4f : 0x403848, 256);
      FillRect(c, on ? bx + 11 : bx + 1, by + 1, 8, 7, kCol_White, 256);
      DrawText(c, bx + 25, y, on ? "Sí" : "No", on ? kCol_White : kCol_Gray, -1);
    }
  }
  DrawScrollMarks(c, x + w / 2, top - 1, top + rows * kRowH, g_om.scroll[0] > 0, g_om.scroll[0] + rows < n);
  DrawHelp(c, x, help_y, w, kOptions[g_om.sel[0]].help);
}

static void DrawControls(Canvas *c, int x, int w, int top, int bottom) {
  if (IsHeader(kOptPage_Controls, g_om.sel[1]))
    MoveSel(kOptPage_Controls, 1);
  int help_y = bottom - 28;
  int col_w = (w * 3 / 10);
  int kx = x + w - 2 * col_w, px = x + w - col_w;
  DrawText(c, kx + 4, top, "Teclado", g_om.col == 0 ? kCol_Gold : kCol_Gray, col_w - 6);
  DrawText(c, px + 4, top, "Joystick", g_om.col == 1 ? kCol_Gold : kCol_Gray, col_w - 6);
  int list_top = top + 15;
  int rows = (help_y - 4 - list_top) / kCtlRowH;
  int n = countof(kControls);
  FixScroll(&g_om.scroll[1], g_om.sel[1], rows, n);
  char buf[128];

  for (int r = 0; r < rows && g_om.scroll[1] + r < n; r++) {
    int i = g_om.scroll[1] + r, y = list_top + r * kCtlRowH;
    const CtlDef *ct = &kControls[i];
    if (ct->header) {
      DrawHeaderRow(c, x, y, w, ct->label);
      continue;
    }
    bool sel = i == g_om.sel[1];
    FillRect(c, x, y, w, kCtlRowH - 1, sel ? kCol_Select : kCol_Panel, sel ? 200 : 120);
    DrawText(c, x + 5, y, ct->label, sel ? kCol_Gold : kCol_White, kx - x - 8);
    for (int col = 0; col < 2; col++) {
      int cx = col ? px : kx;
      bool waiting = g_om.capturing && sel && g_om.cap_pad == col;
      if (waiting) {
        snprintf(buf, sizeof(buf), "...");
      } else if (col == 0) {
        Config_KeyName(Config_GetKeyBinding(ct->cmd), buf, sizeof(buf));
      } else {
        uint32 mods;
        int b = Config_GetPadBinding(ct->cmd, &mods);
        Config_PadName(b, mods, buf, sizeof(buf));
      }
      if (!buf[0])
        snprintf(buf, sizeof(buf), "-");
      DrawText(c, cx + 4, y, buf, buf[0] == '-' && !buf[1] ? kCol_Gray : kCol_White, col_w - 8);
      if (sel && g_om.col == col)
        DrawFrame(c, cx, y, col_w, kCtlRowH - 1, kCol_Gold);
    }
  }
  DrawScrollMarks(c, x + (kx - x) / 2, list_top - 1, list_top + rows * kCtlRowH, g_om.scroll[1] > 0, g_om.scroll[1] + rows < n);

  if (g_om.capturing) {
    const char *l1 = g_om.cap_pad ? "Apretá el botón o la combinación" : "Apretá la tecla nueva";
    const char *l2 = g_om.cap_pad ? "(por ejemplo View+RB). Una tecla cancela." :
                                    "(puede llevar Shift, Ctrl o Alt). Un botón cancela.";
    int bw = MeasureText(l2) + 20;
    if (MeasureText(l1) + 20 > bw) bw = MeasureText(l1) + 20;
    if (bw > c->w - 10) bw = c->w - 10;
    int bh = 50, bx = (c->w - bw) / 2, by = (top + help_y) / 2 - bh / 2;
    FillRect(c, bx, by, bw, bh, kCol_Panel, 245);
    DrawFrame(c, bx, by, bw, bh, kCol_Gold);
    snprintf(buf, sizeof(buf), "%s", kControls[g_om.sel[1]].label);
    DrawText(c, bx + 10, by + 4, buf, kCol_Gold, bw - 20);
    DrawText(c, bx + 10, by + 19, l1, kCol_White, bw - 20);
    DrawText(c, bx + 10, by + 32, l2, kCol_Gray, bw - 20);
    int left = (kCaptureTimeoutMs - (int)(SDL_GetTicks() - g_om.cap_start)) * (bw - 2) / kCaptureTimeoutMs;
    if (left > 0)
      FillRect(c, bx + 1, by + bh - 3, left, 2, kCol_Gold, 256);
  }
  DrawHelp(c, x, help_y, w, g_om.col == 0 ?
      "A: asignar tecla. X / Supr: dejar sin tecla. Izquierda/derecha: teclado o joystick." :
      "A: asignar botón o combinación. X / Supr: dejar sin botón. Izquierda/derecha: teclado o joystick.");
}

void OptMenu_Draw(int page, Canvas *c, int top, int bottom) {
  int x = 10, w = c->w - 20;
  if (page == kOptPage_Options)
    DrawOptions(c, x, w, top, bottom);
  else
    DrawControls(c, x, w, top, bottom);
}

const char *OptMenu_Hint(int page, bool wide) {
  if (page == kOptPage_Options && g_om.picker)
    return wide ? "A / Enter: Elegir     B / Esc: Volver" : "A: Elegir   B: Volver";
  if (page == kOptPage_Options)
    return wide ? "Izq./Der.: Cambiar   A: Elegir   B: Cerrar   LB/RB: Pestaña" : "Izq/Der: Cambiar  B: Cerrar";
  return wide ? "A: Asignar   X: Borrar   B: Cerrar   LB/RB: Pestaña" : "A: Asignar  X: Borrar  B: Cerrar";
}
