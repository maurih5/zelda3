#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "stats.h"
#include "menu_draw.h"
#include "zelda_rtl.h"
#include "variables.h"
#include "config.h"
#include "features.h"
#include "savemenu.h"

static const char kStatsFile[] = "saves/estadisticas.ini";

typedef struct FileStats {
  uint64 ms;  // play time
  uint32 hurt, enemies;
} FileStats;

static FileStats g_files[3];
static bool g_dirty;
static uint32 g_last_tick, g_last_write, g_session_start;

// ---------------------------------------------------------------------------
// Stats file
// ---------------------------------------------------------------------------

static void LoadFile() {
  FILE *f = fopen(kStatsFile, "r");
  if (!f)
    return;
  char line[256];
  int cur = -1;
  while (fgets(line, sizeof(line), f)) {
    int n;
    unsigned long long v;
    if (sscanf(line, "[Partida%d]", &n) == 1)
      cur = (n >= 1 && n <= 3) ? n - 1 : -1;
    else if (cur >= 0 && sscanf(line, "Segundos = %llu", &v) == 1)
      g_files[cur].ms = v * 1000;
    else if (cur >= 0 && sscanf(line, "Golpes = %llu", &v) == 1)
      g_files[cur].hurt = (uint32)v;
    else if (cur >= 0 && sscanf(line, "Enemigos = %llu", &v) == 1)
      g_files[cur].enemies = (uint32)v;
  }
  fclose(f);
}

static void WriteFile() {
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%s.tmp", kStatsFile);
  FILE *f = fopen(tmp, "w");
  if (!f)
    return;
  fprintf(f, "# Estadisticas de cada partida (las escribe el juego).\n");
  for (int i = 0; i < 3; i++) {
    fprintf(f, "\n[Partida%d]\nSegundos = %llu\nGolpes = %u\nEnemigos = %u\n", i + 1,
            (unsigned long long)(g_files[i].ms / 1000), g_files[i].hurt, g_files[i].enemies);
  }
  bool ok = fclose(f) == 0;
  if (ok) {
    remove(kStatsFile);
    rename(tmp, kStatsFile);
  }
  g_dirty = false;
}

void Stats_Init() {
  LoadFile();
  g_last_tick = g_last_write = g_session_start = SDL_GetTicks();
  Stats_Resync();
}

void Stats_Shutdown() {
  if (g_dirty)
    WriteFile();
}

int Stats_CurrentFile() {
  uint8 m = main_module_index;
  if (m < 5 || m == 0x14)  // intro, file select, attract mode
    return -1;
  int v = srm_var1;
  return (v == 2 || v == 4 || v == 6) ? v / 2 - 1 : -1;
}

uint32 Stats_PlaySeconds(int slot) {
  return slot >= 0 && slot < 3 ? (uint32)(g_files[slot].ms / 1000) : 0;
}

void Stats_FormatTime(uint32 s, char *buf, size_t n) {
  if (s < 60)
    snprintf(buf, n, "%u s", s);
  else if (s >= 3600)
    snprintf(buf, n, "%u h %02u min", s / 3600, s / 60 % 60);
  else
    snprintf(buf, n, "%u min", s / 60);
}

void Stats_OnHurt() {
  int s = Stats_CurrentFile();
  if (s >= 0 && !ZeldaIsReplaying())
    g_files[s].hurt++, g_dirty = true;
}

void Stats_OnEnemyDefeated() {
  int s = Stats_CurrentFile();
  if (s >= 0 && !ZeldaIsReplaying())
    g_files[s].enemies++, g_dirty = true;
}

void Stats_OnFileCreated(int slot) {
  if (slot < 0 || slot > 2)
    return;
  memset(&g_files[slot], 0, sizeof(g_files[slot]));
  WriteFile();
}

void Stats_OnFileCopied(int from, int to) {
  if (from < 0 || from > 2 || to < 0 || to > 2)
    return;
  g_files[to] = g_files[from];
  WriteFile();
}

void Stats_OnFileErased(int slot) {
  Stats_OnFileCreated(slot);
}

// ---------------------------------------------------------------------------
// Progress of the save file being played (read from the game RAM)
// ---------------------------------------------------------------------------

typedef struct ItemDef {
  uint16 addr;
  const char *name;
} ItemDef;

static const ItemDef kItems[] = {
  { 0xF340, "Arco" }, { 0xF341, "Bumerán" }, { 0xF342, "Gancho" }, { 0xF344, "Hongo Mágico" },
  { 0xF345, "Vara de Fuego" }, { 0xF346, "Vara de Hielo" }, { 0xF347, "Medallón Bombos" },
  { 0xF348, "Medallón Éter" }, { 0xF349, "Medallón Terremoto" }, { 0xF34A, "Lámpara" },
  { 0xF34B, "Martillo Mágico" }, { 0xF34C, "Pala" }, { 0xF34D, "Red para Bichos" },
  { 0xF34E, "Libro de Mudora" }, { 0xF350, "Bastón de Somaria" }, { 0xF351, "Bastón de Byrna" },
  { 0xF352, "Capa Mágica" }, { 0xF353, "Espejo Mágico" }, { 0xF354, "Guantes de Poder" },
  { 0xF355, "Botas de Pegaso" }, { 0xF356, "Aletas de Zora" }, { 0xF357, "Perla Lunar" },
};
enum { kNumItems = sizeof(kItems) / sizeof(kItems[0]) };

static int PopCount(uint32 v) {
  int n = 0;
  for (; v; v &= v - 1)
    n++;
  return n;
}

static int NumBottles() {
  int n = 0;
  for (int i = 0; i < 4; i++)
    n += link_bottle_info[i] != 0;
  return n;
}

static int NumItems() {
  int n = 0;
  for (int i = 0; i < kNumItems; i++)
    n += g_ram[kItems[i].addr] != 0;
  return n;
}

static int SwordLevel() {
  return link_sword_type <= 4 ? link_sword_type : 0;
}

static int Deaths() {
  // The game keeps them per dungeon, plus the ones since the last dungeon
  // was finished (same as the count shown in the ending).
  int sum = death_save_counter;
  if (death_var2 != 0xffff)
    return sum + death_var2;
  for (int i = 0; i < 14; i++)
    sum += deaths_per_palace[i];
  return sum;
}

static int MaxRupees() {
  return enhanced_features0 & kFeatures0_CarryMoreRupees ? 9999 : 999;
}

typedef struct AchDef {
  const char *name, *desc;
} AchDef;

enum {
  kAch_Sword, kAch_Pendant, kAch_Pendants, kAch_MasterSword, kAch_Crystal, kAch_Crystals,
  kAch_Hearts10, kAch_Hearts20, kAch_Bottles, kAch_Medallions, kAch_GoldSword,
  kAch_MirrorShield, kAch_RedMail, kAch_TitanMitt, kAch_Rupees, kAch_AllItems, kAch_Hero,
  kAch_Count,
};

static const AchDef kAch[kAch_Count] = {
  { "Primera espada", "Conseguí una espada" },
  { "Primer colgante", "Conseguí un colgante" },
  { "Los tres colgantes", "Conseguí los tres colgantes" },
  { "La Espada Maestra", "Sacá la Espada Maestra" },
  { "Primer cristal", "Rescatá a una doncella" },
  { "Los siete cristales", "Rescatá a las siete doncellas" },
  { "Diez corazones", "Llegá a 10 corazones" },
  { "Veinte corazones", "Llegá a 20 corazones" },
  { "Cuatro botellas", "Conseguí las cuatro botellas" },
  { "Los tres medallones", "Bombos, Éter y Terremoto" },
  { "Espada Dorada", "Mejorá la espada al máximo" },
  { "Escudo Espejo", "Conseguí el mejor escudo" },
  { "Túnica Roja", "Conseguí la mejor armadura" },
  { "Guantes de Titán", "Conseguí los guantes más fuertes" },
  { "Billetera llena", "Juntá todas las rupias que entran" },
  { "Todos los objetos", "Los 22 objetos del inventario" },
  { "Héroe de Hyrule", "Terminá el juego" },
};

static bool AchDone(int i) {
  switch (i) {
  case kAch_Sword: return SwordLevel() >= 1;
  case kAch_Pendant: return (link_which_pendants & 7) != 0;
  case kAch_Pendants: return (link_which_pendants & 7) == 7;
  case kAch_MasterSword: return SwordLevel() >= 2;
  case kAch_Crystal: return (link_has_crystals & 0x7f) != 0;
  case kAch_Crystals: return (link_has_crystals & 0x7f) == 0x7f;
  case kAch_Hearts10: return link_health_capacity >= 80;
  case kAch_Hearts20: return link_health_capacity >= 160;
  case kAch_Bottles: return NumBottles() == 4;
  case kAch_Medallions: return link_item_bombos_medallion && link_item_ether_medallion && link_item_quake_medallion;
  case kAch_GoldSword: return SwordLevel() == 4;
  case kAch_MirrorShield: return link_shield_type >= 3;
  case kAch_RedMail: return link_armor >= 2;
  case kAch_TitanMitt: return link_item_gloves >= 2;
  case kAch_Rupees: return link_rupees_actual >= MaxRupees();
  case kAch_AllItems: return NumItems() == kNumItems;
  case kAch_Hero: return death_var2 != 0xffff;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Notices of things that were just obtained
// ---------------------------------------------------------------------------

typedef struct Snapshot {
  bool valid;
  int file;
  uint8 items[kNumItems];
  uint8 sword, shield, armor, bottles, pendants, crystals, capacity, pieces;
  bool ach[kAch_Count];
} Snapshot;

static Snapshot g_snap;
static char g_queue[6][96];
static int g_queue_n;

void Stats_Resync() {
  g_snap.valid = false;
}

static void Notify(const char *fmt, ...) {
  if (!g_config.achievement_toasts || g_queue_n >= 6)
    return;
  va_list va;
  va_start(va, fmt);
  vsnprintf(g_queue[g_queue_n++], sizeof(g_queue[0]), fmt, va);
  va_end(va);
#ifdef SAVEMENU_SELFTEST
  printf("NOTICE %s\n", g_queue[g_queue_n - 1]);
#endif
}

static void TakeSnapshot(Snapshot *s, int file) {
  s->valid = true;
  s->file = file;
  for (int i = 0; i < kNumItems; i++)
    s->items[i] = g_ram[kItems[i].addr];
  s->sword = SwordLevel();
  s->shield = link_shield_type;
  s->armor = link_armor;
  s->bottles = NumBottles();
  s->pendants = link_which_pendants & 7;
  s->crystals = link_has_crystals & 0x7f;
  s->capacity = link_health_capacity;
  s->pieces = link_heart_pieces;
  for (int i = 0; i < kAch_Count; i++)
    s->ach[i] = AchDone(i);
}

static const char *UpgradeName(int i, uint8 old, uint8 v) {
  uint16 a = kItems[i].addr;
  if (a == 0xF340 && old < 3 && v >= 3) return "Flechas de Plata";
  if (a == 0xF341 && v == 2) return "Bumerán Mágico";
  if (a == 0xF344 && v == 2) return "Polvo Mágico";
  if (a == 0xF34C && old < 2 && v >= 2) return "Flauta";
  if (a == 0xF354 && v == 2) return "Guantes de Titán";
  return NULL;
}

static void CompareSnapshots(const Snapshot *o, const Snapshot *n) {
  static const char *const kSwords[5] = { "", "Espada del Guerrero", "Espada Maestra", "Espada Templada", "Espada Dorada" };
  static const char *const kShields[4] = { "", "Escudo", "Escudo de Fuego", "Escudo Espejo" };
  static const char *const kArmors[3] = { "", "Túnica Azul", "Túnica Roja" };
  for (int i = 0; i < kNumItems; i++) {
    if (n->items[i] == o->items[i])
      continue;
    const char *up = UpgradeName(i, o->items[i], n->items[i]);
    if (!o->items[i])
      Notify("Objeto nuevo: %s", kItems[i].name);
    else if (up && n->items[i] > o->items[i])
      Notify("Objeto nuevo: %s", up);
  }
  if (n->sword > o->sword)
    Notify("¡%s!", kSwords[n->sword]);
  if (n->shield > o->shield && n->shield <= 3)
    Notify("Objeto nuevo: %s", kShields[n->shield]);
  if (n->armor > o->armor && n->armor <= 2)
    Notify("Objeto nuevo: %s", kArmors[n->armor]);
  if (n->bottles > o->bottles)
    Notify("Botella nueva (%d de 4)", n->bottles);
  if (n->capacity > o->capacity)
    Notify("¡Corazón nuevo! (%d de 20)", n->capacity / 8);
  else if (n->pieces > o->pieces)
    Notify("Pieza de corazón (%d de 4)", n->pieces);
  if (n->pendants != o->pendants && PopCount(n->pendants) > PopCount(o->pendants))
    Notify("Colgante conseguido (%d de 3)", PopCount(n->pendants));
  if (n->crystals != o->crystals && PopCount(n->crystals) > PopCount(o->crystals))
    Notify("Cristal conseguido (%d de 7)", PopCount(n->crystals));
  for (int i = 0; i < kAch_Count; i++)
    if (n->ach[i] && !o->ach[i])
      Notify("¡Logro! %s", kAch[i].name);
}

void Stats_OnFrame() {
  uint32 now = SDL_GetTicks();
  uint32 dt = now - g_last_tick;
  g_last_tick = now;
  int file = Stats_CurrentFile();
  if (file < 0 || ZeldaIsReplaying()) {
    if (file < 0)
      g_snap.valid = false;
  } else {
    // Long gaps (menu open, pause, window dragged) don't count.
    if (dt <= 250) {
      g_files[file].ms += dt;
      g_dirty = true;
    }
    uint8 m = main_module_index;
    if (m == 7 || m == 9 || m == 14) {
      Snapshot cur;
      TakeSnapshot(&cur, file);
      if (g_snap.valid && g_snap.file == file)
        CompareSnapshots(&g_snap, &cur);
      g_snap = cur;
    }
  }
  if (g_queue_n && !SaveMenu_ToastActive()) {
    SaveMenu_ShowToast(g_queue[0]);
    memmove(g_queue[0], g_queue[1], sizeof(g_queue[0]) * --g_queue_n);
  }
  if (g_dirty && now - g_last_write >= 60000) {
    g_last_write = now;
    WriteFile();
  }
}

// ---------------------------------------------------------------------------
// "Progreso" tab
// ---------------------------------------------------------------------------

enum { kRowH = 14 };

typedef struct Row {
  uint8 kind;  // 0 = header, 1 = value, 2 = achievement
  bool done;
  char label[64], value[64];
} Row;

static int g_scroll;

static int BuildRows(Row *rows, int max) {
  int n = 0;
#define ADD(k, d, l, ...) do { if (n < max) { Row *r = &rows[n++]; r->kind = k; r->done = d; \
    snprintf(r->label, sizeof(r->label), "%s", l); snprintf(r->value, sizeof(r->value), __VA_ARGS__); } } while (0)
  int file = Stats_CurrentFile();
  char t[48];
  uint32 session = (SDL_GetTicks() - g_session_start) / 1000;
  if (file < 0) {
    ADD(0, false, "Progreso", "%s", "");
    ADD(1, false, "Elegí una partida para ver su progreso", "%s", "");
    Stats_FormatTime(session, t, sizeof(t));
    ADD(1, false, "Esta sesión", "%s", t);
    for (int i = 0; i < 3; i++) {
      char l[32];
      snprintf(l, sizeof(l), "Tiempo jugado, partida %d", i + 1);
      Stats_FormatTime(Stats_PlaySeconds(i), t, sizeof(t));
      ADD(1, false, l, "%s", t);
    }
    return n;
  }
  char h[48];
  snprintf(h, sizeof(h), "Partida %d", file + 1);
  ADD(0, false, h, "%s", "");
  Stats_FormatTime(Stats_PlaySeconds(file), t, sizeof(t));
  ADD(1, false, "Tiempo jugado", "%s", t);
  Stats_FormatTime(session, t, sizeof(t));
  ADD(1, false, "Esta sesión", "%s", t);
  ADD(1, false, "Muertes", "%d", Deaths());
  ADD(1, false, "Golpes recibidos", "%u", g_files[file].hurt);
  ADD(1, false, "Enemigos derrotados", "%u", g_files[file].enemies);
  ADD(1, false, "Corazones", "%d de 20 (+%d/4 piezas)", link_health_capacity / 8, link_heart_pieces & 3);
  ADD(1, false, "Colgantes", "%d de 3", PopCount(link_which_pendants & 7));
  ADD(1, false, "Cristales", "%d de 7", PopCount(link_has_crystals & 0x7f));
  ADD(1, false, "Objetos", "%d de %d", NumItems(), kNumItems);
  ADD(1, false, "Botellas", "%d de 4", NumBottles());
  ADD(1, false, "Rupias", "%d", link_rupees_actual);
  int done = 0;
  for (int i = 0; i < kAch_Count; i++)
    done += AchDone(i);
  snprintf(h, sizeof(h), "Logros (%d de %d)", done, kAch_Count);
  ADD(0, false, h, "%s", "");
  for (int i = 0; i < kAch_Count; i++)
    ADD(2, AchDone(i), kAch[i].name, "%s", kAch[i].desc);
#undef ADD
  return n;
}

bool Stats_MenuInput(int action) {
  switch (action) {
  case kSaveMenu_Up: g_scroll--; break;
  case kSaveMenu_Down: g_scroll++; break;
  case kSaveMenu_PageUp: case kSaveMenu_Left: g_scroll -= 8; break;
  case kSaveMenu_PageDown: case kSaveMenu_Right: g_scroll += 8; break;
  case kSaveMenu_Back: return false;
  }
  if (g_scroll < 0)
    g_scroll = 0;
  return true;
}

void Stats_MenuDraw(Canvas *c, int top, int bottom) {
  Row rows[48];
  int n = BuildRows(rows, 48);
  int x = 10, w = c->w - 20;
  int visible = (bottom - top - 2) / kRowH;
  if (g_scroll > n - visible)
    g_scroll = n - visible;
  if (g_scroll < 0)
    g_scroll = 0;
  int vx = x + w / 2;
  for (int r = 0; r < visible && g_scroll + r < n; r++) {
    const Row *row = &rows[g_scroll + r];
    int y = top + r * kRowH;
    if (row->kind == 0) {
      DrawText(c, x + 2, y + 1, row->label, kCol_Gold, w - 4);
      FillRect(c, x + 4 + MeasureText(row->label), y + 8, w - 8 - MeasureText(row->label), 1, kCol_DarkGold, 256);
    } else if (row->kind == 1) {
      DrawText(c, x + 8, y, row->label, kCol_White, vx - x - 12);
      DrawText(c, vx, y, row->value, kCol_White, x + w - vx);
    } else {
      // Achievement: a lit or empty box, the name and how to get it.
      FillRect(c, x + 8, y + 4, 7, 7, row->done ? kCol_Gold : kCol_Black, row->done ? 256 : 160);
      DrawFrame(c, x + 8, y + 4, 7, 7, row->done ? kCol_Gold : kCol_Gray);
      DrawText(c, x + 20, y, row->label, row->done ? kCol_Gold : kCol_Gray, vx - x - 24);
      DrawText(c, vx, y, row->value, row->done ? kCol_White : kCol_Gray, x + w - vx);
    }
  }
  // Scroll marks
  if (g_scroll > 0)
    for (int k = 0; k < 4; k++) FillRect(c, x + w / 2 - k, top - 4 + k, 2 * k + 1, 1, kCol_Gold, 256);
  if (g_scroll + visible < n) {
    int yb = top + visible * kRowH;
    for (int k = 0; k < 4; k++) FillRect(c, x + w / 2 - k, yb + 3 - k, 2 * k + 1, 1, kCol_Gold, 256);
  }
}
