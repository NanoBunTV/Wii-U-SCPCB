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
| 2026-10-14 | Game + big stack | **Wii U (Aroma)** | Gets past the boot screen (GX2 up, 1280x720 graphics open). **Crash (DSI)** in `UpdateINIFile` : `ReadFile("loadingscreens\loadingscreens.ini")` returned 0. Cause : the game lowercases INI names; on hardware the content folder can't be listed and its names are case-sensitive, so the case-insensitive lookup failed. Log : `largest free block 2047 MB` (the probe's upper limit, so ≥ 2 GB?!, to be checked). Code runs 0x0A000000 above the ELF's link addresses (subtract before addr2line) |
| 2026-10-14 | + content file index (`filelist.txt`) | Cemu 2.6 | Reaches the game |
| 2026-10-14 | + content file index | **Wii U (Aroma)** | Same crash (`ReadFile` of `loadingscreens.ini` returned 0). Real cause : `InitLoadingScreens` holds the file open with `OpenFile` (read/write) while `GetINIString` opens it again with `ReadFile`; the Wii U file system refuses a second open of a file open for writing |
| 2026-10-14 | + `OpenFile` read-only until the first write | Cemu 2.6 | Reaches the game |
| 2026-10-14 | + `OpenFile` fix | **Wii U (Aroma)** | **Main menu and a new game work.** Map generation + loading take a few minutes (several watchdog lines). In the game **≈ 10-13 fps**, ≈ 75 draws / frame, **memory ≈ 648 MB** (fits). The system's boot music keeps playing (no audio init). Some visual glitches (not described yet). **HOME → close hangs** at the system loading screen |
| 2026-10-14 | + AX init, exit fix, timing overlay, profiler, no GPU drain per frame | Cemu 2.6 | Reaches the game; in the Class-D cell `UpdateWorld` is the biggest cost even in Cemu (≈ 14 ms of 24) |
| 2026-10-14 | same | **Wii U (Aroma)** | Boot music gone. **HOME → close still hangs** (log.txt empty : never closed, not fsynced). In game ≈ 15-17 fps : frame ≈ 45 ms CPU (logic ≈ 9, UpdateWorld ≈ 19, RenderWorld ≈ 13-19), flip wait 0 → **CPU-bound**, plus ≈ 17 ms unaccounted inside Flip. Sampling profiler useless on hardware. **Glitches** : garbage 4x4 blocks on the light sprites ("fake bloom") and on some mip levels of a metal texture; a document image shows black |
| 2026-10-14 | + GPU memory cache flush, ProcUI on the main thread, function timers, mallinfo once a second | Cemu 2.6 | Reaches the game; function timer reports written |
| 2026-10-14 | same | **Wii U (Aroma)** | **Glitches gone, HOME → close works.** Document still black. In game **≈ 23 fps** (mallinfo was the missing Flip time). Function timers, in the cell : RenderWorld ≈ 17-19 ms self (CPU side of the 3D render, 2 calls / frame), UpdateWorld collide ≈ 12-15 ms (≈ 163 calls), logic ≈ 6-7 ms. Loading : `ReadINILine` ≈ 1 s per frame at the menu, `LoadRMesh` ≈ 7 s and `CalculateRoomExtents` ≈ 3 s in map creation, `EntityVisible` ≈ 1.1 s per frame in `InitWaypoints` |
| 2026-10-14 | + GPU colour cache flush before sampling a drawn canvas | Cemu 2.6 | Reaches the game |
| 2026-10-14 | same | **Wii U (Aroma)** | Document still black. ≈ 22 fps in the cell (collide ≈ 13-17 ms, RenderWorld ≈ 16-19 ms) |
| 2026-10-14 | + CPU `ResizeImage2`, fast square root, collision box / plane rejection, world-space culling, cached entity lists, native `ReadINILine`, log written once a second | Cemu 2.6 | In the cell **40 → 60 fps** (vsync) : UpdateWorld 15 → 6 ms (exact collision tests 18 → 4.7 ms), RenderWorld 6.5 → 3.2 ms (culling 5.6 → 1.7 ms). New game loads within the first 20 s. Loading screen images (same `ResizeImage2`) correct |
| 2026-10-14 | same | **Wii U (Aroma)** | **Document readable.** In the cell ≈ 26 fps : exact collision tests 16.7 → 6.5 ms, but `[RenderWorld: cull + queue]` 16.3 ms (1.7 in Cemu). Loading unchanged : precompressed textures ≈ 23 ms each (≈ 7.7 s, file reads), `LoadRMesh` own code ≈ 7 s, PNG decodes ≈ 70 ms each |
| 2026-10-14 | + reused mesh buffers, one mesh per sprite, timers on mesh updates | Cemu 2.6 | Cull + queue self 1.7 → 0.6 ms; light sprites correct |
| 2026-10-14 | same | **Wii U (Aroma)** | In the cell **≈ 30 fps** (26 before) : cull + queue 16.3 → 0.9 ms. Left : exact collision tests ≈ 7.9 ms, skinning ≈ 7.1 ms (8 surfaces per frame). Frame ≈ 20-25 ms of CPU, so vsync holds it at 30. Loading : `[MeshCollider rebuild]` ≈ 4.5 s (849 trees) |
| 2026-10-14 | + local-space collisions with cached triangle planes, faster tree build, mesh box for rejection, skinning skipped for unchanged poses | Cemu 2.6 | Exact collision tests 6.6 → 4.0 ms; walking into a wall stops correctly |
| 2026-10-14 | same | **Wii U (Aroma)** | ≈ 30 fps : exact collision tests 7.9 → 6.4 ms, skinning ≈ 6.8 ms (≈ 8400 vertices per frame : memory-bound). Loading feels the same : map creation ≈ 20 s = `LoadRMesh` 6.6 s, precompressed textures 5.9 s, `EntityVisible` 3.7 s, collider trees 0.9 s (was 4.5) |
| 2026-10-14 | + GPU skinning, whole-file reads for images / textures / .b3d, slab test in collision trees | Cemu 2.6 | Frame ≈ 8.7 ms (was ≈ 12); skinning gone from the profile. Characters not visible from the cell in Cemu (the cell door doesn't show) : **GPU skinning only checked on the console** |
| 2026-10-14 | same | **Wii U (Aroma)** | **Guards animate correctly** (GPU skinning works). ≈ 30 fps, perhaps slightly better. The log shows the CPU now needs only ≈ 11-14 ms per frame, so something else (GPU?) holds it at 30. Loading ≈ 30 s+ : `InitWayPoints` ≈ 4700 `EntityVisible` calls at ≈ 2 ms (≈ 9.5 s; each walked ≈ 17000 enabled objects), `LoadRMesh` ≈ 6.5 s, precompressed textures ≈ 5.1 s (≈ 21 ms each, whole-file reads didn't help) |
| 2026-10-14 | + pick lists / world-box array for `EntityVisible` and picks, native `LoadRMesh` vertex / triangle reads, aligned file buffers, batched texture staging frees, GPU time sample in the log | Cemu 2.6 | Rooms load and look as before. During `InitWayPoints` each `EntityVisible` now scans ≈ 3000 boxes and runs ≈ 75 mesh tests (was : walked 17000 objects); the box array is rebuilt about once per 1000 calls |
| — | same | **Wii U (Aroma)** | **To test** : loading time (stopwatch from START to the cell), fps; send `log.txt` (the `perf:` lines' "GPU after submit") and `profile.txt` |

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

- **No audio** (gxAudio and the FMOD functions are stubs). The audio system (AX) is initialised at startup only to
  stop the system's boot music.
- **Performance on hardware : ≈ 30 fps in the first cell** although the CPU part is down to ≈ 11-14 ms per
  frame (vsync rounds anything over 16.7 ms down to 30 fps). Being measured : the GPU's share ("GPU after submit"
  in `perf:`). Biggest CPU cost left : exact collision tests (≈ 4-6 ms).
- **Loading a new game is slow on the console** (≈ 30 s+ before this round) : precompressed textures ≈ 21 ms
  each, `InitWayPoints`' `EntityVisible` calls and `LoadRMesh` (both reworked, to be measured), PNG decodes
  ≈ 70 ms each. Candidate : loading textures on another core.
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
