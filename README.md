# Illuminated Anima - SKSE plugin

Copyright (C) 2026 izzydoingit. GPL-3.0-or-later, see `LICENSE`.

The plugin of Illuminated Anima, an all-in-one lighting mod for creatures: no plugin file, no scripts, nothing written into the save.

## What it does

- Creatures glow in their own light: atronachs, wisps, spriggans, ice wraiths, ash spawn, chaurus, familiars, the eyes of the undead,
  dwarven machines and the creatures of the mods its creatures files name. Each light rides the creature's own bones.
- The spells creatures use and their explosions are lit through the game's own light records.
- Finds the lighting by itself (Community Shaders with or without Light Placer, ENB, Vanilla) and makes each light for it.
- Where another creature lighting mod lights a creature, ours steps aside and its light is never touched; the menu warns which mod
  replaces ours on which creatures, and a switch per mod puts ours back on top.
- It works from the game's events and a character's two 3D slots. A light's flicker, death fade and cast surge are keys the game's own
  dimmer controllers play, so no code of ours runs per frame.

## Requires

SKSE and Address Library for SKSE Plugins; SKSE Menu Framework for the menu. Skyrim VR: SKSE VR and the VR version of Address Library,
with the VR build (a build on the wrong game stays off and says so in a box). The VR build is compiled, never played by the author.

## Settings

SKSE Menu Framework > Illuminated Anima, saved to `Data\SKSE\Plugins\IlluminatedAnima\Settings.ini`.

## Building

`build.bat` (git, xmake 3 and the Visual Studio C++ tools): CommonLib at the pinned commit, the DLL, then the off-game tests
(`tests/testanima.cpp`, the game-free part in `src/AnimaMath.h`). For Skyrim VR: `git -C lib\commonlibsse-ng submodule update --init
extern/openvr`, then `build.bat` with `GLOW_VR=1` set (a VR-only build).

## Credits

The way a light is made and registered follows ReLight's, used with Truman's permission. The `Character::Load3D` hook follows Light
Placer's (powerof3, GPL-3.0, with permission). The game's light dimmer was read off CommonLibSSE-NG #477 and Baked Goods by ItzIvy05
(MIT / GPL-3.0). Light spots and relative colors after iLluminous Elementals (RogueUnicorn00); Darenii's creatures after Darenii's
Spells - Creatures - CS Lights Addon (ShreditTES, with permission).

## Third-party files

`src/SKSEMenuFramework.h` is SKSE Menu Framework's own header by Thiago Kaique, copied unchanged from
[SKSE-Menu-Framework-3-Example](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-Example), MIT licensed
(see `src/SKSEMenuFramework.LICENSE.txt`).

`src/DevBenchAPI.h` and `src/DevBenchAPI.cpp` are devbench's own interface files, MIT licensed by
their author (see `src/DevBenchAPI.LICENSE.txt`) and copied unchanged. devbench is not required.
