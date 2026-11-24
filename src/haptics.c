#include <SDL.h>
#include "haptics.h"
#include "zelda_rtl.h"

// Strength (0-100) and duration (ms) of one rumble, plus the minimum time
// before the same event may rumble again.
typedef struct HapticPattern {
  uint8 strength;
  uint16 ms, cooldown_ms;
} HapticPattern;

static int g_haptic_intensity = 70;
#ifdef SAVEMENU_SELFTEST
int g_st_haptic_boots[4];
#endif
static uint32 g_haptic_last[16];

void Haptics_Configure(int intensity) {
  g_haptic_intensity = intensity < 0 ? 0 : intensity > 100 ? 100 : intensity;
}

static bool PatternFor(int type, int arg, HapticPattern *p) {
  static const HapticPattern kSwing = { 35, 60, 0 }, kSpin = { 70, 120, 300 };
  static const HapticPattern kHit = { 50, 80, 60 }, kClink = { 25, 60, 100 };
  static const HapticPattern kDamageLow = { 40, 100, 200 }, kDamageMed = { 70, 150, 200 }, kDamageHigh = { 100, 200, 200 };
  static const HapticPattern kHookshotWall = { 40, 60, 100 }, kBoomerang = { 25, 40, 100 };
  static const HapticPattern kEnv[8] = {
    { 60, 300, 500 },   // fall into pit
    { 50, 100, 200 },   // land from ledge
    { 40, 150, 300 },   // chest
    { 90, 200, 100 },   // bomb explosion
    { 40, 150, 300 },   // enter deep water
    { 50, 300, 500 },   // mirror warp
    { 100, 600, 600 },  // quake
    { 100, 800, 1000 }, // boss defeated
  };
  switch (type) {
  case kHaptic_SwordSwing: *p = arg ? kSpin : kSwing; return true;
  case kHaptic_SwordHitEnemy: *p = kHit; return true;
  case kHaptic_SwordClink: *p = kClink; return true;
  case kHaptic_DamageTaken: *p = arg >= 40 ? kDamageHigh : arg >= 24 ? kDamageMed : kDamageLow; return true;
  case kHaptic_HookshotWall: *p = kHookshotWall; return true;
  case kHaptic_BoomerangCatch: *p = kBoomerang; return true;
  case kHaptic_Boots: {
    static const HapticPattern kBoots[4] = {
      { 22, 45, 0 },     // charge
      { 60, 140, 0 },    // launch
      { 14, 40, 0 },     // running steps
      { 90, 220, 300 },  // bonk
    };
    if ((unsigned)arg >= 4)
      return false;
    *p = kBoots[arg];
    return true;
  }
  case kHaptic_Environment:
    if (arg < 0 || arg >= 8) return false;
    *p = kEnv[arg];
    return true;
  case kHaptic_ItemUsed:
    switch (arg) {
    case 1: *p = (HapticPattern){ 30, 60, 100 }; return true;     // bombs
    case 3: *p = (HapticPattern){ 30, 50, 100 }; return true;     // bow
    case 4: *p = (HapticPattern){ 80, 120, 200 }; return true;    // hammer
    case 5: case 6: *p = (HapticPattern){ 40, 100, 200 }; return true;  // fire / ice rod
    case 13: case 18: *p = (HapticPattern){ 40, 80, 200 }; return true; // canes
    case 14: *p = (HapticPattern){ 35, 80, 200 }; return true;    // hookshot
    case 15: case 16: case 17: *p = (HapticPattern){ 100, 600, 1000 }; return true;  // medallions
    case 19: *p = (HapticPattern){ 30, 80, 300 }; return true;    // cape
    case 21: *p = (HapticPattern){ 50, 80, 200 }; return true;    // shovel
    }
    return false;
  }
  return false;
}

void Haptic_Event(int type, int arg) {
  // Replays and fast-forwarded replays re-run the game code; they shouldn't shake.
  if (g_haptic_intensity <= 0 || ZeldaIsReplaying())
    return;
  HapticPattern p;
  if (!PatternFor(type, arg, &p))
    return;
#ifdef SAVEMENU_SELFTEST
  if (type == kHaptic_Boots)
    g_st_haptic_boots[arg]++;
#endif
  uint32 now = SDL_GetTicks();
  if (type >= 0 && type < 16) {
    if (p.cooldown_ms && g_haptic_last[type] && now - g_haptic_last[type] < p.cooldown_ms)
      return;
    g_haptic_last[type] = now ? now : 1;
  }
  float s = p.strength * g_haptic_intensity * (1.0f / 10000);
  Uint16 low = (Uint16)(s * 0xffff), high = (Uint16)(s * 0.6f * 0xffff);
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (!SDL_IsGameController(i))
      continue;
    SDL_GameController *gc = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
    if (gc)
      SDL_GameControllerRumble(gc, low, high, p.ms);
  }
}
