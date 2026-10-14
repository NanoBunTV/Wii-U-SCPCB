# Feasibility study (2026-10-13)

**Verdict : feasible.** Nothing found makes this impossible, and the prerequisites are better than expected,
because both the game's source and the engine's source are public. It is still a large project : the bulk of the
work is a new Wii U back end for the Blitz3D engine (GX2 renderer especially) plus a BlitzBasic → C++
transpiler. The main unknowns are CPU performance and memory on real hardware, and both can be measured early
(milestone M2 in STATUS.md) before most of the work is spent.

## What exists

| Piece | Source | Licence | State |
|---|---|---|---|
| Game, v1.3.11 release (exe + 320 MB assets) | undertowgames.com | CC BY-SA 3.0 (check `Credits.txt` per asset) | Downloaded to `_ref/game/` |
| Game source, v1.3.11 (`VersionNumber$ = "1.3.11"`, 52k lines BlitzBasic) | [Regalis11/scpcb](https://github.com/Regalis11/scpcb) | CC BY-SA 3.0 | Cloned to `_ref/scpcb-src/` |
| Decompiled exe (B3DDecomp) | [juanjp600/B3DDecomp](https://github.com/juanjp600/B3DDecomp) | see repo | Built and run : `_ref/decomp/scpcb_decomp/` (Main.bb, Globals, 327 functions, types) |
| Blitz3D engine + compiler source | [blitz-research/blitz3d](https://github.com/blitz-research/blitz3d) | zlib/libpng | Cloned to `_ref/blitz3d-runtime/` |
| Wii U toolchain | devkitPPC r50, wut 1.9.1, wut-tools, wiiu-sdl2 | — | Already installed in `F:\Programs\devkitPro` |

### Decompiler vs official source

B3DDecomp works on this exe (with the game's `fmod.decls` passed in, as its README says) and produces
recompilable BlitzBasic. But local variables and type fields lose their names (`local1\Field15`), and comments
are gone. The official source has the same function set : all 327 functions in the exe exist in it, and the
58 extra functions in the source are unreferenced (the Blitz compiler drops them). So the **official source
is the better transpiler input**; the decompiler is a cross-check and gave us the exact builtin inventory.

## What has to be written

| # | Component | Size (estimate) | Difficulty | Notes |
|---|---|---|---|---|
| 1 | **BlitzBasic → C++ transpiler** (PC tool) | 3-5k lines | Medium | Reuse the Blitz3D compiler's tokenizer / parser / semantic pass (`compiler/`, ~4.3k lines, AST in `*node.cpp`) and add a C++ emitter in place of `codegen_x86`. This keeps Blitz's exact typing rules (implicit int/float/string conversions, int division, `And`/`Or` being bitwise...). Game uses `Goto` (16×), `Select` (247×), `Data`/`Read`/`Restore`, typed lists (`Each`, `Before`, `After`, `Insert`), `Handle`/`Object`, 95 `Dim` arrays. All map to C++ cleanly |
| 2 | **Blitz support library** (`bb::`) | 2-3k lines | Low-medium | Strings, typed object lists, arrays, banks, file streams. The 44 compiler intrinsics in RUNTIME_API.md. File/bank reads must stay **little-endian** on big-endian PPC (RMesh, saves, INI, banks are all parsed with `ReadInt`/`ReadFloat`/`PeekInt`) |
| 3 | **Port the Blitz3D scene graph** (`blitz3d/`, ~10.5k lines) | Modify | Medium | Entities, cameras, meshes, animation, picking, ellipsoid collision, B3D / X / 3DS loaders. Only 2 files touch Direct3D. Fix endianness in the loaders, MSVC 6 / x86-isms, 64-bit-free but must check alignment |
| 4 | **Wii U graphics back end** (replaces `gxruntime` graphics) | 4-6k lines | **High** | GX2. Must emulate DirectX 7 fixed function with shaders : multitexture blend modes (lightmaps), vertex colours, per-vertex lighting (up to 8 lights), linear fog, alpha / additive / multiply blending, sprites, cube maps, render-to-texture (security cameras, `CameraProjMode`, `CopyRect`), plus the 2D layer (`DrawImage`, `Text`, `Rect`, `Line`, pixel access on textures). Shaders must be precompiled (no runtime GLSL on Wii U unless using CafeGLSL) |
| 5 | Audio back end | ~1k lines | Low-medium | 895 OGG files. stb_vorbis + AX or SDL2 audio. Blitz `PlaySound`/`Channel*`/`Load3DSound` with distance/pan, and FMOD 3 `FSOUND_Stream_*` for music |
| 6 | Input | ~0.5k lines | Low (design work) | Game is mouse + keyboard. GamePad sticks → move / mouse-look, buttons → keys, **GamePad touchscreen → mouse** for menus and inventory (good fit). Pro Controller support. Software keyboard (`nn::swkbd`) for the console / save names |
| 7 | Files, fonts, images, misc | ~1k lines | Low | SD card or wuhb content paths, case-insensitive lookup (game mixes case), FreeType for TTF fonts, stb_image for PNG/JPG/BMP, stub Win32 DLL calls, drop updater / launcher / TCP |
| 8 | Game-side adaptation | — | Medium | Options for 1280×720 TV + 854×480 GamePad, control remapping, saves location, performance fixes found by profiling |

Rough total : **20-30k lines** of new or adapted C++, plus the transpiled game. For one developer working with an
AI assistant this is months of work, not weeks. Milestones in STATUS.md are ordered so that the hardest risks are
tested first.

## Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| **CPU too slow** : Espresso is 3 × PowerPC 750 at 1.24 GHz; SCP:CB is single-threaded and its update loop (NPCs, events, collisions, `UpdateRooms`) is heavy | Medium | High | Measure early (M2/M5). Run audio / streaming on other cores. Profile; the Blitz collision code and per-frame `For Each` scans are the usual hot spots. Target 30 fps |
| **Memory** : SCP:CB on PC often uses 0.6-1.2 GB; a Wii U app gets roughly ≤ 1 GB | Medium | High | Convert textures offline to GX2-native compressed formats (BC1/BC3) with mipmaps : 4-8× smaller. Lazy-load room meshes. Measure on hardware |
| GX2 renderer bugs / fixed-function differences | High (expected) | Medium | Lots of visual iteration, compare screenshots against the PC game |
| Cemu differs from hardware | Medium | Medium | Cemu for iteration, hardware for sign-off. Cemu's homebrew support is less tested than its game support |
| Transpiler semantic mismatches (Blitz quirks) | Medium | Medium | Reusing the original compiler front end avoids most. Blitz's `Rand`/`SeedRnd` algorithm is in `bbruntime/bbmath.cpp`; copy it so map seeds give the same maps |
| Blitz3D 1.108 features missing from the open-source runtime (`texturebumpenv*`, `texturelodbias`, `errorlog`) | Certain | Low | Stubs |
| Asset licensing for distribution | Low | Medium | See PROJECT.md "Open questions" |

## Why not the alternatives

- **Run the x86 exe in an emulator / translator** : no x86 emulation that fast exists on Wii U, and it would also
  need DirectX 7 and Win32. Not "native" either.
- **Interpret BlitzBasic on the console** : simpler toolchain but too slow on Espresso.
- **Port a remake** (Unity, Unreal, C++/OpenGL recreations) : different games, and none of them are written
  for a platform without OpenGL / Vulkan. The Wii U has neither, so any route needs a GX2 renderer anyway.
- **Rewrite the game in C++ by hand** : 52k lines; transpiling keeps behaviour identical and can be redone when
  something in the source changes.
- **SDL2's Wii U port alone** : it only renders 2D (no OpenGL on Wii U), so it can help with audio, input and
  window setup, but not 3D.
