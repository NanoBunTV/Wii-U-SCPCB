# Status, known issues and roadmap

## Current state (2026-10-15)

**The whole game is transpiled, builds and runs in Cemu** : title screen → main menu → New Game → map
generation and loading → in the game (Class-D cell, intro) with the HUD and audio. Runs on hardware (Aroma) at
≈ 55-58 fps in the first cell (see the test log below).

| Part | State |
|---|---|
| Wii U back end (`runtime/gxruntime`) | Graphics (2D + 3D), input (GamePad + touch, Pro Controllers), audio, files, timers, software keyboard done; HUD drawn bigger on the GamePad |
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
| 2026-10-14 | same | **Wii U (Aroma)** | **Loading much faster** (`EntityVisible` ≈ 0.5 ms per call instead of ≈ 2 ms; `LoadRMesh` self ≈ 3.7 s, texture file reads ≈ 4.5 s at ≈ 17 ms per file : probably the storage's read speed). Still ≈ 30 fps : **GPU after submit ≈ 23-26 ms per frame** (CPU ≈ 14-18 ms) : the GPU is now the limit; ≈ 5 ms of GPU time even on the 2D menu |
| 2026-10-14 | + back buffer (colour + depth) in MEM1 | Cemu 2.6 | Renders as before; log `gxGraphics: back buffer in MEM1` |
| 2026-10-14 | same | **Wii U (Aroma)** | HOME Menu and back works (back buffer placed in MEM1 again). Still ≈ 30 fps : GPU after submit 23-26 → **21-22 ms**, CPU ≈ 16 ms; ≈ 4.6 ms of GPU time on the 2D menu. TV output is 1920x1080 (the 1280x720 back buffer is scaled up for it). File reads ≈ 11-13 MB/s, 10-18 ms per file : loading is limited by the storage. Overlay too small to read on the GamePad |
| 2026-10-14 | + GPU timestamps per frame part (`GPU parts` log line), bigger 3-line overlay with CPU / GPU ms | Cemu 2.6 | Overlay readable; GPU parts read 0 in Cemu (no GPU timestamps there) |
| 2026-10-14 | same | **Wii U (Aroma)** | GPU timestamps work. Cell : GPU ≈ 21 ms = **3D scene ≈ 16-17 ms**, TV copy 3.1, GamePad copy 0.7, 2D 0.6. Menu : ≈ 4.5 ms (mostly the copies) |
| 2026-10-14 | + pixel shader variants (no per-pixel branches, no fetches for unused / bump stages), no clears before the screen copies | Cemu 2.6 | 5 variants load; screenshots with / without (`no_shader_variants.txt`) identical |
| 2026-10-14 | same | **Wii U (Aroma)** | **≈ 50-60 fps in the cell** (profile : 55-58 fps). Everything looks normal. GPU ≈ 9 ms (3D scene 16-17 → ≈ 6 ms, TV copy 3.1 → 2.1 ms). CPU ≈ 16.5 ms is the limit now : exact collision tests ≈ 6 ms (≈ 5400 triangle tests per update against the dense toilet / sink meshes) |
| 2026-10-14 | + edge test without a basis matrix per edge | Cemu 2.6 | Collision counters identical to before (same sweeps, same 5433 / 5440 exact tests); exact tests 4.0 → 3.6 ms in Cemu; walking into the sink / wall still stops |
| 2026-10-14 | same | **Wii U (Aroma)** | **54-58 fps in the cell**, CPU ≈ 15.7 ms, GPU ≈ 9 ms. Walls / furniture still stop the player. Exact collision tests ≈ 6.3 → 4.2-5.5 ms |
| 2026-10-14 | + y-scaled colliders (different x / y radii, e.g. the player) on the local-space collision path | Cemu 2.6 | Collision counters unchanged. Found : almost all exact tests come from one class-D NPC parked below the floor of a nearby room (radius 0.32, y ≈ -1.43), pushed into the same dense geometry every update (≈ 5400 triangle tests) : the game's own behaviour. A per-collider result cache was tried and dropped (its target position changes slightly every update, so it never hit) |
| 2026-10-14 | same | **Wii U (Aroma)** | ≈ 55-57 fps in the cell (exact collision tests ≈ 3.3-5.7 ms) |
| 2026-10-14 | + **audio** : sounds (Ogg / WAV) and FMOD streams (music) on AX voices | Cemu 2.6 (muted) | Menu music and game sounds play (AX consumes the streams at exactly their sample rate : 44.1 / 96 kHz; up to 62 voices at once in the intro); decoded samples look like audio, not byte-swapped noise. Not listened to (automated runs are muted) |
| 2026-10-14 | same | **Wii U (Aroma)** | **Audio works** (owner : "works great"), HOME Menu and back doesn't break it. Menu 60 fps, cell ≈ 57 fps. `[LoadSound: decode]` ≤ 0.8 ms per frame on average (no complaints of hitches). Map creation ≈ 20 s : texture file reads ≈ 6.3 s, `LoadRMesh` ≈ 3.1 s, image decodes ≈ 3.1 s |
| 2026-10-14 | + title "SCP: Containment Breach", Home Menu icon + boot screens (`meta/`), bigger main / pause menus (sub-screens on the right side), options that don't apply removed, GamePad controls listed in Options → Controls, touch : drag to look / tap to click in gameplay, R = console, system keyboard for text boxes | Cemu 2.6 | Menus readable at 1:1 (main buttons, New Game, Load Game, Options tabs, pause menu + its options); keyboard opens on the save name box (Cemu shows its own keyboard); quick save (−) writes `sd:/wiiu/apps/scpcb/Saves/untitled/save.txt` and Load Game loads it back |
| — | same | **Wii U (Aroma)** | **Pass** : Home Menu icon / boot screen; menus readable on the GamePad; system keyboard (type, OK); save (−), quit to menu, Load Game; touch drag to look and tap on items / doors; 55–56 fps in game. Drag-to-look speed doubled afterwards (0.35 → 0.7) at the owner's request |
| 2026-10-15 | + **memory** (`memory :` report; vertex arrays reserved, double-sided collision on shared vertices, orphan alpha meshes freed, bounded collision plane cache, image CPU copies only when needed) + **compressed content** (zstd : `.wbc` `.wbc3` `.rmesh` `.b3d`) | Cemu 2.6 | Memory in the cell **≈ 648 → 420 MB**, peak while loading ≈ 668 → 421 MB. New game reads **135 → 46 MB** (decompressing ≈ 129 MB : ≈ 1 s in Cemu). Rendering the same; walking into the cell's wall stops, walking backwards reaches the same corridor as the original code; collision cost unchanged (`UpdateWorld` ≈ 1.2 ms in Cemu, 2-5 leaf plane rebuilds per frame). `.wuhb` 580 → 375 MB |
| 2026-10-15 | same | **Wii U (Aroma)** | **Pass** : new game loads "a little faster", **432 MB** in the cell, ≈ 57 fps, walls / furniture stop the player, documents / item icons / loading screens normal. Log : new game read **53 MB in 5.9 s** (≈ 9 MB/s) + **decompressing 134.5 MB took 3.5 s** (≈ 38 MB/s : much slower than Cemu's ≈ 1 s), so ≈ 9.4 s instead of ≈ 11 s of reading. `largest free block` says 2047 MB, the top of its search : not a real budget (malloc must succeed lazily). **"Load map" on the New Game screen crashed** → button removed (custom maps from the PC map creator) |
| 2026-10-15 | + **GamePad HUD** (the in-game HUD drawn again 1.5x bigger on the GamePad only, `WiiU_HUDAnchor`...), brightness ("screen gamma") applied when presenting, **Pro Controller** support, `cemu_run.ps1 -Pad` | Cemu 2.6 | TV picture unchanged. GamePad : meters 1.5x at the bottom left, inventory slots / names / pointer 1.5x about the centre, overlay unchanged (screenshots `build/hud_*_pad.png`). Cemu's GamePad view shows white speckles in dark scene areas, **also without the change** (Cemu). Not checked in Cemu : touch on the bigger inventory, documents / radio / S-NAV views (scripted clicks don't register in the inventory), brightness slider, Pro Controller |

## Hardware test checklist (next build)

Copy `build/scpcb_game.wuhb` to `sd:/wiiu/apps/`. If anything goes wrong, send `sd:/wiiu/apps/scpcb/log.txt`
(written on every start) and a photo of any crash screen. This build : the GamePad HUD and the Pro Controller.

1. In the game, compare the TV and the GamePad : the TV as before; on the GamePad bigger blink / stamina meters (bottom
   left), messages, hand icons and inventory (X); the pause menu the same as on the TV. Is 1.5x a good size?
2. Inventory on the GamePad : tap / drag items with the touch screen. Do taps land on the slot under the finger?
3. Open a document (double tap in the inventory), the radio and the S-NAV if you have them : on the GamePad do they
   fit on the screen?
4. Keypad on a door (touch the digits), pause menu → Options → brightness slider : brighter / darker on both screens?
5. Pro Controller : connect it (HOME Menu → Controller Settings → Pair if needed) and play with it; same buttons as the
   GamePad. Does the GamePad still work at the same time? Right stick look speed (GamePad too) unchanged?
6. Fps in the cell : still ≈ 57? (The overlay is now off by default : click the right stick to show it.)

## Controls (GamePad, current mapping — `runtime/gxruntime/gxinput.cpp`)

Wii U Pro Controllers (any of the four) use the same buttons and sticks, together with the GamePad (no touch screen).

| GamePad | Game |
|---|---|
| Left stick | Move (W A S D) |
| Right stick | Look (mouse) |
| Touch screen | Menus, inventory, keypads (the game shows a pointer) : cursor, tap = left click (on the HUD as the GamePad shows it, bigger). Gameplay : drag = look around, quick tap = left click (interact) |
| A / ZR | Left click (use / pick up) |
| Y | Right click |
| ZL | Blink (Space) |
| L | Crouch (Left Ctrl) |
| B / left stick press | Sprint (Left Shift) |
| X | Inventory (Tab) |
| + | Pause menu (Esc) |
| − | Save (F5) |
| R | Console (F3), when enabled in Options → Advanced |
| D-pad | Arrow keys |
| Right stick press | Performance overlay on / off (off at start) |

## Roadmap

| # | Milestone | State |
|---|---|---|
| M1 ✅ | Hello world | Done |
| M2 | Room viewer, performance / memory on hardware | Viewer done in Cemu; **hardware numbers missing** |
| M3 ✅ | Transpiler | Done : the whole game transpiles and compiles |
| M4 ✅ | Runtime port | Done (graphics, input, files, audio) |
| M5 ✅ | Menu + first room, with input and sound | Done on hardware : ≈ 55-58 fps in the first cell, audio works |
| M6 | Full game | Next : play through, fix rendering / logic differences. Saves work; memory reduced (≈ 420 MB) |
| M7 | Polish | Performance, controls (Pro Controller done, better mapping), GamePad screen use (bigger HUD done), packaging, credits / licences |

Next steps, in order :
1. Play past the first cell (owner on hardware, Cemu with scripted input) and fix what breaks : the intro
   sequence, SCP-173's chamber, the Light Containment Zone, doors / keycards, items, documents, NPCs
2. Controls : feedback on the GamePad HUD (size 1.5x : `hud_scale` in gxgraphics.cpp, or an option if wanted), the
   Pro Controller, the right-stick look speed
3. Loading time : file reads ≈ 5.9 s + zstd decompression ≈ 3.5 s on the console. Next : read and decompress on
   other cores while the game builds rooms (prefetch), a faster codec for textures (LZ4 : less gain in size,
   several times faster to decode), `InitWayPoints`' `EntityVisible` calls, collision tree builds, image decodes
4. Memory (≈ 432 MB on the console) : the real budget is still unknown (`largest free block` isn't reliable);
   candidates left in ARCHITECTURE.md "Memory"
5. CPU budget (≈ 16 ms per frame in the cell)

## Known issues

- **Audio** works on hardware (AX voices + stb_vorbis). Sounds are decoded when loaded, on the game thread. No 3D
  sound / MP3 (SCP:CB doesn't need them).
- **Performance on hardware : ≈ 50-60 fps in the first cell** : GPU ≈ 9 ms, CPU ≈ 16.5 ms (just over the
  16.7 ms vsync budget at times). Biggest CPU cost left : exact collision tests (≈ 4-5 ms), nearly all from one
  class-D NPC stuck below a nearby room's floor and pushed into dense geometry every update (original game
  behaviour); then `RenderWorld` ≈ 2.7 ms, `UpdateWorld` rest ≈ 2 ms.
- **Loading reads files at ≈ 11-13 MB/s** on the console (`file reads :` log line), ≈ 10-18 ms per file : the
  SD card's speed. Map assets are now stored zstd-compressed (135 → 53 MB read per new game on the console :
  5.9 s), but decompressing takes ≈ 3.5 s on the console's CPU, so the gain is ≈ 1.5 s.
- **"Load map"** (custom maps) removed from the New Game screen : it crashed on the console.
- **Memory** : ≈ 420 MB in use in the game in Cemu (was ≈ 650 MB, ARCHITECTURE.md "Memory"); the hardware budget
  isn't known yet (Cemu reports ~2 GB free). GPU meshes are made the first time a room is seen, so memory grows
  while exploring.
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
