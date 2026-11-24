#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "types.h"
#include "variables.h"
#include "zelda_rtl.h"
#include "savemenu.h"
#include "menu_draw.h"
#include "optmenu.h"

enum {
  kKind_Auto = 0,
  kKind_Area = 1,
  kKind_Quit = 2,
  kKind_BeforeLoad = 3,
  kKind_Manual = 4,
  kNumManualSlots = 10,
  kMaxAutoSlots = 50,
  kMaxEntries = kNumManualSlots + kMaxAutoSlots,
  kLocLen = 48,
};

static const char kThumbMagic[4] = { 'Z', '3', 'T', 'H' };

typedef struct ThumbHeader {
  char magic[4];
  uint32 version;
  uint32 w, h;
  int64 time;
  uint8 kind, hearts, max_hearts, pad;
  char loc[kLocLen];
} ThumbHeader;

typedef struct MenuEntry {
  char sav[64];
  int slot;  // manual slot number or auto slot number
  ThumbHeader hdr;
  bool has_hdr;
  uint32 *thumb;
} MenuEntry;

static struct {
  int autosave_frames;  // 0 = no periodic autosave
  int autosave_slots;   // 0 = autosave disabled
  int frames_since_autosave;
  int last_area_key;
  bool area_changed;

  uint32 *last_frame;
  int lf_w, lf_h;

  char toast[96];
  int toast_frames;

  bool open;
  int tab;
  MenuEntry ents[kMaxEntries];
  int num_ents, sel, scroll;
  // "Guardar" tab: the manual slots, empty ones with hdr.time == 0.
  MenuEntry slots[kNumManualSlots];
  int slot_sel, slot_scroll;
  int confirm;  // slot waiting for "overwrite?" confirmation, -1 = none
  uint32 last_gamepad;
} g_sm = { .last_area_key = -1 };

// ---------------------------------------------------------------------------
// Game info
// ---------------------------------------------------------------------------

static bool InGameplay() {
  uint8 m = main_module_index;
  return m == 7 || m == 9;
}

static bool SafeToAutosave() {
  return !ZeldaIsReplaying() && InGameplay() && submodule_index == 0;
}

static int AreaKey() {
  uint8 pal = (uint8)cur_palace_index_x2;
  if (player_is_indoors && pal < 28)
    return pal;
  return 0x100 | (savegame_is_darkworld ? 1 : 0);
}

static void GetLocationName(char *buf, size_t n) {
  static const char *const kDungeons[14] = {
    "Alcantarillas del Castillo", "Castillo de Hyrule", "Palacio del Este", "Palacio del Desierto",
    "Torre de Agahnim", "Palacio del Pantano", "Palacio de la Oscuridad", "Ciénaga del Desaliento",
    "Bosque Calavera", "Palacio de Hielo", "Torre de Hera", "Villa de los Ladrones",
    "Roca Tortuga", "Torre de Ganon",
  };
  static const struct { uint8 area; const char *name; } kAreas[] = {
    {0x00, "Bosque Perdido"}, {0x03, "Montaña de la Muerte"}, {0x05, "Montaña de la Muerte"},
    {0x07, "Montaña de la Muerte"}, {0x18, "Aldea Kakariko"}, {0x1b, "Castillo de Hyrule"},
    {0x1e, "Palacio del Este"}, {0x2c, "Casa de Link"}, {0x30, "Desierto"}, {0x35, "Lago Hylia"},
    {0x40, "Bosque Calavera"}, {0x43, "Montaña Oscura"}, {0x45, "Montaña Oscura"},
    {0x47, "Montaña Oscura"}, {0x58, "Villa de los Ladrones"}, {0x5b, "Pirámide"},
    {0x5e, "Palacio de la Oscuridad"}, {0x70, "Ciénaga del Desaliento"}, {0x75, "Lago de Hielo"},
    {0x80, "Bosque Sagrado"}, {0x81, "Cascada de Zora"},
  };
  if (!InGameplay()) {
    snprintf(buf, n, "%s", main_module_index < 6 ? "Pantalla de inicio" : "");
    return;
  }
  const char *world = savegame_is_darkworld ? "Mundo Oscuro" : "Mundo de la Luz";
  uint8 pal = (uint8)cur_palace_index_x2;
  if (player_is_indoors) {
    if (pal < 28)
      snprintf(buf, n, "%s", kDungeons[pal >> 1]);
    else
      snprintf(buf, n, "Interior · %s", world);
    return;
  }
  uint16 area = overworld_area_index;
  for (size_t i = 0; i < countof(kAreas); i++) {
    if (kAreas[i].area == area) {
      snprintf(buf, n, "%s", kAreas[i].name);
      return;
    }
  }
  snprintf(buf, n, "%s", world);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

static void MakeDir(const char *name) {
#ifdef _WIN32
  _mkdir(name);
#else
  mkdir(name, 0755);
#endif
}

static void ThumbPathFor(char *dst, size_t n, const char *sav) {
  snprintf(dst, n, "%s", sav);
  char *dot = strrchr(dst, '.');
  if (dot && (size_t)(dot - dst) + 4 < n)
    strcpy(dot, ".thm");
}

static bool FileExists(const char *path, int64 *mtime) {
  struct stat st;
  if (stat(path, &st) != 0)
    return false;
  if (mtime)
    *mtime = (int64)st.st_mtime;
  return true;
}

static bool ReadThumbHeader(const char *sav, ThumbHeader *hdr, uint32 **pixels) {
  char thm[80];
  ThumbPathFor(thm, sizeof(thm), sav);
  FILE *f = fopen(thm, "rb");
  if (!f)
    return false;
  bool ok = fread(hdr, 1, sizeof(*hdr), f) == sizeof(*hdr) &&
            memcmp(hdr->magic, kThumbMagic, 4) == 0 && hdr->version == 1 &&
            hdr->w <= 512 && hdr->h <= 512;
  if (ok) {
    hdr->loc[kLocLen - 1] = 0;
    if (pixels && hdr->w && hdr->h) {
      size_t n = (size_t)hdr->w * hdr->h;
      *pixels = malloc(n * 4);
      if (*pixels && fread(*pixels, 4, n, f) != n) {
        free(*pixels);
        *pixels = NULL;
      }
    }
  }
  fclose(f);
  return ok;
}

static void WriteThumb(const char *sav, int kind) {
  ThumbHeader hdr;
  memset(&hdr, 0, sizeof(hdr));
  memcpy(hdr.magic, kThumbMagic, 4);
  hdr.version = 1;
  hdr.time = (int64)time(NULL);
  hdr.kind = kind;
  hdr.hearts = link_health_current;
  hdr.max_hearts = link_health_capacity;
  if (!InGameplay())
    hdr.hearts = hdr.max_hearts = 0;
  GetLocationName(hdr.loc, sizeof(hdr.loc));

  uint32 *pix = NULL;
  if (g_sm.last_frame) {
    hdr.w = g_sm.lf_w / 2;
    hdr.h = g_sm.lf_h / 2;
    pix = malloc((size_t)hdr.w * hdr.h * 4);
    if (pix) {
      for (uint32 y = 0; y < hdr.h; y++) {
        const uint32 *r0 = g_sm.last_frame + (size_t)(y * 2) * g_sm.lf_w;
        const uint32 *r1 = r0 + g_sm.lf_w;
        for (uint32 x = 0; x < hdr.w; x++) {
          uint32 a = r0[x * 2], b = r0[x * 2 + 1], c = r1[x * 2], d = r1[x * 2 + 1];
          uint32 rb = ((a & 0xff00ff) + (b & 0xff00ff) + (c & 0xff00ff) + (d & 0xff00ff)) >> 2;
          uint32 g = ((a & 0xff00) + (b & 0xff00) + (c & 0xff00) + (d & 0xff00)) >> 2;
          pix[y * hdr.w + x] = (rb & 0xff00ff) | (g & 0xff00);
        }
      }
    } else {
      hdr.w = hdr.h = 0;
    }
  }
  char thm[80];
  ThumbPathFor(thm, sizeof(thm), sav);
  FILE *f = fopen(thm, "wb");
  if (f) {
    fwrite(&hdr, 1, sizeof(hdr), f);
    if (pix)
      fwrite(pix, 4, (size_t)hdr.w * hdr.h, f);
    fclose(f);
  }
  free(pix);
}

static void AutoSlotPath(char *dst, size_t n, int slot) {
  snprintf(dst, n, "saves/auto/auto%02d.sav", slot);
}

static int64 SaveTime(const char *sav) {
  ThumbHeader hdr;
  int64 t = 0;
  if (!FileExists(sav, &t))
    return -1;
  if (ReadThumbHeader(sav, &hdr, NULL))
    t = hdr.time;
  return t;
}

// Picks the auto slot to overwrite: an empty one, otherwise the oldest.
static int PickAutoSlot(const char *exclude) {
  int best = 0;
  int64 best_t = INT64_MAX;
  for (int i = 0; i < g_sm.autosave_slots; i++) {
    char p[64];
    AutoSlotPath(p, sizeof(p), i);
    if (exclude && strcmp(p, exclude) == 0)
      continue;
    int64 t = SaveTime(p);
    if (t < 0)
      return i;
    if (t < best_t)
      best_t = t, best = i;
  }
  return best;
}

static void ShowToast(const char *msg) {
  snprintf(g_sm.toast, sizeof(g_sm.toast), "%s", msg);
  g_sm.toast_frames = 150;
}

void SaveMenu_ShowToast(const char *msg) {
  ShowToast(msg);
}

static bool g_kbd_hints;

void SaveMenu_SetKeyboardHints(bool keyboard) {
  g_kbd_hints = keyboard;
}

static char g_progress_msg[96];
static int g_progress = -1;

void SaveMenu_SetProgress(const char *msg, int permille) {
  if (!msg) {
    g_progress = -1;
    return;
  }
  snprintf(g_progress_msg, sizeof(g_progress_msg), "%s", msg);
  g_progress = permille < 0 ? 0 : permille > 1000 ? 1000 : permille;
}

static bool DoAutosave(int kind, const char *exclude, bool toast) {
  if (g_sm.autosave_slots <= 0 || ZeldaIsReplaying())
    return false;
  MakeDir("saves");
  MakeDir("saves/auto");
  char p[64];
  AutoSlotPath(p, sizeof(p), PickAutoSlot(exclude));
  if (!ZeldaSaveStateFile(p)) {
    fprintf(stderr, "Autosave: no se pudo escribir %s\n", p);
    return false;
  }
  WriteThumb(p, kind);
  printf("Autosave: %s\n", p);
#ifdef SAVEMENU_SELFTEST
  {
    extern void SelftestRecordSave(const char *path);
    SelftestRecordSave(p);
  }
#endif
  g_sm.frames_since_autosave = 0;
  g_sm.area_changed = false;
  if (toast)
    ShowToast("Autoguardado");
  return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SaveMenu_Init(int autosave_minutes, int autosave_slots) {
  if (autosave_slots < 0) autosave_slots = 0;
  if (autosave_slots > kMaxAutoSlots) autosave_slots = kMaxAutoSlots;
  if (autosave_minutes < 0) autosave_minutes = 0;
  if (autosave_minutes > 600) autosave_minutes = 600;
  g_sm.autosave_slots = autosave_slots;
  g_sm.autosave_frames = autosave_minutes * 60 * 60;
}

#ifdef SAVEMENU_SELFTEST
void SaveMenu_SelftestSetInterval(int frames) { g_sm.autosave_frames = frames; }
const char *SaveMenu_SelftestSelectedPath() {
  return (g_sm.open && g_sm.sel < g_sm.num_ents) ? g_sm.ents[g_sm.sel].sav : "";
}
int SaveMenu_SelftestNumEntries() { return g_sm.num_ents; }
#endif

void SaveMenu_CaptureFrame(const uint8 *pixels, int pitch, int scale, int width, int height) {
  if (!g_sm.last_frame || g_sm.lf_w != width || g_sm.lf_h != height) {
    free(g_sm.last_frame);
    g_sm.last_frame = malloc((size_t)width * height * 4);
    g_sm.lf_w = width, g_sm.lf_h = height;
    if (!g_sm.last_frame)
      return;
  }
  for (int y = 0; y < height; y++) {
    const uint32 *src = (const uint32 *)(pixels + (size_t)y * scale * pitch);
    uint32 *dst = g_sm.last_frame + (size_t)y * width;
    if (scale == 1) {
      memcpy(dst, src, width * 4);
    } else {
      for (int x = 0; x < width; x++)
        dst[x] = src[x * scale];
    }
  }
}

void SaveMenu_DrawOverlay(uint8 *pixels, int pitch, int scale, int width, int height) {
  if (g_progress >= 0) {
    Canvas c = { pixels, pitch, scale, width, height };
    int tw = MeasureText(g_progress_msg);
    int bw = tw + 16, bh = MENU_FONT_HEIGHT + 12;
    int x = width - bw - 8, y = height - bh - 8;
    FillRect(&c, x, y, bw, bh, kCol_Panel, 224);
    DrawFrame(&c, x, y, bw, bh, kCol_Gold);
    DrawText(&c, x + 8, y + 3, g_progress_msg, kCol_White, -1);
    int barw = bw - 16;
    FillRect(&c, x + 8, y + bh - 7, barw, 3, kCol_HeartEmpty, 256);
    FillRect(&c, x + 8, y + bh - 7, barw * g_progress / 1000, 3, kCol_Gold, 256);
    return;
  }
  if (g_sm.toast_frames <= 0)
    return;
  g_sm.toast_frames--;
  Canvas c = { pixels, pitch, scale, width, height };
  int alpha = g_sm.toast_frames >= 32 ? 256 : g_sm.toast_frames * 8;
  int tw = MeasureText(g_sm.toast);
  int bw = tw + 16, bh = MENU_FONT_HEIGHT + 6;
  int x = width - bw - 8, y = height - bh - 8;
  FillRect(&c, x, y, bw, bh, kCol_Panel, alpha * 7 / 8);
  if (alpha == 256) {
    DrawFrame(&c, x, y, bw, bh, kCol_Gold);
    DrawText(&c, x + 8, y + 3, g_sm.toast, kCol_White, -1);
  }
}

void SaveMenu_OnFramePlayed() {
  if (g_sm.autosave_slots <= 0 || ZeldaIsReplaying())
    return;
  g_sm.frames_since_autosave++;
  if (InGameplay()) {
    int key = AreaKey();
    if (g_sm.last_area_key >= 0 && key != g_sm.last_area_key)
      g_sm.area_changed = true;
    g_sm.last_area_key = key;
  }
  if (!SafeToAutosave())
    return;
  if (g_sm.autosave_frames && g_sm.frames_since_autosave >= g_sm.autosave_frames) {
    DoAutosave(kKind_Auto, NULL, true);
  } else if (g_sm.area_changed && g_sm.frames_since_autosave >= 60 * 60) {
    // Entering or leaving a dungeon, or changing worlds.
    DoAutosave(kKind_Area, NULL, true);
  } else if (g_sm.area_changed) {
    g_sm.area_changed = false;
  }
}

void SaveMenu_OnManualSave(int slot) {
  char p[64];
  snprintf(p, sizeof(p), "saves/save%d.sav", slot);
  if (FileExists(p, NULL))
    WriteThumb(p, kKind_Manual);
}

void SaveMenu_OnQuit() {
  if (InGameplay())
    DoAutosave(kKind_Quit, NULL, false);
}

bool SaveMenu_IsOpen() {
  return g_sm.open;
}

static void FreeEntries() {
  for (int i = 0; i < g_sm.num_ents; i++)
    free(g_sm.ents[i].thumb);
  g_sm.num_ents = 0;
}

static bool FillEntry(MenuEntry *e, const char *sav, int slot) {
  int64 mtime;
  memset(e, 0, sizeof(*e));
  if (!FileExists(sav, &mtime))
    return false;
  snprintf(e->sav, sizeof(e->sav), "%s", sav);
  e->slot = slot;
  e->has_hdr = ReadThumbHeader(sav, &e->hdr, &e->thumb);
  if (!e->has_hdr) {
    memset(&e->hdr, 0, sizeof(e->hdr));
    e->hdr.kind = (slot >= 0) ? kKind_Manual : kKind_Auto;
  }
  // The file time is the truth if the thumbnail is from an older save.
  if (!e->has_hdr || mtime > e->hdr.time + 5)
    e->hdr.time = mtime;
  if (strncmp(sav, "saves/save", 10) == 0)
    e->hdr.kind = kKind_Manual;
  return true;
}

static void AddEntry(const char *sav, int slot) {
  if (g_sm.num_ents < kMaxEntries && FillEntry(&g_sm.ents[g_sm.num_ents], sav, slot))
    g_sm.num_ents++;
}

static void FreeSlots() {
  for (int i = 0; i < kNumManualSlots; i++) {
    free(g_sm.slots[i].thumb);
    g_sm.slots[i].thumb = NULL;
  }
}

static void ScanSlots() {
  FreeSlots();
  for (int i = 0; i < kNumManualSlots; i++) {
    char p[64];
    snprintf(p, sizeof(p), "saves/save%d.sav", i);
    if (!FillEntry(&g_sm.slots[i], p, i)) {
      g_sm.slots[i].slot = i;
      g_sm.slots[i].hdr.kind = kKind_Manual;
    }
  }
}

static int CompareEntries(const void *a, const void *b) {
  int64 ta = ((const MenuEntry *)a)->hdr.time, tb = ((const MenuEntry *)b)->hdr.time;
  return ta < tb ? 1 : ta > tb ? -1 : 0;
}

static void ScanEntries() {
  FreeEntries();
  char p[64];
  for (int i = 0; i < kNumManualSlots; i++) {
    snprintf(p, sizeof(p), "saves/save%d.sav", i);
    AddEntry(p, i);
  }
  for (int i = 0; i < kMaxAutoSlots; i++) {
    AutoSlotPath(p, sizeof(p), i);
    AddEntry(p, -1 - i);
  }
  qsort(g_sm.ents, g_sm.num_ents, sizeof(MenuEntry), CompareEntries);
}

void SaveMenu_Toggle() {
  g_sm.open = !g_sm.open;
  g_sm.confirm = -1;
  if (g_sm.open) {
    ScanEntries();
    ScanSlots();
    g_sm.sel = g_sm.scroll = 0;
    g_sm.tab = kMenuTab_Saves;
    OptMenu_Open();
  } else {
    FreeEntries();
    FreeSlots();
  }
}

void SaveMenu_OpenTab(int tab) {
  if (!g_sm.open)
    SaveMenu_Toggle();
  g_sm.tab = tab;
}

int SaveMenu_GetTab() {
  return g_sm.tab;
}

static void EntryLabel(const MenuEntry *e, char *buf, size_t n) {
  switch (e->hdr.kind) {
  case kKind_Manual: snprintf(buf, n, "Ranura %d", e->slot + 1); break;
  case kKind_Quit: snprintf(buf, n, "Al salir del juego"); break;
  case kKind_BeforeLoad: snprintf(buf, n, "Antes de cargar"); break;
  default: snprintf(buf, n, "Autoguardado"); break;
  }
}

static void RelativeTime(int64 t, char *buf, size_t n) {
  int64 d = (int64)time(NULL) - t;
  if (d < 60)
    snprintf(buf, n, "Recién");
  else if (d < 3600)
    snprintf(buf, n, "Hace %d min", (int)(d / 60));
  else if (d < 86400)
    snprintf(buf, n, "Hace %d h", (int)(d / 3600));
  else
    snprintf(buf, n, "Hace %d día%s", (int)(d / 86400), d >= 2 * 86400 ? "s" : "");
}

static void AbsoluteTime(int64 t, char *buf, size_t n) {
  static const char *const kDays[7] = { "Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb" };
  time_t tt = (time_t)t;
  struct tm *tm = localtime(&tt);
  if (!tm) {
    buf[0] = 0;
    return;
  }
  snprintf(buf, n, "%s %02d/%02d  %02d:%02d", kDays[tm->tm_wday % 7], tm->tm_mday, tm->tm_mon + 1,
           tm->tm_hour, tm->tm_min);
}

static int VisibleRows(int height) {
  int rows = (height - 34 - 22) / 30;
  return rows < 1 ? 1 : rows;
}

static void LoadSelected() {
  if (g_sm.sel < 0 || g_sm.sel >= g_sm.num_ents)
    return;
  MenuEntry *e = &g_sm.ents[g_sm.sel];
  char label[48];
  EntryLabel(e, label, sizeof(label));
  // Keep the current progress reachable from the menu, in case this was a mistake.
  if (InGameplay())
    DoAutosave(kKind_BeforeLoad, e->sav, false);
  if (ZeldaLoadStateFile(e->sav, true)) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Cargado: %s", label);
    ShowToast(msg);
    g_sm.frames_since_autosave = 0;
    g_sm.area_changed = false;
    g_sm.last_area_key = -1;
  } else {
    ShowToast("No se pudo cargar ese guardado");
  }
  g_sm.open = false;
  FreeEntries();
  FreeSlots();
}

// Saves the running game to a manual slot (same files as Shift+F1..F10).
static void SaveToSlot(int slot) {
  MakeDir("saves");
  SaveLoadSlot(kSaveLoad_Save, slot);
  SaveMenu_OnManualSave(slot);
  char msg[64];
  snprintf(msg, sizeof(msg), "Guardado en ranura %d", slot + 1);
  ShowToast(msg);
  g_sm.open = false;
  g_sm.confirm = -1;
  FreeEntries();
  FreeSlots();
}

static void SaveTabInput(int action) {
  if (g_sm.confirm >= 0) {
    if (action == kSaveMenu_Ok)
      SaveToSlot(g_sm.confirm);
    else if (action == kSaveMenu_Back)
      g_sm.confirm = -1;
    return;
  }
  int n = kNumManualSlots;
  switch (action) {
  case kSaveMenu_Up: g_sm.slot_sel = (g_sm.slot_sel + n - 1) % n; break;
  case kSaveMenu_Down: g_sm.slot_sel = (g_sm.slot_sel + 1) % n; break;
  case kSaveMenu_Left:
  case kSaveMenu_PageUp: g_sm.slot_sel = g_sm.slot_sel > 4 ? g_sm.slot_sel - 4 : 0; break;
  case kSaveMenu_Right:
  case kSaveMenu_PageDown: g_sm.slot_sel = g_sm.slot_sel + 4 < n ? g_sm.slot_sel + 4 : n - 1; break;
  case kSaveMenu_Ok:
    if (g_sm.slots[g_sm.slot_sel].hdr.time != 0)
      g_sm.confirm = g_sm.slot_sel;  // ask before overwriting
    else
      SaveToSlot(g_sm.slot_sel);
    break;
  case kSaveMenu_Back: SaveMenu_Toggle(); break;
  }
}

void SaveMenu_Input(int action) {
  if (!g_sm.open)
    return;
  if (OptMenu_IsCapturing())
    return;
  if (action == kSaveMenu_PrevTab || action == kSaveMenu_NextTab) {
    int d = action == kSaveMenu_NextTab ? 1 : kMenuTab_Count - 1;
    g_sm.tab = (g_sm.tab + d) % kMenuTab_Count;
    g_sm.confirm = -1;
    OptMenu_Open();
    return;
  }
  if (g_sm.tab == kMenuTab_Save) {
    SaveTabInput(action);
    return;
  }
  if (g_sm.tab != kMenuTab_Saves) {
    if (!OptMenu_Input(g_sm.tab == kMenuTab_Options ? kOptPage_Options : kOptPage_Controls, action))
      SaveMenu_Toggle();
    return;
  }
  int n = g_sm.num_ents;
  switch (action) {
  case kSaveMenu_Up: if (n) g_sm.sel = (g_sm.sel + n - 1) % n; break;
  case kSaveMenu_Down: if (n) g_sm.sel = (g_sm.sel + 1) % n; break;
  case kSaveMenu_Left:
  case kSaveMenu_PageUp: g_sm.sel = g_sm.sel > 4 ? g_sm.sel - 4 : 0; break;
  case kSaveMenu_Right:
  case kSaveMenu_PageDown: if (n) g_sm.sel = g_sm.sel + 4 < n ? g_sm.sel + 4 : n - 1; break;
  case kSaveMenu_Ok: LoadSelected(); break;
  case kSaveMenu_Back: SaveMenu_Toggle(); break;
  }
}

// The list of saves (left) with a preview of the selected one (right).
// |slots_mode|: the "Guardar" tab, where entries can be empty slots.
static void DrawEntries(Canvas *c, MenuEntry *ents, int n, int *sel, int *scroll, bool slots_mode) {
  int width = c->w, height = c->h;
  char buf[96];
  // Layout: list on the left, preview on the right.
  int preview_w = (width - 34) / 2;
  int list_x = 10, list_w = width - 34 - preview_w;
  int prev_x = width - 10 - preview_w;
  int top = 30;
  int rows = VisibleRows(height);
  if (*sel < *scroll) *scroll = *sel;
  if (*sel >= *scroll + rows) *scroll = *sel - rows + 1;

  for (int r = 0; r < rows && *scroll + r < n; r++) {
    int i = *scroll + r;
    MenuEntry *e = &ents[i];
    int y = top + r * 30;
    if (i == *sel) {
      FillRect(c, list_x, y, list_w, 28, kCol_Select, 220);
      DrawFrame(c, list_x, y, list_w, 28, kCol_Gold);
    } else {
      FillRect(c, list_x, y, list_w, 28, kCol_Panel, 150);
    }
    char label[48], rel[48];
    bool empty = e->hdr.time == 0;
    EntryLabel(e, label, sizeof(label));
    if (empty)
      snprintf(rel, sizeof(rel), "Vacía");
    else
      RelativeTime(e->hdr.time, rel, sizeof(rel));
    int rel_w = MeasureText(rel);
    DrawText(c, list_x + list_w - 5 - rel_w, y + 1, rel, kCol_Gray, -1);
    DrawText(c, list_x + 5, y + 1, label, i == *sel ? kCol_Gold : kCol_White, list_w - 16 - rel_w);
    if (empty)
      snprintf(buf, sizeof(buf), "%s", "Ranura libre");
    else if (e->hdr.loc[0])
      snprintf(buf, sizeof(buf), "%s", e->hdr.loc);
    else
      AbsoluteTime(e->hdr.time, buf, sizeof(buf));
    DrawText(c, list_x + 5, y + 14, buf, kCol_Gray, list_w - 10);
  }
  // Scroll indicators
  if (*scroll > 0)
    for (int k = 0; k < 4; k++) FillRect(c, list_x + list_w / 2 - k, 26 + k, 2 * k + 1, 1, kCol_Gold, 256);
  if (*scroll + rows < n) {
    int yb = top + rows * 30 - 1;
    for (int k = 0; k < 4; k++) FillRect(c, list_x + list_w / 2 - k, yb + 3 - k, 2 * k + 1, 1, kCol_Gold, 256);
  }

  // Preview of the selected entry
  MenuEntry *e = &ents[*sel];
  int aspect_w = e->thumb ? (int)e->hdr.w : width, aspect_h = e->thumb ? (int)e->hdr.h : height;
  int preview_h = preview_w * aspect_h / aspect_w;
  if (preview_h > height / 2) preview_h = height / 2;
  FillRect(c, prev_x - 2, top - 2, preview_w + 4, preview_h + 4, kCol_Black, 256);
  if (e->thumb) {
    DrawImage(c, prev_x, top, preview_w, preview_h, e->thumb, e->hdr.w, e->hdr.h);
  } else {
    FillRect(c, prev_x, top, preview_w, preview_h, kCol_Panel, 256);
    const char *msg = e->hdr.time == 0 ? "Ranura vacía" : "Sin imagen";
    DrawText(c, prev_x + (preview_w - MeasureText(msg)) / 2, top + preview_h / 2 - 7, msg, kCol_Gray, -1);
  }
  DrawFrame(c, prev_x - 2, top - 2, preview_w + 4, preview_h + 4, kCol_Gold);

  int y = top + preview_h + 6;
  if (e->hdr.time == 0) {
    if (slots_mode)
      DrawText(c, prev_x, y, "Guardá acá la partida actual", kCol_Gray, preview_w);
    return;
  }
  AbsoluteTime(e->hdr.time, buf, sizeof(buf));
  DrawText(c, prev_x, y, buf, kCol_White, preview_w);
  y += 14;
  if (e->hdr.loc[0]) {
    DrawText(c, prev_x, y, e->hdr.loc, kCol_Gold, preview_w);
    y += 15;
  }
  if (e->hdr.max_hearts && y + 8 < height - 22)
    DrawHearts(c, prev_x, y + 1, e->hdr.hearts, e->hdr.max_hearts, preview_w);
}

void SaveMenu_Draw(uint8 *pixels, int pitch, int width, int height) {
  Canvas c = { pixels, pitch, 1, width, height };

  // Darkened, blue tinted game frame as background.
  for (int y = 0; y < height; y++) {
    uint32 *dst = (uint32 *)(pixels + (size_t)y * pitch);
    const uint32 *src = (g_sm.last_frame && g_sm.lf_w == width && y < g_sm.lf_h) ?
        g_sm.last_frame + (size_t)y * width : NULL;
    for (int x = 0; x < width; x++) {
      uint32 p = src ? src[x] : 0;
      uint32 r = (p >> 16 & 0xff) * 3 / 10 + 6, g = (p >> 8 & 0xff) * 3 / 10 + 8, b = (p & 0xff) * 3 / 10 + 26;
      dst[x] = r << 16 | g << 8 | b;
    }
  }

  // Tabs
  static const char *const kTabNames[kMenuTab_Count] = { "Cargar", "Guardar", "Opciones", "Controles" };
  int tx = 12;
  bool wide = width >= 360;
  bool tab_glyphs = wide && !g_kbd_hints;
  if (tab_glyphs)
    tx += DrawButtonGlyph(&c, tx, 6, "L", false) + 8;
  for (int t = 0; t < kMenuTab_Count; t++) {
    int tw = MeasureText(kTabNames[t]);
    if (t == g_sm.tab) {
      FillRect(&c, tx - 4, 4, tw + 8, 19, kCol_Select, 220);
      FillRect(&c, tx - 4, 21, tw + 8, 2, kCol_Gold, 256);
    }
    DrawText(&c, tx, 6, kTabNames[t], t == g_sm.tab ? kCol_Gold : kCol_Gray, -1);
    tx += tw + 12;
  }
  if (tab_glyphs)
    tx += DrawButtonGlyph(&c, tx - 4, 6, "R", false) + 4;
  char buf[96];
  if (g_sm.tab == kMenuTab_Saves && g_sm.num_ents) {
    snprintf(buf, sizeof(buf), "%d / %d", g_sm.sel + 1, g_sm.num_ents);
    if (width - 12 - MeasureText(buf) > tx + 20)
      DrawText(&c, width - 12 - MeasureText(buf), 6, buf, kCol_Gray, -1);
  }
  FillRect(&c, 10, 23, width - 20, 1, kCol_Gold, 256);
  FillRect(&c, 10, 24, width - 20, 1, kCol_DarkGold, 256);

  // Footer
  FillRect(&c, 10, height - 20, width - 20, 1, kCol_DarkGold, 256);
  const char *hint = g_sm.tab == kMenuTab_Saves ? "LR:Pestaña|B:Volver|A:Cargar" :
      g_sm.tab == kMenuTab_Save ? (g_sm.confirm >= 0 ? "B:No|A:Sí" : "LR:Pestaña|B:Volver|A:Guardar") :
      OptMenu_Hint(g_sm.tab == kMenuTab_Options ? kOptPage_Options : kOptPage_Controls, wide);
  DrawHintBar(&c, 12, width - 12, height - 16, hint, g_kbd_hints);

  if (g_sm.tab == kMenuTab_Save) {
    DrawEntries(&c, g_sm.slots, kNumManualSlots, &g_sm.slot_sel, &g_sm.slot_scroll, true);
    if (g_sm.confirm >= 0) {
      snprintf(buf, sizeof(buf), "¿Sobrescribir la ranura %d?", g_sm.confirm + 1);
      int bw = MeasureText(buf) + 32, bh = 44;
      if (bw < 160) bw = 160;
      int bx = (width - bw) / 2, by = (height - bh) / 2;
      FillRect(&c, bx, by, bw, bh, kCol_Panel, 245);
      DrawFrame(&c, bx, by, bw, bh, kCol_Gold);
      DrawText(&c, bx + (bw - MeasureText(buf)) / 2, by + 6, buf, kCol_White, -1);
      DrawHintBar(&c, bx + 8, bx + bw - 10, by + bh - 18, "B:No|A:Sí", g_kbd_hints);
    }
    return;
  }

  if (g_sm.tab != kMenuTab_Saves) {
    OptMenu_Draw(g_sm.tab == kMenuTab_Options ? kOptPage_Options : kOptPage_Controls, &c, 30, height - 22);
    return;
  }

  if (g_sm.num_ents == 0) {
    const char *msg = "Todavía no hay guardados";
    DrawText(&c, (width - MeasureText(msg)) / 2, height / 2 - 16, msg, kCol_White, -1);
    if (g_sm.autosave_frames) {
      snprintf(buf, sizeof(buf), "El juego se guarda solo cada %d min", g_sm.autosave_frames / 3600);
      DrawText(&c, (width - MeasureText(buf)) / 2, height / 2, buf, kCol_Gray, width - 20);
    }
    return;
  }

  DrawEntries(&c, g_sm.ents, g_sm.num_ents, &g_sm.sel, &g_sm.scroll, false);
}
