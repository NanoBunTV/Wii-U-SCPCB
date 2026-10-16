# Building

This file covers the toolchain, building, the reference material and how to recreate it, and testing.

## Requirements

| What | Where (owner's PC) | Used for |
|---|---|---|
| devkitPro : devkitPPC, wut, wut-tools, wiiu-cmake, `ppc-zstd` | `F:\Programs\devkitPro` | Wii U build (`build.ps1`); `zstd.exe` from its msys2 (`msys2/usr/bin`) compresses the content |
| MinGW-w64 g++ (msys2 `mingw-w64-x86_64-gcc`) | `C:\msys64\mingw64\bin\g++.exe` | Building the PC tools `bb2cpp` and `texconv` |
| Python 3 (found as `py`) | `C:\Python315` | Asset tools (`convert_x.py`), `gen_bbapi.py` |
| CafeGLSL command-line compiler | `_ref/tools/cafeglsl/` | Shaders (see below) |
| SCP:CB 1.3.11 release | `_ref/game/` | Game assets (GFX, SFX, Data, Loadingscreens) |

## Build

The first time (and after changing the tools' sources), build the two PC tools :

```bash
tools/bb2cpp/build.sh
```

```bash
tools/texconv/build.sh
```

Then, from PowerShell 7 :

```powershell
.\build.ps1                 # configure (first time) and build
.\build.ps1 reconfigure     # re-run CMake : after editing game/src/*.bb, adding runtime files, ...
.\build.ps1 clean
.\build.ps1 -DebugBuild     # -O0, build-debug\
.\tools\build_errors.ps1 [args]   # same, but prints only the compiler errors (log in build\build_log.txt)
```

Outputs :

| File | What |
|---|---|
| `build/scpcb_game.wuhb` | **The game** (≈ 375 MB : all assets + precompressed textures, map assets zstd-compressed) |
| `build/scpcb.wuhb` | The M2 room viewer (`source/main.cpp`), kept as a renderer test |

The project path has spaces, which GNU make can't handle, so `build.ps1` maps the folder to a free drive letter
with `subst` for the duration of the build (same approach as the owner's Half-Life port). It runs CMake with
devkitPro's `WiiU.cmake` toolchain inside devkitPro's msys2 bash.

### What the build does

1. **Transpile** (at CMake configure time) : `tools/bb2cpp/bb2cpp.exe game/src/Main.bb build/game tools/bbapi.txt
   game/src/*.decls` writes the game as C++ (`build/game/bbgame_*.cpp`, ≈ 51 000 lines). Editing any `.bb` file
   re-runs configure automatically on the next build.
2. **Shaders** : `shaders/*.vert/.frag` → `build/content/shaders/*.gsh` (CafeGLSL, register uniform mode).
3. **Assets** (`assets` target) : copies `GFX`, `SFX`, `Data`, `Loadingscreens` from `_ref/game` and
   `game/options.ini` into `build/content/` (only changed files), converts binary `.x` models to text
   (`tools/convert_x.py`), and writes BC1 / BC3 versions of textures (`tools/texconv`, see below). Only changed
   files are reprocessed. Then `tools/pack_content.py` builds `build/content_wuhb/`, the folder packed into the
   .wuhb : hard links to `build/content/`, except `.wbc`, `.wbc3`, `.rmesh`, `.b3d`, stored zstd-compressed under
   the same names (ARCHITECTURE.md "Files"); only changed files are recompressed (the first time ≈ 30 s).
4. **Compile** the runtime (`runtime/*`), the generated game and `runtime/userlibs`, link, `elf2rpl`, `wuhbtool`
   (with the Home Menu icon and boot screens from `meta/` : `icon_128.png`, `boot_tv.png`, `boot_drc.png`,
   resized from the owner's `icon.png` / `boot.png`; to change them, replace those and resize to 128x128,
   1280x720, 854x480).

A full rebuild takes about 1.5-2.5 minutes; most of that is packing the .wuhb.

### Tools

| Tool | What |
|---|---|
| `tools/bb2cpp/` | BlitzBasic → C++ transpiler : the original Blitz3D compiler front end + `cppgen.cpp` (see ARCHITECTURE.md). `build.sh` builds `bb2cpp.exe` |
| `tools/gen_bbapi.py` | Regenerates `runtime/bbruntime/bbapi.h`, `tools/bbapi.json` and `tools/bbapi.txt` (the command list bb2cpp reads) from the `rtSym(...)` tables. **Run after adding / changing runtime commands** |
| `tools/texconv/` | Texture precompression : `<image>.wbc` (BC1) and `<image>.wbc3` (BC3, images with alpha) next to every image, power-of-two + mipmapped like the runtime. `LoadTexture` uses them when Blitz's flags allow (no mask flag; alpha flag only if the image has alpha) |
| `tools/convert_x.py` | Binary / MSZIP-compressed `.x` → text `.x` (the runtime's .x loader reads text only) |
| `tools/pack_content.py` | `build/content` → `build/content_wuhb` with the map assets zstd-compressed (see "What the build does") |
| `tools/cemu_run.ps1` | Automated Cemu run (see Testing) |
| `tools/build_errors.ps1` | Build and show only compiler errors |

### Shaders (CafeGLSL)

GLSL in `shaders/` is compiled at build time to `.gsh` by CafeGLSL's command-line compiler
(`_ref/tools/cafeglsl/bin/glslcompiler.exe`, MIT licence, from
https://github.com/Exzap/CafeGLSL/releases, v1.0.0-rc1 Windows x86_64 zip, extracted so that `bin/` is directly
under `_ref/tools/cafeglsl/`). CMake function `add_shader_pair(name vert frag)` adds a pair; uniforms use register
mode.

## Reference material (`_ref/`, not part of the port)

Downloaded / generated on 2026-10-13. Treat as read-only input; it doesn't belong in git.

| Folder | What | How to recreate |
|---|---|---|
| `_ref/game/` | SCP:CB v1.3.11 release (exe, GFX, SFX, Data) | download `https://undertowgames.com/file/scp/SCP%20-%20Containment%20Breach%20v1.3.11.zip`, unzip |
| `_ref/scpcb-src/` | Official 1.3.11 source (pristine; the port's working copy is `game/src/`) | `git clone --depth 1 https://github.com/Regalis11/scpcb _ref/scpcb-src` |
| `_ref/blitz3d-runtime/` | Original Blitz3D engine + compiler (zlib) | `git clone --depth 1 https://github.com/blitz-research/blitz3d _ref/blitz3d-runtime` |
| `_ref/B3DDecomp/` | The decompiler | `git clone --depth 1 https://github.com/juanjp600/B3DDecomp _ref/B3DDecomp` |
| `_ref/tools/cafeglsl/` | CafeGLSL shader compiler (build dependency) | see "Shaders" above |
| `_ref/tools/cemu/` | Portable Cemu 2.6 copy for automated runs | copy `Cemu.exe`, `resources/`, `gameProfiles/` from the main install, create `portable/settings.xml` (see Testing) |
| `_ref/decomp/` | Decompiler input/output : `scpcb.exe` (copy of the game exe), `fmod.decls`, `scpcb_disasm/`, `scpcb_decomp/` | see below |
| `_ref/unused_blitz3d/` | Blitz3D runtime files the port doesn't use (DirectPlay, DLL loader, unused model code), moved out of `runtime/` | — |

### Running the decompiler

Needs the .NET 8 SDK (installed). One project targets net6.0, which isn't installed, so allow roll-forward.

```bash
cd "_ref/B3DDecomp" && DOTNET_ROLL_FORWARD=Major dotnet build Blitz3DDecomp.sln -c Release
cd "../decomp"
export DOTNET_ROLL_FORWARD=Major
../B3DDecomp/bin/Release/net8.0/Blitz3DDisasm.exe scpcb.exe
../B3DDecomp/bin/Release/net8.0/Blitz3DDecomp.exe scpcb_disasm/ fmod.decls
```

Run the two steps separately : dropping the exe on `Blitz3DDecomp` tries to start `Blitz3DDisasm` by name and
fails unless the bin folder is the current directory. `fmod.decls` comes from the official source
(`_ref/scpcb-src/fmod.decls`); without it decompilation fails (game bug in the decls, see B3DDecomp's README).

Regenerate RUNTIME_API.md's builtin list : `bash Docs/tools/list_builtins.sh _ref/decomp/scpcb_disasm`.

## Testing

### Hardware (Wii U with Aroma)

Copy `build/scpcb_game.wuhb` to `sd:/wiiu/apps/` and start it from the HOME Menu. Saves and changed options go to
`sd:/wiiu/apps/scpcb/` (the writable overlay, see ARCHITECTURE.md "Files"). The yellow line at the top-left is
the debug overlay (fps, game frame time, draw calls, memory in use / peak); the right stick button toggles it.
Controls : see STATUS.md. The room viewer `build/scpcb.wuhb` shows up as "SCP: Containment Breach (room viewer)".

Diagnostics (`runtime/gxruntime/wiiu_debug.cpp`, also in the room viewer) :

- **Log file** : every log line also goes to `sd:/wiiu/apps/scpcb/log.txt`, rewritten on each start and flushed
  line by line, so it's complete up to a crash or hang. The first lines show how far startup got (`ProcUI and
  logging up`, `GX2 up, first frame presented`, `largest free block` = memory available, `runtime open`,
  `first game frame presented`).
- **Boot screen** : a black frame is presented right after GX2 init, so the system's boot logo disappears at once.
  Stuck on the boot logo = the app hangs or crashes before that (very early).
- **Crash screen** : DSI / ISI / program / alignment exceptions show an `OSFatal` screen with PC, LR, the faulting
  address (`dar`) and the stack's return addresses (also sent to OSReport with all registers).
- **Hang watchdog** : when no frame has been presented for 10 s (a hang, or a long load), a thread on CPU2 logs
  where the game thread is : `watchdog: ... pc ... lr ... stack ...`. Under Cemu these values are garbage.
- **Comparison switches** (empty files in `sd:/wiiu/apps/scpcb/`, read at startup) : `cpu_skinning.txt` (skin
  characters on the CPU), `no_shader_variants.txt` (general scene pixel shader only).
- **Overlay** (top left, three lines in a 22 px font so it can be read on the GamePad; **off by default** since the
  public release preparation, clicking the right stick shows / hides it) : fps, CPU and GPU ms per frame; logic /
  `UpdateWorld` / `RenderWorld` / wait in `Flip`; draws, triangles, memory.
- **GPU parts** : on the sampled frames (every 30th) `wiiu::gpuMark` puts GPU timestamps (`GX2SampleBottomGPUCycle`)
  in the command stream : frame start, end of each `RenderWorld`, after the 2D drawing, after the TV copy, after
  the GamePad copy, after the swap. Every 5 s a `GPU parts (ms ...) :` line gives the GPU time of each part (time
  since the previous mark, summed per name). The first two sampled frames also log the raw values (`gpu marks :`)
  to check the format. Cemu doesn't emulate these timestamps (all parts read 0 there).
- **Frame timing** : the overlay (and every 5 s the `perf:` log line) splits the frame time into game logic,
  `UpdateWorld` (Blitz collisions + animation), `RenderWorld` (CPU side of 3D rendering), and the wait in `Flip`
  for the previous frame to be shown. `tris` = triangles drawn by `RenderWorld` per frame (thousands). The
  `perf:` line's "GPU after submit" : every 30th frame `Flip` waits for the GPU right after submitting
  (`GX2DrawDone`) and times it, i.e. the GPU work still left when the CPU is done with the frame. Over
  ≈ 16 ms there means the GPU, not the CPU, holds the frame rate at 30.
- **Function timers** : every transpiled game function (bb2cpp puts `BB_PROF_SCOPE` at its top) and some runtime
  functions (`[UpdateWorld]` and its parts `animate` / `collide` / `collide, exact tests`, `[RenderWorld]` and
  its parts `enumVisible` / `beginRender` / `cull + queue` / `draw queues` / `transparents`,
  `[gxScene::render, per draw]`, `[Flip]` and its parts, `[LinePick]`, `[EntityPick]`, `[EntityVisible]`,
  `[CameraPick]`, `[MeshCollider rebuild]`, `[loadCanvas: precompressed / decode / upload + mips]`,
  `[loadCanvas: precompressed, file read]`, `[AddMesh]`, `[CopyMesh]`, `[FlipMesh]`,
  `[Surface::getMesh, rebuild / skinning]`, `[gxMesh::lock, new buffers]`, `[gxMesh::unlock, cache flush]`) time
  themselves, with `OSGetSystemTime` (reading the time base register directly gave inconsistent nested times in
  Cemu). Every 20 s the watchdog thread appends a report to
  `sd:/wiiu/apps/scpcb/profile.txt` (rewritten on each start) : fps over the window, then the 80 most expensive
  entries with self ms / inclusive ms / calls **per frame**. `[Flip]` self time is mostly waiting for the
  previous frame to be shown (vsync). Implementation : `runtime/gxruntime/wiiu_prof.*`; `-DSCPCB_NO_PROFILE`
  removes it. (A sampling profiler was tried first : on the Wii U a suspended thread only stops where it waits,
  so busy code never showed up.)
- **App lifecycle** (`runtime/gxruntime/wiiu_app.*`) : the RPX main thread runs ProcUI; the game runs on its own
  thread (8 MB stack, CPU1). When the system wants the screen (HOME Menu), the main thread asks the game thread to
  pause (at its next `Flip` / input poll, after `GX2DrawDone`) before releasing the foreground. When the system
  closes the app, the Blitz program is stopped and graphics aren't freed (GPU commands after the release would
  hang). Log lines start with `app:`.
- **Log file** : lines are buffered and the watchdog thread `fflush`es + `fsync`s the file once a second and
  after a hang line (an `fflush` alone left a 0-byte file after a forced power-off). Doing it for every line cost
  ≈ 4 ms per line on the console : seconds during map generation, which logs hundreds of lines. A forced
  power-off loses at most the last second.
- **Collision counters** : every 600 `UpdateWorld`s a `collisions per update :` line gives the moving colliders,
  mesh tests, tests left after the bounding box rejection, sweep iterations, triangles in the visited tree leaves,
  tree nodes visited and triangles given to the exact test.
- **File read speed** : every 100 whole-file reads (images, textures, `.b3d`) a `file reads :` line gives the
  megabytes, time, MB/s and ms per file (open + read + close). Cemu reads ≈ 1 GB/s; on the console this shows
  whether loading is limited by the storage.
- **Reads by file type** : once reading has stopped for 2 s (end of a loading screen), `file reads, total :` gives
  the files, megabytes read (compressed size) and time, plus the megabytes decompressed and the time it took, and
  `file reads, by type :` the same per extension (whole-file reads and Blitz `ReadFile` streams).
- **Memory breakdown** : `memory :` lines (after the first frame, then every 15 s while the total moves by 2 MB) :
  heap in use, GPU textures (RGBA / compressed), render targets, CPU image copies (and those also on the GPU),
  GPU mesh buffers, decoded sounds, mesh vertex arrays (`surfaces (CPU)`, with vertex / triangle counts and unused
  capacity), GPU skin buffers, collision trees (vertices, triangles, cached planes, tree), and the 12 biggest
  CPU image copies with their file names (`(created)` : made by the game).
- **`EntityVisible` counters** : every 1000 calls an `EntityVisible per call :` line gives the obscurers, how
  many passed the box + segment test (meshes, spheres / boxes), tree nodes, exact triangle tests, how often the
  pick lists and the box array were rebuilt, and the average time per call (µs, collision tree builds included).
- **Decoding addresses** : on hardware (Aroma) the code runs **0x0A000000 above** the ELF's link addresses
  (`0c1a9c9c` on the console = `021a9c9c` in the ELF); the log's first line prints the runtime address of
  `wiiu::debugInit`, compare with `powerpc-eabi-nm -C build/scpcb_game.elf | grep debugInit` to get the offset.
  Subtract it, then `powerpc-eabi-addr2line -f -C -i -e build/scpcb_game.elf <addresses>` (keep the `.elf` of
  the tested build). Addresses around `01xxxxxx` are system libraries. In Cemu there's no offset.
- The game runs on its own thread with an **8 MB stack** on CPU1 (`wiiu::appMain`) : the RPX main thread
  only gets 64 KB (elf2rpl's default, not configurable).

### Cemu (PC, for iteration)

**Automated runs** : `.\tools\cemu_run.ps1 [-Wuhb build\scpcb_game.wuhb] [-Seconds 10] [-Shot build\cemu_shot.png]
[-ShotsAt 95,110] [-Filter regex] [-Interpreter]` (`-ShotsAt` : extra screenshots `<shot>_<seconds>.png`; give
`-Shot` as an absolute path) starts the `.wuhb` in the project's **portable Cemu copy** (`_ref/tools/cemu/`,
same 2.6 version as the main install : `Cemu.exe`, `resources/`, `gameProfiles/` copied, plus a `portable/` folder
with its own `settings.xml` = the owner's settings with fullscreen off, update check off, empty mlc path). After the
given time it saves a capture of Cemu's window only (`PrintWindow`, never the screen), closes that Cemu and prints
the `[OSConsole]` lines from `_ref/tools/cemu/portable/log.txt`. The copy exists because the owner's main install
is used at the same time by the Half-Life port (shared log and settings). `-Interpreter` runs Cemu's PPC
interpreter (slow, exact memory accesses) for crash hunting. `-Pad` also opens Cemu's GamePad view (a normal
window, 854x480 picture) and saves it with every screenshot as `<shot>_pad.png` : the GamePad HUD differs from
the TV picture. (Cemu's GamePad view shows white speckles in dark scene areas, with or without the HUD scaling; its TV view
doesn't.)
**Runs are silent** : every run sets the portable Cemu's TV / GamePad volume to 0 and no output device (an untested
audio path in another port once played loud static into the owner's headphones). `-Audio [-Volume 15]` turns the TV
output on at that volume (device id in `_ref/tools/cemu/portable/audio_device.txt`), only for listening tests the
owner asks for.

**Scripted input** : if `autoinput.txt` exists in the writable folder, its timed events are merged with the
GamePad. For the portable Cemu that's `_ref/tools/cemu/portable/sdcard/wiiu/apps/scpcb/autoinput.txt` (the real
SD card never has it). One event per line, time in ms since start :

```
9000 key 57              Space (DirectInput scan code), pressed for 100 ms
11000 mouse 374 337      move the mouse (back buffer pixels, 1280x720)
12000 click 374 337      move and left-click
20000 keydown 17         hold W ...
25000 keyup 17           ... until here
```

The current script reaches the game : Space at the title, NEW GAME (374,337), START (1181,515), Space to leave
the loading screen (60, 75, 90 s), then walks forward into the cell's wall (93-101 s) and backwards out of the
cell (103-113 s) : a collision check (`-ShotsAt 102` and a final shot at ≈ 116 s).
`mouse` positions are absolute and the game doesn't re-centre the cursor, so to look around use increasing x
values (≈ 260 px ≈ 90° from the centre, 640); repeating the same position doesn't turn. Tab (`key 15`) opens
the inventory; scripted clicks in the inventory don't register (to be looked at), so items can't be used from a
script yet.

**Log lines worth knowing** : `perf:` (every 5 s : fps, draws, uploads, read-backs, GPU syncs, memory, GPU time
after submit), `gxGraphics: back buffer in MEM1` (or MEM2 if MEM1 was full), `file reads :`, `memory :`,
`gxRuntime: largest free block`, `blitz error:` (a Blitz runtime error, e.g. a missing file), `scpcb: runtime
error:` (the program stopped).

**Manual** : open a `.wuhb` in the owner's main Cemu install
(`E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\Cemu.exe`, File → Load). Cemu loads homebrew `.rpx` / `.wuhb`
directly, no Aroma needed.

- Logging : wut's `WHBLogCafeInit()` writes through `OSReport`; Cemu logs it as `[OSConsole]` in `log.txt`.
- Debugger : Debug → Open debugger (breakpoints, PPC disassembly, memory).
- Cemu differs from hardware : it doesn't reflect GPU writes in CPU memory (read-backs return stale data), and
  its frame rate says nothing about the real console's. Anything that works in Cemu still needs a hardware check.
