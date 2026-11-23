# Changes in this fork

This fork of [snesrev/zelda3](https://github.com/snesrev/zelda3) adds a few
quality-of-life features. The in-game menus are in Spanish.

## Rewind
Hold **Backspace** (keyboard) or **View+LT** (gamepad) to rewind, RetroArch
style. States are stored as compressed XOR deltas (a few KB each). The in-game
save (SRAM) is never rolled back.

```ini
[General]
RewindSeconds = 30        # 0 disables rewind
RewindFrameInterval = 2   # capture a state every N frames

[KeyMap]
Rewind = Backspace
[GamepadMap]
Rewind = Back+L2
```

## Autosave states and save menu
Save states are written automatically to `saves/auto/` every few minutes of
play, when entering or leaving a dungeon, on quit, and before loading another
state, each with a thumbnail. **Escape** / **View+Start** opens a menu over the
game to choose which autosave or manual slot to load. Loading keeps the in-game
save (SRAM).

```ini
[General]
AutosaveMinutes = 5   # 0 = only on quit / dungeon changes
AutosaveSlots = 10    # 0 disables autosave

[KeyMap]
SaveMenu = Escape
[GamepadMap]
SaveMenu = Back+Start
```

## Options and controls menu
The same menu has **Saves / Options / Controls** tabs (LB/RB or Tab):

- **Options**: master / music (MSU) / effects volume, a shader picker for the
  `glsl-shaders` folder that applies instantly (OpenGL output only), renderer
  toggles, all `[Features]`, and rewind / autosave settings.
- **Controls**: rebind keyboard keys and gamepad button combos while playing.

Changes are applied immediately and saved to `zelda3.opciones.ini`, which is
read after `zelda3.ini`, so your `zelda3.ini` is never rewritten.
`VolumeUp` / `VolumeDown` change the in-game master volume.

## Building on Windows without MSBuild
`build_msvc.bat [out.exe] [extra defines]` builds with the Visual Studio Build
Tools. It expects the SDL2 development package (VC) extracted to
`third_party/SDL2-2.26.5`.

## Fixes for upstream issues
- [#328](https://github.com/snesrev/zelda3/issues/328): bow, rods, hammer and
  Cane of Somaria could lock Link in an animation loop (out-of-bounds read).
- [#331](https://github.com/snesrev/zelda3/issues/331): analog stick overflow
  with the stick fully in a corner.
- [#295](https://github.com/snesrev/zelda3/issues/295): cheats have no keys by
  default.
- [#261](https://github.com/snesrev/zelda3/issues/261): new `Quit` command to
  close the game from a key or gamepad combo.
- Rewind now restores the PPU registers, so the background rewinds too.

## Ideas from Relic of the Past
Some features of the [Relic of the Past](https://github.com/drizztdourden08/relic-of-the-past)
launcher (MIT), reimplemented natively. All of them are in the in-game Options tab.

- **Edge effect**: in widescreen, the black side bars outside the current room or
  area are filled with a blurred, darkened mirror of the scene.
- **Rumble**: the gamepad vibrates on sword swings and hits, damage taken, items,
  bombs, chests, pits, ledges, deep water, mirror warps and defeated bosses.
- **Fast dialog**: hold A to type text at double speed; B writes the rest of the
  box at once (without also advancing it).
- **Widescreen HUD**: magic, item and counters at the left edge, life meter
  at the right edge, instead of squeezed in the middle 256 px
  ([#322](https://github.com/snesrev/zelda3/issues/322), [#142](https://github.com/snesrev/zelda3/issues/142)).
- **Widescreen iris**: the iris/spotlight circle uses its real width instead of
  being cut flat at the sides of the 256 px picture, and the opening circle keeps
  growing until it covers the whole widescreen picture.
- **Turbo speed**, **stick dead zone** and a **3:2** aspect ratio
  ([#311](https://github.com/snesrev/zelda3/issues/311)).

```ini
[Graphics]
EdgeEffect = 1
WideHud = 1
[General]
Rumble = 70           # 0-100, 0 = off
StickDeadzone = 30    # percent
TurboSpeed = 0        # 2-16 = that many times faster, 0 = as fast as possible
ExtendedAspectRatio = 3:2
[Features]
FastDialog = 1
```
