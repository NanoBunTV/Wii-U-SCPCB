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
| `gxaudio.cpp` | `gxAudio` | Sounds and streams on AX voices (see "Audio"); `wiiu_vorbis.cpp` compiles stb_vorbis |
| `gxmisc.cpp` | `gxDir`, `gxTimer`, `gxMovie` | Movies not supported (OpenMovie returns 0) |
| `imageload.cpp` | | stb_image + Blitz's alpha / mask / power-of-two rules (`ddUtil::loadSurface`) |
| `wiiu_gpu.cpp` | | GPU memory, frame fences (`gpuFrameBusy`), per-frame transient memory, shader loading |
| `wiiu_app.cpp` | | App lifecycle : ProcUI on the main thread, game thread (8 MB stack), pause for the HOME Menu, exit (BUILDING.md "Hardware") |
| `wiiu_debug.cpp` | | Log file, crash screen, hang watchdog, periodic report hook |
| `wiiu_prof.cpp` | | Function timers (`BB_PROF_SCOPE`) and their report in `profile.txt` |
| `wiiu_keyboard.cpp` | | System software keyboard (nn::swkbd) for text boxes (see "Menus, touch and text entry") |

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
- **CPU copies only when needed** (memory) : `Cls` of a whole image (and `CreateImage` / `CreateTexture`, which
  clear) frees the CPU copy and only remembers the colour (`lazy_clear`); `cpuPixels()` makes the copy again
  when the CPU needs it, an upload fills a temporary one. After the GPU draws into an image (`CopyRect` from the
  back buffer, camera renders), the stale CPU copy is freed (`gpuModified`) and a later CPU access reads the
  texture back. The game's 2048x2048 screen-copy textures (gamma / screen scaling, blur) kept 16 MB of CPU memory
  each for nothing. `ReadPixelFast` / `WritePixelFast` without `LockBuffer` make the copy instead of crashing.
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
- **Compressed content** : the console reads its SD card at ≈ 12 MB/s, and a new game read ≈ 135 MB (textures,
  room meshes, models), ≈ 11 s of loading. The build packs `build/content_wuhb/` (`tools/pack_content.py`) : the
  same files as `build/content/` (hard links), but `.wbc`, `.wbc3`, `.rmesh` and `.b3d` zstd-compressed (level 19)
  under their own names. `readWholeFile` sees the zstd frame magic and decompresses (devkitPro's `ppc-zstd`);
  Blitz `ReadFile` on those types reads the whole file through it and serves it from memory (`MemBuf` in
  `bbfilesystem.cpp`). A new game now reads ≈ 46-53 MB; decompressing ≈ 130 MB takes ≈ 1 s in Cemu but
  ≈ 3.5 s on the console (≈ 38 MB/s), which eats most of the gain (reads 11 → 5.9 s). `FileSize` /
  `CopyFile` of those files see the compressed bytes (the game doesn't use them on these types).

### Performance

The Espresso (3 × 1.24 GHz PowerPC 750) was the limit. With the changes below the game's CPU work in the first
cell is ≈ 14-18 ms per frame on the console, and the GPU became the limit : the `perf:` log line's "GPU after
submit" sample showed ≈ 23-26 ms of GPU work per frame (≈ 5 ms even on the 2D main menu). Changes, measured with
the function timers and the log (BUILDING.md "Hardware") :

- **GPU time per part** (`GPU parts` log line, BUILDING.md) in the cell : 3D scene ≈ 16-17 ms, TV copy (to
  1920x1080) ≈ 3.1 ms, GamePad copy ≈ 0.7 ms, 2D ≈ 0.6 ms. On the 2D menu ≈ 4.5 ms, almost all the copies.
  After the pixel shader variants and skipping the clears : 3D scene ≈ 6 ms, TV copy ≈ 2.1 ms, total ≈ 9 ms
  (the game then runs at ≈ 55-58 fps in the cell, CPU-bound again).
- **Pixel shader variants** (`shaders/scene.frag`, `SCENE_VARIANTS` in CMakeLists.txt, `gxScene::pickVariant`) :
  the general scene pixel shader branches on each of the 4 texture stages' blend op per pixel, and fetched textures
  even for ops without effect (blend 6 is FastExt's bump mode, which this renderer ignores). CMake builds variants
  of the same file with the ops fixed (`#define OP0..OP3`, `scene_<ops>.gsh`) : no branches, no fetch for unused
  stages. Variants for the states that cover almost all triangles (counted in Cemu over 20000 draws) : `5635`
  and `5350` (room meshes : multiply 2x / add / bump), `2500`, `2000`, `0000`. A variant is used when the first
  pass's ops match and alpha test is off; otherwise (and for extra passes) the general shader. Pixel shader
  uniform / sampler locations are looked up per variant; vertex shader uniform locations must match the general
  shader's (checked at load). `sd:/wiiu/apps/scpcb/no_shader_variants.txt` turns them off for comparisons
  (Cemu screenshots with and without are the same).
- **No clear before the TV / GamePad copies** when the back buffer's aspect ratio matches the target's (within
  0.5 %, then it is stretched over the whole target) : the copy covers every pixel.
- **Back buffer in MEM1** (`gxCanvas::placeInMem1`) : the back buffer (where the 3D scene and the 2D drawing
  go) and its depth buffer were in MEM2, the console's main memory, so every pixel written, blended and
  depth-tested went through it. They now sit in MEM1, the GPU's fast 32 MB, taken from WHBGfx's MEM1 heap
  (`GfxHeapAllocMEM1`, exported by libwhb; WHBGfx keeps its own TV / DRC buffers there too). That heap is a frame
  heap that can't free single blocks and is rebuilt every time the app gets the foreground back, so : one block
  is reused by later back buffers of the same or smaller size (`Graphics` then `Graphics3D` at startup), and
  `wiiu::appSetResumeCallback` places the back buffer again (and clears it) after the HOME Menu. If MEM1 is full
  it stays in MEM2 (log line `gxGraphics: back buffer in MEM1 / MEM2`).

- **No hardware square root** : the 750 has no `fsqrt`, and `sqrtf` is newlib's bit-by-bit software routine.
  `fsqrt()` in `blitz3d/geom.h` uses the reciprocal square root estimate (`frsqrte`) + 3 Newton-Raphson steps
  (full float precision). Used by vector length / normalisation and the collision maths.
- **Collisions** (`blitz3d/world.cpp`, `collision.cpp`) :
  - each mesh caches its world bounding box (`Object::worldCollideBounds`, keyed on its transform and local box),
    and a swept sphere skips meshes whose box it doesn't touch, before the exact test (which inverts the mesh's
    transform). Also used by picks and `EntityVisible`;
  - `triangleCollide` rejects a triangle when the sphere starts more than its radius behind the triangle's plane
    (it moves away from it, so it can't touch the triangle or its edges), before building the 3 edge planes.
  - for meshes whose transform is a rotation + uniform scale (rooms, props), the sweep runs in the mesh's own
    space : the line and radius are transformed once, each triangle's plane and 3 edge planes are cached per
    tree leaf (`MeshCollider::leafPlanes`), and the hit normal is turned back to world space.
    `collision_eps` scales the collision epsilon to local units meanwhile. Colliders with different x / y radii
    (the player : 0.15 / 0.30) scale the world in y; when the mesh only turns about the y axis (rooms, props) that
    scale commutes with its rotation, so the sweep runs in local space against the vertices scaled in y (a leaf's
    planes are redone when it is tested with another y scale). Other transforms use the original world-space path;
  - the leaf plane cache is shared by all colliders and bounded (4 MB) : planes for a whole map took ≈ 65 MB
    (64 bytes per triangle and y scale), but only leaves with a triangle near a moving collider are ever tested.
    Past the limit the cache is emptied and refills with the leaves in use (`[collide: leaf planes]` timer :
    ≈ 2-5 leaves per frame in the cell);
  - the collision tree splits at the median with `nth_element` (was a `multimap` sort : seconds of loading);
  - `edgeTest` (sphere against a triangle edge : cylinder + end sphere) works on the line's components
    perpendicular to the edge instead of building an orthonormal basis matrix per edge and moving the line into
    it; same results (the collision counters of a test run are identical);
  - the bounding box used for rejection is the mesh's box, so meshes that are never tested don't build a tree.
  In the first cell one NPC collider is pushed into a dense prop every update (≈ 5500 triangle tests) : the
  game's own behaviour, which a PC absorbs.
- **Skinning on the GPU** (`MeshModel::render`, `Surface::getBindMesh / getSkinBuffer`, `gxScene::setSkin`,
  `shaders/scene.vert`) : Blitz skins boned meshes on the CPU; on the console that cost ≈ 1 µs per vertex
  (≈ 7 ms per frame in the first cell : every vertex read and written through the slow memory). Now the surface
  keeps its bind-pose mesh plus a static buffer of bone indices + weights (attribute buffer 1, 32 bytes per
  vertex), and each draw uploads only the bone matrices (3 rows each, up to 64 bones = 192 uniform registers);
  the vertex shader blends them (`u_skin.x` = 1). Unskinned draws bind a 1-element dummy buffer with stride 0.
  Models with more than 64 bones, or with `sd:/wiiu/apps/scpcb/cpu_skinning.txt` present (for comparisons), use
  the CPU path, which skips re-skinning when the pose number (new only when a bone moved) hasn't changed.
- **Whole-file reads** (`gxFileSystem::readWholeFile`) : images, precompressed textures and `.b3d` files are read
  with one `read` call. stb_image read through stdio 128 bytes at a time, and every file system call is slow on
  the console. The buffers (`FileData`, and the 64 KB buffers of Blitz `ReadFile` streams) are 64-byte aligned :
  wut's file system layer reads straight into aligned memory and goes through a small bounce buffer otherwise.
- **Texture uploads while loading** (`gxGraphics::loadCompressed`) : the linear staging copy of a big mip level
  (over 1 MB) was freed after a full GPU wait per texture; now staging copies pile up to 16 MB and are freed
  after one wait (what is left goes to the deferred free list at the next frame).
- **Long lines through collision trees** (`MeshCollider`, `SegTest`) : the tree walk tests the segment against
  each node box (slab test), not only the segment's bounding box : `EntityVisible` / picks with long diagonal
  lines visited most of a room's tree.
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
- **Picks and `EntityVisible`** (`World::checkLOS`, `traceRay`) : while a map loads ≈ 17000 objects are enabled
  and `InitWayPoints` makes ≈ 4700 `EntityVisible` calls (≈ 2 ms each on the console, ≈ 9 s of loading). Now :
  - `World` keeps the enabled objects that have a pick mode, and those that are obscurers, until the tree or a
    pick mode changes (`Object::pickGen()`) : ≈ 2000-3000 obscurers during loading, ≈ 200 in game;
  - their world boxes sit in one flat array, rebuilt only when an entity moved (`Entity::tformGen()`, bumped
    when a world transform becomes invalid), geometry changed (`Surface::geomGen()`) or a collision radius / box
    changed (`Object::boundsGen()`); each call scans the array with the segment's box and the slab test
    (`SegTest`, now in `collision.h`) and runs the exact test only for the objects the segment passes through.
    Meshes use their cached world box (as before), spheres / boxes a slightly enlarged one; objects without
    bounds always pass. During `InitWayPoints` the array is rebuilt about once per 1000 calls.
  - the log prints counters every 1000 calls (BUILDING.md "Log lines").
- **Game code** (`;Wii U port:` in `game/src`) :
  - `LoadRMesh` (`MapSystem.bb`) reads each mesh's vertices and triangles with `WiiU_ReadRMeshVertices` /
    `WiiU_ReadRMeshTriangles` (`runtime/userlibs/wiiu_rmesh.cpp`) : one read per block and the same vertex /
    triangle data as the original loop of `ReadFloat` / `ReadByte` / `ReadInt` + `AddVertex` /
    `VertexTexCoords` / `VertexColor ...,1.0` / `AddTriangle` (≈ 14 runtime calls per vertex; `LoadRMesh` took
    ≈ 6.5 s of a map's loading on the console). `AddMesh` / `CopyMesh` / `FlipMesh` have timers to see what is left.
    Its collision meshes are made double-sided with `WiiU_MakeDoubleSided` (see "Memory");
  - `ReadINILine` copies the line out of the bank with `WiiU_PeekString` (`game/src/wiiu.decls`, implemented in
    `runtime/userlibs/userlibs_stubs.cpp`) instead of `retStr=retStr+Chr(byte)` per character (≈ 1 s per frame
    of INI lookups on the menu);
  - `ResizeImage2` uses `ResizeImage` (bilinear, on the CPU; `tformCanvas` has a fast path for plain scaling)
    instead of copying into a 2048² texture, rendering it with a camera and copying the back buffer back (a 16 MB
    texture upload per call, and black document images on the console).
- **Log** : buffered, written to the card once a second (BUILDING.md "Hardware").

### Memory

In the game the heap held ≈ 650 MB (Cemu, first cell); now ≈ 420 MB, peak while loading ≈ 420 MB (was ≈ 668).
The `memory :` log lines (BUILDING.md) break it down; what changed :

- **Mesh vertex arrays** (`Surface`, 64 bytes per vertex on the CPU) : ≈ 1/3 of their memory was unused vector
  capacity (one `push_back` at a time doubles it). Bulk adds now reserve first (`Surface::reserveMore`, used by
  `AddMesh` and the RMesh readers; exact growth, or a quarter more for many small adds). 219 → 108 MB with the next
  two points.
- **Double-sided collision without vertex copies** : `LoadRMesh` made each room's collision meshes double-sided with
  `CopyMesh` + `FlipMesh` + `AddMesh`, copying every vertex. `WiiU_MakeDoubleSided` (`wiiu_rmesh.cpp`,
  `Surface::addReversedTriangles`) appends the reversed triangles on the same vertices; collisions only use the
  corners (checked : walking into walls stops, same place reached as with the original code).
- **Unused copies in `LoadRMesh`** : the child mesh of every alpha surface stayed in the world as an invisible
  orphan entity (a full copy of the room's transparent geometry per room template); it's freed after `AddMesh`.
  Its `pinkTexture` was never freed either.
- **Collision planes** : bounded leaf cache instead of planes for every triangle (93 → 31 MB of colliders, see
  "Collisions" above).
- **Images** : CPU copies only when needed (91 → 30 MB, see "Canvases").
- Left (Cemu, in the cell) : textures ≈ 150 MB (GPU; 3 of the RGBA ones are the game's 2048x2048 screen copies,
  16 MB each), surfaces ≈ 108 MB, colliders ≈ 31 MB, images ≈ 30 MB, ≈ 90 MB else (Blitz objects, game stack,
  transient GPU memory...). GPU meshes are made when a surface is first drawn, so they grow while exploring.
  Candidates : vertices without the bone data (64 → 44 bytes) or without a CPU copy once on the GPU (room
  meshes); smaller screen-copy textures (game code change).

### Menus, touch and text entry

- **Bigger menus** (`BeginPortMenu` / `EndPortMenu` in `game/src/Menu.bb`) : SCP:CB sizes its menus with
  `MenuScale = GraphicHeight / 1024` (0.70 at 720p), and on the GamePad (854x480) that made 9-pixel text. The
  main menu and the pause menu now draw at `PortMenuScale` (1.05 at 720p : the tallest sub-screen, load game,
  just fits) with fonts loaded at that size (`MenuFont1 / MenuFont2`) and a pause menu image scaled to it
  (`PortPauseMenuIMG`). The main menu's sub-screens (new game, load game, options, load map) used the left
  third of the screen; they are drawn on the right side instead with `Origin MenuOffsetX, MenuOffsetY`, and
  `ScaledMouseX/Y` (all the menu hit tests) subtract that offset. Tooltips go left of the panel, the delete-save
  question over the list. `EndPortMenu` runs before anything that loads or starts a game (they use `MenuScale`
  and the fonts) and at the end of the menu functions.
- **Options removed** (main and pause menus) : bump mapping (the renderer ignores bump maps; `BumpEnabled` is
  forced off, which also skips loading them), VSync (always on), anti-aliasing, texture LOD bias, "save
  textures in the VRAM", user tracks, frame limit (forced off), antialiased text (forced off : the renderer
  smooths text, and AA text drew with a camera per call), the keyboard bindings (replaced by a GamePad legend,
  `DrawGamePadControls`). Mouse sensitivity / invert / smoothing are labelled "Look".
- **Touch** (`gxinput.cpp`) : while the game shows a pointer (`ShowPointer` : menus, inventory, keypads,
  console), the touch point is the cursor and a touch is a left click; in gameplay (`HidePointer`), a touch
  that starts then is relative : dragging turns the view (0.7 mouse units per touch pixel) and a short tap
  (< 300 ms, < 24 px of movement) is a left click.
- **Text entry** (`wiiu_keyboard.cpp`, `WiiU_TextInput` in `game/src/wiiu.decls`) : tapping a text box (save
  name, map seed, console) opens the system software keyboard (`nn::swkbd`, created on first use : 26 MB of
  work memory from the default heap). It blocks the game : its own loop reads the GamePad, runs swkbd's `Calc`
  and presents frames, the keyboard drawn into the TV / GamePad pictures by `gxGraphics::setPresentHook`. OK
  returns the text (Latin-1), Cancel keeps the old one. In Cemu, Cemu's own keyboard appears instead.
- **Saves** : `Saves\<name>\save.txt` and `options.ini` are written to the writable overlay on the SD card
  (`sd:/wiiu/apps/scpcb/`, see "Files"); quick save (−), Save & Quit and Load Game work in Cemu.
- **App metadata** : title "SCP: Containment Breach", short name "SCP:CB"; Home Menu icon and boot screens from
  `meta/icon.png` and `meta/boot.png` (the owner's images), resized to the Wii U sizes in `meta/icon_128.png`
  (128x128), `meta/boot_tv.png` (1280x720), `meta/boot_drc.png` (854x480) and passed to `wuhbtool`.

### Audio

AX voices (sndcore2) play everything : the console's audio DSP mixes and resamples, so audio costs almost no CPU.

- **Samples** : AX's `LPCM16` voices read 16-bit **big-endian** samples, the CPU's own byte order. stb_vorbis
  (`wiiu_vorbis.cpp`, memory API only) decodes Ogg Vorbis to native shorts, so they are big-endian already (its
  float-to-int trick is byte-order neutral); WAV (RIFF PCM 8 / 16 bit) is little-endian and swapped on load. A
  Cemu run logged plausible statistics for decoded sounds (sign changes between samples 0.3-23 %, not the ≈ 50 %
  of byte-swapped noise) before anyone listened.
- **Sounds** (`LoadSound`) : the whole file is decoded into one 16-bit plane per channel (stereo kept, more channels
  cut to 2), flushed from the CPU cache. A playing sound = 1 voice (mono) or 2 voices (left / right). Each voice
  goes to the TV and the GamePad (device mix channel 0 = left, 1 = right); gains = channel volume × master volume
  × pan (Blitz / FMOD pan -1..1 : the far side fades out, the centre is full volume on both sides). AX resamples
  from the sound's rate (or `ChannelPitch` Hz) to the 48 kHz renderer.
- **Channels** : pooled like Blitz's (`allocSoundChannel`) : `PlaySound` reuses the first channel object that has
  stopped, so the channel handles the game keeps stay valid. A channel's voices are freed when it is found
  stopped. All voices use the same priority and at most `AXGetMaxVoices() - 8` (88) are used, so AX never steals
  one; when none is left `PlaySound` returns 0 (silence).
- **Streams** (FMOD `FSOUND_Stream_*` in `runtime/userlibs/userlibs_stubs.cpp`, and `PlayMusic`) : the Ogg file is
  read whole and decoded into a 16384-frame ring that a looping voice plays; a thread on CPU0 ("scpcb audio
  streams", priority 12) tracks the voice's play position every 5 ms and decodes ahead. Mode bit 2 loops (seek to
  the start at the end); otherwise silence follows and the stream stops when the voice passes the end. FMOD
  channel numbers are ids from 1 (the game treats 0 and -1 as failures); volume / pan are FMOD's 0..255.
- **Background** : `wiiu_app` calls `gxAudio::pause / resume` around the HOME Menu (voices stopped, not freed).
  `gxAudio::shutdown` (stream thread, voices) runs before `AXQuit`, also when the system closes the app.
- Not done : 3D sound (`Load3DSound`, listeners : SCP:CB pans and fades its sounds itself), MP3, CD tracks.

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
