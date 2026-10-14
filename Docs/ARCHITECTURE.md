# Architecture

```
 PC (build time)                                   Wii U (run time)
 ───────────────                                   ────────────────
 game/src/*.bb ──► bb2cpp (transpiler) ──►  build/game/  (generated C++)
                                                      │ calls Blitz commands (bbapi.h)
 shaders/*.vert/.frag ──► CafeGLSL ──► .gsh            ▼
 _ref/game/GFX... ──► build/content (+ BC textures) runtime/bbruntime  Blitz commands, strings, types, banks, files
                                                      │
                                                   runtime/blitz3d    scene graph, loaders, collision, animation
                                                      │ gx* classes (same interface as the original gxruntime)
                                                      ▼
                                                   runtime/gxruntime  Wii U back end : GX2 renderer, input, files…
```

## Source layout

| Path | Purpose | Origin |
|---|---|---|
| `source/game_main.cpp` | Entry point of the game : opens the runtime, runs the transpiled `bbMain()` | new |
| `source/main.cpp` | The M2 room viewer (renderer test, `scpcb.wuhb`) | new |
| `game/src/` | The game's BlitzBasic source (copy of the official 1.3.11 source with `;Wii U port:` changes) + `.decls` | SCP:CB, modified |
| `game/options.ini` | Default settings shipped in the content folder | SCP:CB, modified |
| `runtime/bbruntime/bbcpp.h` | Support header for the generated code (exact x86 semantics helpers, type descriptor layouts) | new |
| `runtime/bbruntime/bbextras.cpp` | Blitz3D 1.106-1.108 commands missing from the open-source runtime (stubs) | new |
| `runtime/userlibs/` | Native versions of the game's DLL functions (FMOD, BlitzMovie, Win32, zlib) — stubs for now | new |
| `tools/bb2cpp/` | The transpiler (PC tool) | Blitz3D compiler + new emitter |
| `tools/texconv/` | Texture precompression (PC tool) | new |
| `runtime/bbruntime/` | Blitz commands that compiled Blitz code calls (`bbLoadTexture`, `_bbStrConcat`…) | Blitz3D `bbruntime/`, ported |
| `runtime/bbruntime/bbapi.h` | **Generated** C++ declarations of all 536 commands (`tools/gen_bbapi.py`) | generated |
| `runtime/blitz3d/` | Scene graph : entities, meshes, cameras, lights, B3D/3DS/MD2 loaders, collisions, animation | Blitz3D `blitz3d/`, ported |
| `runtime/gxruntime/` | Wii U platform layer, same classes as the original `gxruntime` (see below) | new |
| `runtime/stdutil/`, `runtime/config/` | Blitz3D helpers (`ftoa`, string utils) | Blitz3D, ported |
| `runtime/thirdparty/stb/` | stb_image (PNG/JPG/BMP/TGA), stb_truetype (fonts), stb_vorbis (OGG, for audio later) | public domain |
| `shaders/` | `scene.*` : D3D7 fixed-function emulation; `canvas2d.*` : 2D drawing and presenting | new |
| `tools/` | `build_errors.ps1`, `cemu_run.ps1`, `gen_bbapi.py` (see BUILDING.md) | new |

Changes made to the original Blitz3D sources are marked with `Wii U port` comments. Files of the original release
that the port doesn't use (DirectPlay, DLL loading, unused model formats) were moved to `_ref/unused_blitz3d/`.

## runtime/gxruntime (Wii U back end)

| File | Class | Notes |
|---|---|---|
| `gxruntime.cpp` | `gxRuntime` | Startup (logging, AX, GX2), `MilliSecs`, factories. `idle()` asks `wiiu_app` whether to go on (pauses in the background) and polls input |
| `gxgraphics.cpp` | `gxGraphics` | Back buffer (render target at the game's resolution), 2D batcher, present (letterboxed to TV 1920×1080 / GamePad 854×480), image loading, fonts |
| `gxcanvas.cpp` | `gxCanvas` | Images / textures / back buffer. CPU ARGB copy for software 2D + pixel access, tiled GX2 texture for the GPU (see "Canvases") |
| `gxscene.cpp` | `gxScene` | DirectX 7 render state → uniforms of one uber-shader; blend / depth / cull state; multi-pass for > 4 texture stages |
| `gxmesh.cpp` | `gxMesh` | Vertex (48-byte `dxVertex`) + 16-bit index buffers in GPU memory, renamed on `lock()` if the GPU may still read them |
| `gxlight.cpp` | `gxLight` | D3D7 light parameters, evaluated per vertex in `shaders/scene.vert` |
| `gxfont.cpp` | `gxFont` | stb_truetype → 1-bit glyph atlas (Blitz fonts are not antialiased); "Courier"/"Blitz" map to the game's Courier New |
| `gxinput.cpp` | `gxInput` | GamePad → Blitz keyboard (DirectInput scan codes) / mouse / joystick. Mapping table at the top of the file |
| `gxfilesystem.cpp` | `gxFileSystem` | Path mapping (see "Files") |
| `gxaudio.cpp` | `gxAudio` | **Placeholder** (sounds load as 0) until the mixer is written |
| `gxmisc.cpp` | `gxDir`, `gxTimer`, `gxMovie` | Movies not supported (OpenMovie returns 0) |
| `imageload.cpp` | | stb_image + Blitz's alpha / mask / power-of-two rules (`ddUtil::loadSurface`) |
| `wiiu_gpu.cpp` | | GPU memory, frame fences (`gpuFrameBusy`), per-frame transient memory, shader loading |
| `wiiu_app.cpp` | | App lifecycle : ProcUI on the main thread, game thread (8 MB stack), pause for the HOME Menu, exit (BUILDING.md "Hardware") |
| `wiiu_debug.cpp` | | Log file, crash screen, hang watchdog, periodic report hook |
| `wiiu_prof.cpp` | | Function timers (`BB_PROF_SCOPE`) and their report in `profile.txt` |

### Canvases

- Each canvas may have a **CPU copy** (`pixels`, ARGB) and a **GPU texture** (tiled, `GX2_TILE_MODE_DEFAULT`,
  RGBA8, usable as a colour buffer). CPU changes are uploaded through a linear staging surface +
  `GX2CopySurface`, one call per mip level (mips are box-filtered on the CPU), so tiling and mip addressing are
  GX2's job.
- 2D drawing onto the **back buffer** is done on the GPU (batched quads, point sampling, mask colour via
  `discard`); onto any other canvas it's done in **software** on the CPU copy, like DirectDraw blits were. This
  matches Blitz's exact 2D results and avoids reading GPU memory back (which Cemu doesn't reflect in CPU memory).
- `CopyRect` / `blit` involving the back buffer is a GPU draw; the destination becomes "GPU-dirty" and a later
  CPU access reads it back (`GX2CopySurface` tiled → linear + `GX2DrawDone`). Works on hardware; Cemu may return
  stale data.
- `LoadTexture` textures drop their CPU copy after upload (memory).
- Blitz's 2D colour commands write alpha 0, reads from surfaces without an alpha channel return alpha 0xFF, and
  textures without the alpha flag sample with alpha 1 — all as in DirectX 7 Blitz.

### GPU state

- `gxGraphics` renders offscreen with **its own `GX2ContextState`**. GX2 context states shadow register writes,
  including the bound colour buffer : rendering offscreen with WHBGfx's TV state active made WHBGfx present into
  our back buffer (black screen). WHBGfx's states are only used inside `flip()`.
- **CPU cache vs GPU writes** : `gpuAlloc` flushes the new block from the CPU data cache. Heap memory can still
  hold dirty lines from its previous use; when the GPU writes the memory (texture copies, render targets) and
  the CPU later evicts those lines, they overwrite the GPU's data (garbage blocks, black images). Cemu has no
  cache, so only hardware shows it. CPU writes the GPU reads are flushed with `GX2Invalidate(... CPU ...)`;
  read-backs invalidate first (`gpuInvalidateCpu`).
- **GPU colour cache vs textures** : a canvas the GPU drew into (2D batch, camera render, clear) is marked
  `gpu_drawn`; before it is sampled as a texture, `gxGraphics::syncTexture` (called from `gxCanvas::getTexture`)
  draws its pending 2D batch and flushes the colour cache (`GX2Invalidate(COLOR_BUFFER|TEXTURE)`). Without it the
  texture unit reads memory the render hasn't reached yet. Cemu doesn't emulate these caches. (It did not fix the
  black document images : `ResizeImage2` was then changed to a CPU resize, see "Performance".)
- `flip()` uses WHBGfx's begin functions (`WHBGfxBeginRender` waits until the previous frame is shown, so at most
  one frame is queued) but not `WHBGfxFinishRender`, which ends with `GX2DrawDone` and so keeps the CPU waiting
  for the GPU every frame. It swaps and flushes itself; memory the GPU may still read is protected by the frame
  fences (`gpuEndFrame`, `gpuFrameBusy`, deferred frees, transient chunks).
- `gxGraphics::stateEpoch()` changes whenever something other than the scene touched GPU state (2D batch, clears,
  copies, present); the scene re-applies its shader and raster state when it sees a new epoch.
- Fast clears (`GX2ClearColor` / `GX2ClearDepthStencilEx`) reset the context state; partial clears (camera
  viewports) draw a quad.

### Files

- Blitz paths are relative, use backslashes and are case-insensitive. `gxFileSystem::mapPath` resolves each
  component case-insensitively (directory listings cached).
- **On hardware, `/vol/content` file names are case-sensitive and the folder can't be listed** (opendir fails
  under Aroma; Cemu on Windows hides both). So the build writes `content/filelist.txt` (every file and folder,
  `tools/gen_filelist.cmake`, last step of the `assets` target) and the runtime answers listings and `FileType`
  for the content from it. Paths that resolve nowhere are logged (`gxFileSystem: not found`, first 200); a few
  are normal (Blitz looks for model textures in several places, some textures are missing from the game data).
- Reads look in the writable overlay `sd:/wiiu/apps/scpcb/` first, then the read-only `.wuhb` content
  (`fs:/vol/content/`). Writes go to the overlay. `OpenFile` (read/write) opens the file read-only and only on the first
  write copies it to the overlay (if it's in the content) and reopens it read/write : the Wii U file system refuses
  to open a file that's open for writing, and the game reads `loadingscreens.ini` with `OpenFile` and `ReadFile`
  at the same time (crash on hardware, fine on Windows / Cemu). Failed opens are logged (`file: can't open`).
- File streams use a 64 KB buffer; the B3D loader reads the whole file into memory (many small reads are slow).

### Performance (CPU-bound on the console)

The Espresso (3 × 1.24 GHz PowerPC 750) is the limit, not the GPU. Changes, measured with the function timers
(BUILDING.md "Hardware") :

- **No hardware square root** : the 750 has no `fsqrt`, and `sqrtf` is newlib's bit-by-bit software routine.
  `fsqrt()` in `blitz3d/geom.h` uses the reciprocal square root estimate (`frsqrte`) + 3 Newton-Raphson steps
  (full float precision). Used by vector length / normalisation and the collision maths.
- **Collisions** (`blitz3d/world.cpp`, `collision.cpp`) :
  - each mesh caches its world bounding box (`Object::worldCollideBounds`, keyed on its transform and local box),
    and a swept sphere skips meshes whose box it doesn't touch, before the exact test (which inverts the mesh's
    transform). Also used by picks and `EntityVisible`;
  - `triangleCollide` rejects a triangle when the sphere starts more than its radius behind the triangle's plane
    (it moves away from it, so it can't touch the triangle or its edges), before building the 3 edge planes.
  In the first cell one NPC collider is pushed into a dense prop every update (≈ 5500 triangle tests) : the
  game's own behaviour, which a PC absorbs.
- **Mesh buffers rewritten every frame** (skinned meshes, sprites) : `gxMesh::lock` replaces buffers the GPU may
  still read; the replaced pair is kept (up to 3) and reused once the GPU is done, instead of a new `memalign`
  + cache flush each time. Sprites have one 4-vertex mesh each : the original shared mesh made every visible
  sprite copy and flush the whole shared buffer every frame (≈ 15 ms per frame on the console, nothing in Cemu
  where cache flushes are free).
- **Culling** (`MeshModel::render`) : the model's box corners go to world space and are tested against the
  camera's world frustum, instead of building a model-space frustum per model (inverse, 9 transforms, 6 planes).
- **Entity lists** : `World` keeps its enabled / visible object lists until the entity tree changes
  (`Entity::treeGen()` : creation, deletion, re-parenting, show / hide, enable / disable). Picks and
  `EntityVisible` rebuilt the enabled list on every call (≈ 16 ms each on the console during loading).
- **Game code** (`;Wii U port:` in `game/src`) :
  - `ReadINILine` copies the line out of the bank with `WiiU_PeekString` (`game/src/wiiu.decls`, implemented in
    `runtime/userlibs/userlibs_stubs.cpp`) instead of `retStr=retStr+Chr(byte)` per character (≈ 1 s per frame
    of INI lookups on the menu);
  - `ResizeImage2` uses `ResizeImage` (bilinear, on the CPU; `tformCanvas` has a fast path for plain scaling)
    instead of copying into a 2048² texture, rendering it with a camera and copying the back buffer back (a 16 MB
    texture upload per call, and black document images on the console).
- **Log** : buffered, written to the card once a second (BUILDING.md "Hardware").

### Endianness (big-endian PowerPC)

- Blitz streams (`ReadInt`/`ReadFloat`/`ReadShort`/`WriteInt`…) and banks (`PeekInt`/`PokeFloat`…) are
  little-endian, as on x86 (`runtime/bbruntime/endian.h`). `.rmesh` rooms and saves rely on this.
- The B3D loader byte-swaps everything except chunk tags (compared as 4-char constants like `'BB3D'`).
- MD2, 3DS and Q3 BSP loaders are **not** endian-fixed (SCP:CB doesn't use these formats).
- `.x` models (55 text + 1 compressed binary in SCP:CB) need a new loader : the original used DirectX's
  `IDirectXFile` (`loader_x.cpp` is excluded from the build for now).

## GX2 conventions (verified in Cemu)

| Topic | Finding |
|---|---|
| NDC orientation | +Y is the top of the screen, like Direct3D. Blitz's D3D-style projection needs no Y flip |
| Vertex colour | Blitz stores `0xAARRGGBB` as a 32-bit int. On big-endian that's bytes A,R,G,B in memory; read it as `GX2_ATTRIB_FORMAT_UNORM_8_8_8_8` and swizzle `.yzwx` in the shader to get RGBA |
| Indices | `GX2_INDEX_TYPE_U16` = big-endian u16 (native CPU order) |
| Shaders | CafeGLSL `--uniform-mode register`; uniforms set with `GX2SetVertexUniformReg(var->offset, ...)` |
| Viewport | The TV colour buffer is 1920×1080 in Cemu; always set viewport + scissor explicitly |
| Culling | D3D culls counter-clockwise : `GX2SetCullOnlyControl(GX2_FRONT_FACE_CW, FALSE, TRUE)`; verified with rooms (walls are visible from inside) |
| Depth range | `GX2SetRasterizerClipControlHalfZ(TRUE, TRUE, TRUE)` for D3D's 0..1 clip Z |
| Context states | Use a separate `GX2ContextState` for offscreen rendering (see "GPU state") |
| TLS | `elf2rpl` can't relocate thread-local storage : stb_image needs `STBI_NO_THREAD_LOCALS` |

## Transpiler (`tools/bb2cpp`)

The original Blitz3D compiler front end (tokenizer, parser, semantic pass from `_ref/blitz3d-runtime/compiler/`,
lightly patched to build with MinGW-w64 g++ : `GetFullPathName` → `std::filesystem`, x87 asm → `lrintf`,
MSVC for-scope) plus a C++ emitter (`cppgen.cpp`) that mirrors each node's `translate()`, so the generated code
makes the **same runtime calls** as the original x86 output. `main.cpp` builds the runtime environment the way
`blitz/libs.cpp` did : runtime commands from `tools/bbapi.txt` (generated from the runtime's `rtSym` tables) and
userlib functions from the `.decls` files.

Output (`build/game/`) : `bbgame.h` (declarations), `bbgame_data.cpp` (type descriptors, globals, Dim arrays, Data
table), `bbgame_main.cpp` (`bbMain()` = the main program), `bbgame_funcs_<n>.cpp` (user functions, 40 per file).
Runtime commands take C++ pointer types (`Entity*`...) while Blitz handles are ints : calls cast with
`(T*)(intptr_t)` / `(int)(intptr_t)` (32-bit pointers, single inheritance, so the values are the same as on x86).
Userlib calls go to `_f<name>(...)` with `int` / `float` / `const char*` parameters (strings through
`_bbStrToCStr`, objects as their field pointer, banks for `*` parameters as their data pointer).

| Blitz | C++ |
|---|---|
| `%` / `#` / `$` / object | `int` / `float` / `BBStr*` (owned temporaries, consumed by the callee, as in the original ABI) / `BBObj*` |
| String ops | `_bbStrConcat`, `_bbStrLoad`, `_bbStrStore`, `_bbStrCompare`… (same calls as the x86 code) |
| `Type` | static `BBObjType` descriptor (same layout as the original data section); `New` = `_bbObjNew`; fields = `obj->fields[k]` |
| `Dim` arrays | `BBArray` descriptors + `_bbDimArray` |
| `Data` / `Read` / `Restore` | static `BBData` table + `_bbRestore` / `_bbReadInt`… |
| `For` | `To` re-evaluated every iteration, `Step` constant, exactly like `ForNode::translate` |
| `Select` | ordered comparison chain on a temporary |
| `Goto` | C++ `goto` (all locals declared at the top of the function, so no jump skips an initialisation) |
| Float → int | round to nearest (x87 `fistp`), not truncation |
| `Shl`/`Shr`/`Sar` | shift count masked to 5 bits (x86) |
| Argument / operand evaluation | left to right (GCC statement expressions with temporaries when needed) |
| Compile flags | `-fwrapv` (int overflow wraps), `-ffp-contract=off` (no fused multiply-add) |
