# Status, known issues and roadmap

## Current state (2026-10-13)

**The whole game is transpiled, builds and runs in Cemu** : title screen → main menu → New Game → map
generation and loading → in the game (Class-D cell, intro) with the HUD. No audio yet. Not tested on hardware yet.

| Part | State |
|---|---|
| Wii U back end (`runtime/gxruntime`) | Graphics (2D + 3D), input (GamePad), files, timers done. **Audio : placeholder** (silent) |
| Blitz3D scene graph + runtime | Ported, endian-fixed (B3D, streams, banks); new text `.x` loader |
| Transpiler (`tools/bb2cpp`) | Converts all of SCP:CB 1.3.11 (388 functions, ≈ 51 000 lines of C++) |
| Userlib (DLL) functions | 47 used : all stubs (FMOD music, BlitzMovie, Win32, zlib) |
| Textures | Precompressed BC1 / BC3 (`tools/texconv`) |
| Game source changes (`game/src`) | DLL check removed. Settings : `game/options.ini` (launcher, update check, startup video off) |

## Test results

| Date | Build | Where | Result |
|---|---|---|---|
| 2026-10-13 | M1 hello world | Cemu 2.6 | Runs at 60 fps, reads `fs:/vol/content/`, logs via OSReport |
| 2026-10-13 | GX2 spike | Cemu 2.6 | Shaders from CafeGLSL load; orientation, vertex colours, BE indices OK (ARCHITECTURE.md) |
| 2026-10-13 | Room viewer | Cemu 2.6 | SCP-173 room renders correctly (lightmaps, culling, depth), 60 fps, ≈ 12 700 triangles. Auto-tour of 106 rooms : no crash, loads 0.2-1.4 s (Cemu). `room2.rmesh` in the game data is an empty file (shown as nothing) |
| 2026-10-13 | Game | Cemu 2.6 | Title, main menu, New Game screen, map generation, loading, in-game (intro cell) with HUD. ≈ 30-35 fps in Cemu (≈ 115 draws / frame; Cemu speed isn't meaningful). **Memory in use ≈ 650 MB in game** |
| 2026-10-13 | Game (first build) | **Wii U (Aroma)** | **Stuck on the system boot screen** (boot logo + loading music), never showed a frame. No log available. Suspected cause : the game ran on the RPX main thread, which only has a 64 KB stack (the Half-Life port hit the same) |
| 2026-10-13 | Game + big stack, diagnostics | Cemu 2.6 | Still reaches the game. The game now runs on an 8 MB-stack thread; a black frame is shown right after GX2 init; log file, crash screen and hang watchdog added (BUILDING.md "Hardware") |
| — | Game + big stack | **Wii U (Aroma)** | **To test** — if it still fails, send `sd:/wiiu/apps/scpcb/log.txt` (and a photo of any crash screen) |

## Hardware test checklist (next build)

Copy `build/scpcb_game.wuhb` to `sd:/wiiu/apps/`. Please report the overlay line (top-left, yellow) at each step.
If anything goes wrong, send `sd:/wiiu/apps/scpcb/log.txt` (written on every start) and a photo of any crash screen.

1. Does it start? Title screen ("press any key") → GamePad button → main menu (no music yet; that's expected)
2. Touch screen and right stick move the cursor; A / ZR / touching click. Menus usable?
3. New Game → START : does loading reach 100 % or does it crash / run out of memory? Overlay `mem` / `peak` values
4. In the game : fps and `frame` (ms) in the Class-D cell, then while walking; left stick moves, right stick looks
5. HOME button and back; quitting from the pause menu (+ button) returns to the HOME Menu

## Controls (GamePad, current mapping — `runtime/gxruntime/gxinput.cpp`)

| GamePad | Game |
|---|---|
| Left stick | Move (W A S D) |
| Right stick | Look (mouse) |
| Touch screen | Cursor + left click |
| A / ZR | Left click (use / pick up) |
| Y | Right click |
| ZL | Blink (Space) |
| L | Crouch (Left Ctrl) |
| B / left stick press | Sprint (Left Shift) |
| X | Inventory (Tab) |
| + | Pause menu (Esc) |
| − | Save (F5) |
| D-pad | Arrow keys |
| Right stick press | Debug overlay on / off |

## Roadmap

| # | Milestone | State |
|---|---|---|
| M1 ✅ | Hello world | Done |
| M2 | Room viewer, performance / memory on hardware | Viewer done in Cemu; **hardware numbers missing** |
| M3 ✅ | Transpiler | Done : the whole game transpiles and compiles |
| M4 ✅ | Runtime port | Done (graphics, input, files); audio missing |
| M5 | Menu + first room, with input and sound | Menu and first room work in Cemu; **audio** missing; hardware check |
| M6 | Full game | Next : play through, fix rendering / logic differences, saves, memory |
| M7 | Polish | Performance, controls (Pro Controller, better mapping), GamePad screen use, packaging, credits / licences |

Next steps, in order :
1. Hardware test of the current build (owner) — decides how much memory / performance work is needed
2. Audio : software mixer (stb_vorbis) for Blitz sounds + the FMOD stream functions (music)
3. Memory : find where the 650 MB go (meshes are kept on CPU and GPU, 2D images keep CPU copies...) and reduce
4. Play through the game in Cemu with scripted input, fix what breaks
5. Software keyboard for text entry (save names, console)

## Known issues

- **No audio** (gxAudio and the FMOD functions are stubs).
- **Memory** : ≈ 650 MB in use in the game; the hardware budget isn't known yet (Cemu reports ~2 GB free).
- Intro video (BlitzMovie) isn't played; `play startup video` is off.
- Text is antialiased on the back buffer (stb_truetype has no hinting; 1-bit text looked broken). Text drawn into
  images (software) stays 1-bit like Blitz.
- White speckles on some masked 2D images (e.g. the SCP-173 picture on the main menu) : to investigate
  (masked colour vs. JPEG decoding?).
- Cube map textures aren't supported (treated as 2D).
- MD2 / 3DS / Q3 BSP model loaders aren't endian-fixed (SCP:CB doesn't use them).
- Integer division by zero returns 0 instead of crashing.
- Reading back GPU-rendered images (`LockBuffer` after `CopyRect` from the back buffer) returns stale data in
  Cemu (fine on hardware).
