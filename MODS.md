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
