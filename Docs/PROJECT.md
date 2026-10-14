# Project overview and decisions

## What it is

A native Wii U homebrew port of **SCP - Containment Breach** v1.3.11 (Undertow Games, Blitz3D, 2012-2018).
"Native" means the game logic runs as compiled PowerPC code inside a normal wut application (`.rpx` / `.wuhb`),
not inside an emulator or interpreter.

The game is written in BlitzBasic for the Blitz3D engine (Windows, DirectX 7). Neither runs on the Wii U, so the
port has two halves (details in FEASIBILITY.md and ARCHITECTURE.md) :

1. **Game code** : translate the game's BlitzBasic into C++ with a source-to-source transpiler.
2. **Engine** : re-host the Blitz3D runtime on the Wii U (GX2 graphics, AX/SDL audio, GamePad input, SD card files).

## History

- **2026-10-13** : project started. Feasibility study done (FEASIBILITY.md). Downloaded the 1.3.11 release,
  built and ran [B3DDecomp](https://github.com/juanjp600/B3DDecomp) on the exe (works : full `Main.bb` +
  328 function files). Found that the official 1.3.11 source ([Regalis11/scpcb](https://github.com/Regalis11/scpcb))
  and the original Blitz3D engine source ([blitz-research/blitz3d](https://github.com/blitz-research/blitz3d))
  are both public.
- **2026-10-13** : owner confirmed : use the official source, test on a Wii U with Aroma, bundle the game's
  assets in the `.wuhb`. M1 (hello world) done and running in Cemu. GX2 conventions spike done (see
  ARCHITECTURE.md "GX2 conventions"). Shaders are compiled offline with CafeGLSL. The original Blitz3D runtime
  (`blitz3d/`, `bbruntime/`, `stdutil/`, `config/`) copied to `runtime/` as the base of the port.
- **2026-10-13** : Wii U back end written (`runtime/gxruntime` : GX2 renderer with a D3D7 fixed-function shader,
  2D batcher, canvases, fonts, input, files). Blitz3D scene graph and runtime ported and endian-fixed. M2 room
  viewer renders all SCP:CB rooms in Cemu.
- **2026-10-13** : bb2cpp transpiler written (original Blitz3D compiler front end + C++ emitter). All of SCP:CB
  1.3.11 transpiles and compiles; the game runs in Cemu up to the first room (no audio). Added texture
  precompression (BC1/BC3), a text `.x` loader, scripted test input for Cemu, a debug overlay.

## Decisions and why

| Decision | Reason |
|---|---|
| Transpile to C++ ahead of time (no interpreter / JIT) | Homebrew can't easily allocate executable memory, and an interpreter on a 1.24 GHz Espresso costs too much CPU |
| Use the **official source** as the transpiler's input; keep B3DDecomp as a cross-check | Same version (1.3.11), but with real variable / field names and comments. The decompiler loses local and field names (`local1\Field15`). The decompiler stays useful to verify the public source matches the shipped exe, and its disassembler gives the exact builtin list (RUNTIME_API.md). Owner confirmed 2026-10-13 |
| Game assets are bundled in the `.wuhb` | Owner's decision (2026-10-13). One file to install. Asset licences still need checking against `Credits.txt` before any public release |
| Transpiled code calls the original runtime ABI (`_bbStrConcat`, `bbCreateMesh`...) | That's what the Blitz compiler's x86 output calls, so `bbruntime/` ports almost unchanged and keeps exact behaviour. Command signatures and defaults come from the runtime's own `rtSym(...)` tables |
| Shaders : GLSL compiled offline with CafeGLSL to `.gsh`, register uniform mode | No runtime compiler needed, normal GLSL. Register mode : no uniform-buffer lifetime / endian-swap issues |
| A portable copy of Cemu 2.6 in `_ref/tools/cemu` for automated runs | The owner's main Cemu install is shared with the Half-Life port, whose session runs it at the same time (shared `log.txt`, fullscreen setting). The copy has its own settings / log, runs windowed, and never touches the main install's settings |
| Reuse the original Blitz3D engine code (zlib licence) | Its scene graph, B3D/X/3DS loaders and ellipsoid collision system are platform-independent C++ (~10.5k lines); only the `gxruntime` layer (DirectX 7 / DirectDraw / FMOD / Win32) needs replacing. Reusing it is the best way to get identical collision and animation behaviour |
| Port licence : CC BY-SA 3.0 for game code | The game and its source are CC BY-SA 3.0, so derived game code must be too. Blitz3D-derived engine code stays zlib |
| Test on PC in Cemu during development | Owner's request; faster iteration. Final checks must be on hardware (see BUILDING.md) |
| Game source changes live in `game/src/` (a copy of the official source), marked `;Wii U port:` | Keeps `_ref/scpcb-src` pristine for comparison; changes are easy to find and to list. Settings defaults in `game/options.ini` |
| The game's DLL functions (FMOD, BlitzMovie, Win32, zlib) are implemented natively in `runtime/userlibs/` | No DLLs on the Wii U; the transpiler calls `_f<name>` C++ functions with the .decls signatures |
| Textures are precompressed to BC1/BC3 at build time (`tools/texconv`), used when Blitz's flags allow | RGBA8 textures ran out of memory while loading a new game; BC1 is 8× smaller. Runtime compression would be too slow on Espresso |
| GPU memory comes from the malloc heap (`memalign`), not the default MEM2 heap | wut gives most of MEM2 to malloc; the default heap ran out after a few rooms |
| Text on the back buffer is antialiased (deviation from Blitz) | stb_truetype has no hinting : 1-bit glyphs at small sizes lost strokes |
| Blitz-semantics exceptions : integer division by 0 returns 0 | x86 would crash the game; no reason to reproduce that |
| The game runs on its own 8 MB-stack thread; log file on the SD card, crash screen, hang watchdog | First hardware test hung on the boot screen with no way to see why; the RPX main thread has only 64 KB of stack (same lesson as the Half-Life port) |

## Open questions for the owner

- None right now. (Answered 2026-10-13 : official source; owner has a Wii U with Aroma; assets bundled in
  the `.wuhb`.)

## Owner preferences (for AI assistants)

- **Never run `git commit` or `git push`**, and don't stage changes. The owner handles all git operations.
- Document everything in this `Docs/` folder (same style as the owner's 3DTube Music project).
- Use Cemu (`E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\Cemu.exe`, latest version) for PC-side debugging.
  Automated runs use the portable copy of the same version (see BUILDING.md); the Half-Life port uses the main
  install at the same time.
- The owner has a Wii U with Aroma for hardware tests.
- **Don't copy builds to the owner's SD card or change files on it** (it's mounted as `M:` when they plug it
  in). The owner copies the `.wuhb` and manages the card. Reading `log.txt` from it when asked is fine.
- Never capture the owner's screen; only Cemu's own window (`tools/cemu_run.ps1` uses `PrintWindow`).
