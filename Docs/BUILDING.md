# Building

This file covers the toolchain, building, the reference material and how to recreate it, and testing.

## Requirements

| What | Where (owner's PC) | Used for |
|---|---|---|
| devkitPro : devkitPPC, wut, wut-tools, wiiu-cmake | `F:\Programs\devkitPro` | Wii U build (`build.ps1`) |
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
| `build/scpcb_game.wuhb` | **The game** (≈ 580 MB : all assets + precompressed textures) |
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
   files are reprocessed.
4. **Compile** the runtime (`runtime/*`), the generated game and `runtime/userlibs`, link, `elf2rpl`, `wuhbtool`.

A full rebuild takes about 1.5 minutes; most of that is packing the .wuhb.

### Tools

| Tool | What |
|---|---|
| `tools/bb2cpp/` | BlitzBasic → C++ transpiler : the original Blitz3D compiler front end + `cppgen.cpp` (see ARCHITECTURE.md). `build.sh` builds `bb2cpp.exe` |
| `tools/gen_bbapi.py` | Regenerates `runtime/bbruntime/bbapi.h`, `tools/bbapi.json` and `tools/bbapi.txt` (the command list bb2cpp reads) from the `rtSym(...)` tables. **Run after adding / changing runtime commands** |
| `tools/texconv/` | Texture precompression : `<image>.wbc` (BC1) and `<image>.wbc3` (BC3, images with alpha) next to every image, power-of-two + mipmapped like the runtime. `LoadTexture` uses them when Blitz's flags allow (no mask flag; alpha flag only if the image has alpha) |
| `tools/convert_x.py` | Binary / MSZIP-compressed `.x` → text `.x` (the runtime's .x loader reads text only) |
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
Controls : see STATUS.md. The room viewer `build/scpcb.wuhb` shows up as "SCP - Containment Breach (room viewer)".

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
- **Decoding addresses** : the RPX code runs at its link addresses (the log's first line prints the address of
  `wiiu::debugInit`; compare with `nm`). Keep the `.elf` of the tested build :
  `powerpc-eabi-addr2line -f -C -i -e build/scpcb_game.elf <addresses>`.
- The game runs on its own thread with an **8 MB stack** on CPU1 (`wiiu::runOnBigStack`) : the RPX main thread
  only gets 64 KB (elf2rpl's default, not configurable).

### Cemu (PC, for iteration)

**Automated runs** : `.\tools\cemu_run.ps1 [-Wuhb build\scpcb_game.wuhb] [-Seconds 10] [-Shot build\cemu_shot.png]
[-Filter regex] [-Interpreter]` starts the `.wuhb` in the project's **portable Cemu copy** (`_ref/tools/cemu/`,
same 2.6 version as the main install : `Cemu.exe`, `resources/`, `gameProfiles/` copied, plus a `portable/` folder
with its own `settings.xml` = the owner's settings with fullscreen off, update check off, empty mlc path). After the
given time it saves a capture of Cemu's window only (`PrintWindow`, never the screen), closes that Cemu and prints
the `[OSConsole]` lines from `_ref/tools/cemu/portable/log.txt`. The copy exists because the owner's main install
is used at the same time by the Half-Life port (shared log and settings). `-Interpreter` runs Cemu's PPC
interpreter (slow, exact memory accesses) for crash hunting.

**Scripted input** : if `autoinput.txt` exists in the writable folder, its timed events are merged with the
GamePad. For the portable Cemu that's `_ref/tools/cemu/portable/sdcard/wiiu/apps/scpcb/autoinput.txt` (the real
SD card never has it). One event per line, time in ms since start :

```
9000 key 57              Space (DirectInput scan code), pressed for 100 ms
11000 mouse 252 225      move the mouse (back buffer pixels, 1280x720)
12000 click 252 225      move and left-click
20000 keydown 17         hold W ...
25000 keyup 17           ... until here
```

The current script reaches the game : Space at the title, NEW GAME, START, then Space to leave the loading screen.

**Log lines worth knowing** : `perf:` (every 5 s : fps, draws, uploads, read-backs, GPU syncs, memory),
`gxRuntime: largest free block`, `blitz error:` (a Blitz runtime error, e.g. a missing file), `scpcb: runtime
error:` (the program stopped).

**Manual** : open a `.wuhb` in the owner's main Cemu install
(`E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\Cemu.exe`, File → Load). Cemu loads homebrew `.rpx` / `.wuhb`
directly, no Aroma needed.

- Logging : wut's `WHBLogCafeInit()` writes through `OSReport`; Cemu logs it as `[OSConsole]` in `log.txt`.
- Debugger : Debug → Open debugger (breakpoints, PPC disassembly, memory).
- Cemu differs from hardware : it doesn't reflect GPU writes in CPU memory (read-backs return stale data), and
  its frame rate says nothing about the real console's. Anything that works in Cemu still needs a hardware check.
