#ifndef ZELDA3_HAPTICS_H_
#define ZELDA3_HAPTICS_H_

#include "types.h"

// Gamepad rumble for game events. The event list and the strengths follow the
// ones used by the "Relic of the Past" launcher (MIT).

enum {
  kHaptic_SwordSwing,      // arg: 0 = swing, 1 = spin attack
  kHaptic_SwordHitEnemy,   // arg: damage dealt
  kHaptic_SwordClink,
  kHaptic_DamageTaken,     // arg: damage in 1/8 hearts
  kHaptic_ItemUsed,        // arg: item id (current_item_active)
  kHaptic_Environment,     // arg: kHapticEnv_*
  kHaptic_HookshotWall,
  kHaptic_BoomerangCatch,
  kHaptic_Boots,           // arg: kHapticBoots_*
};

enum {
  kHapticBoots_Charge,     // revving up (every few frames)
  kHapticBoots_Launch,     // starts running
  kHapticBoots_Step,       // running (every few frames)
  kHapticBoots_Bonk,       // ran into a wall or a sprite
};

enum {
  kHapticEnv_FallIntoPit,
  kHapticEnv_LandFromLedge,
  kHapticEnv_ChestOpen,
  kHapticEnv_BombExplode,
  kHapticEnv_EnterWater,
  kHapticEnv_MirrorWarp,
  kHapticEnv_Quake,
  kHapticEnv_BossDefeated,
};

// intensity: 0-100 (0 turns rumble off)
void Haptics_Configure(int intensity);
// Called from the game code.
void Haptic_Event(int type, int arg);

#endif  // ZELDA3_HAPTICS_H_
