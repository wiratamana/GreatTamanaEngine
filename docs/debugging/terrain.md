# Terrain: debug and benchmark routes
> The streamed procedural world: how to open it, change it, fly over it and read its cost over HTTP. For anyone who tests or benchmarks the terrain.
> Verified against a live run: 2026-11-16 (Intel Iris Xe, Game view 719x359 and 1280x720)

## TL;DR
- The terrain is one mega-mesh with one cluster per node. `GpuDrivenScene` culls it (frustum, optional Hi-Z) and draws it with one indirect draw.
- Open it with `GET /terrain/open`. It replaces the current scene. Close it with `GET /terrain/close`.
- All settings change live with `GET /terrain/set?key=value&...`. Height changes rebuild every node.
- Read everything with `GET /terrain/state`. Look at the world with `GET /get_game_view` after `GET /activate_tab?name=Game`.
- Move the camera with the free-fly or orbit mode (`GET /terrain/cam`), `GET /terrain/flight` (repeatable) or `POST /set_entity_trs` with `"name":"Terrain Camera"`.
- Everything is also a panel: Window > Terrain (sliders, live preview, save and load, bounding boxes, stats). See [Terrain panel](#terrain-panel-window--terrain).
- Code: `src/Features/Terrain/` (pure CPU), `src/Features/Terrain/Editor/` (GPU pool, world, commands), `src/Network/NetworkTerrainRoutes.*`.

## Routes
All routes are `GET`. Routes that change something refuse browser requests (`403`). `state`, `settings` read only.

| Route | What it does |
|---|---|
| `/terrain/open?profile=lean&poolSlots=0&water=true&budget=true` | Builds the scene (camera, sun, terrain, water). `profile`: `full`, `lean`, `geometry_only`. `budget=true` also lowers shadow and aerial perspective quality (not with `full`). |
| `/terrain/close` | Removes the terrain and restores the features and settings it changed. |
| `/terrain/state` | Stats (see below), `runtime` options and every `settings` field as one JSON object. |
| `/terrain/settings` | Only the settings object. |
| `/terrain/set?k=v&k=v` | Applies settings, runtime options and flags in one call. Unknown key or bad value: `400`, nothing changes. |
| `/terrain/flight?kind=orbit\|line&x=&z=&heading=&radius=&speed=&alt=&minAlt=&lookDown=&fixedStep=&duration=` | Scripted camera. `alt` is metres above ground. `speed` 0.5 is a fixed spot. Positive `lookDown` looks toward the ground. |
| `/terrain/stop_flight` | Stops the flight. The camera stays. |
| `/terrain/save?path=C:/x/world.gta` | Starts a background bake of `bakeNodesPerSide` squared finest nodes around the camera and writes the `.gta`. Returns at once. Watch `assetBusy`, `assetProgress` and `assetResult` in `/terrain/state`. |
| `/terrain/load?path=...` | Starts a background read of a baked file. When it finishes, the file's settings are applied and nodes inside its region come from the file. Same `asset*` state keys. |
| `/terrain/map?size=768&range=&mode=lod\|state&frustum=true` | PNG top-down map of the resident nodes, the camera and its view wedge. See [Map picture](#map-picture). |
| `/terrain/cam?mode=script\|free\|orbit&x=&y=&alt=&z=&yaw=&pitch=&tx=&tz=&dist=` | Camera mode and placement (`alt` is metres above ground, `yaw` 0 looks along +Z, `pitch` positive looks down). See [Camera](#camera). |
| `/terrain/cam_input?fwd=&right=&up=&lookx=&looky=&wheel=&boost=&look=&frames=` | Repeats one frame of mouse and key input for `frames` frames (free or orbit mode). |

Errors: `400` bad key or value, `404` unknown action, `409` the world refused (for example "open the terrain before saving it"), `503` another terrain command is running, `504` main thread too slow.

### `set` keys
- Every `TerrainSettings` field by its C++ name, for example `seed`, `heightScale`, `baseFrequency`, `domainWarpStrength`, `octaves`, `gain`, `lacunarity`, `erosionEnabled`, `erosionStrength`, `grassMaxSlopeDeg`, `snowMinHeightFraction`, `chunkQuads`, `lod0Spacing`, `lodDistanceFactor`, `frustumCulling`, `hiZCulling`, `maxViewDistance`. The full list is in `GET /terrain/settings`.
- Runtime options: `maxVisibleNodes`, `maxUploadsPerTick`, `streamingMaxInFlight`, `unloadDelayFrames`, `bakeNodesPerSide`, `collectGpuStats`.
- Flags: `livePreview=true|false` (one 129x129 chunk, rebuilt on every change), `debugAabb`, `previewCenterX`, `previewCenterZ`, `previewSpacing`.

What a change costs:
| Change | Effect |
|---|---|
| Height fields (`seed`, noise, erosion) | Every node is dropped and rebuilt on the job system. About 60 nodes per second. |
| Geometry (`chunkQuads`, `lod0Spacing`, `lodLevelCount`, `worldHalfExtentChunks`) | The scene is closed and reopened, the camera pose is kept. The flight stops. |
| Material (slope and height thresholds) | Nodes are recoloured. The colour ramp is rebuilt 0.3 s after the last change. |
| Optimization (`lodDistanceFactor`, `maxViewDistance`, culling flags) | Nothing is rebuilt. |

### Culling flags
`frustumCulling=false` gives `NoCull`: the culling feature draws every pool slot, including stale ones. Use it only to see the cost of culling. `frustumCulling=true&hiZCulling=false` gives `Frustum`. Both true gives `HiZ`.

## `state` keys
Streaming: `nodesWanted`, `nodesInView`, `nodesResident`, `nodesPending`, `uploadQueue`, `nodesWrittenThisTick`, `builtTotal`, `evictedTotal`, `generationMsAverage`, `generationMsMax`, `lodDistanceScale` (below 1 means the visible cap lowered the detail).
Pool: `slotsTotal`, `slotsUsed`, `slotsEnabled`, `trianglesEnabled`, `residentMbCpu`, `residentMbGpu`.
Main thread: `tickCpuMs`, `tickCpuMsMax`, `selectMs`.
GPU culling (about two frames old): `gpuCullMode`, `gpuOcclusion`, `gpuDrawnNodes`, `gpuFrustumCulledNodes`, `gpuHiZCulledNodes`, `gpuTrianglesDrawn`, `gpuPathActive`.
`lastError` is empty when all is well.

`nodesResident` is larger than `nodesWanted`: nodes stay `unloadDelayFrames` frames after the camera leaves them.

## Terrain panel (Window > Terrain)
A dockable window (closed by default) with every setting as a slider, grouped like the generator: seed, height and frequency, domain warp, octaves, gain and lacunarity, continentalness, peaks and valleys, detail, erosion, materials, geometry and LOD, optimization (frustum culling, Hi-Z culling, max view distance).
- **Open terrain world** builds the scene from the sliders and switches the camera to free-fly. **Close** removes it.
- **Live preview mode**: one 129x129 chunk is rebuilt on every slider move (about 20 ms). The big world is not rebuilt while it is on.
- Without the preview, a height or erosion change reaches the streamed world when the slider is released or has rested 0.35 s. A geometry change (`chunkQuads`, spacing, levels, half world) reopens the scene and waits for the release. Material and optimization changes apply at once.
- **Randomize seed**, **Reset defaults**, streaming sliders (visible cap, uploads per frame, jobs).
- **Save Terrain Asset** and **Load Terrain Asset** with a path field (default `<exe dir>/Project/terrain_world.gta`). Both run on a background thread: the panel shows `Baking and saving ... NN%` and the editor keeps drawing. Save bakes `Bake nodes per side` squared nodes (256 nodes: 10.8 MB; 1024 nodes: 43 MB in 1.4 s, a bit slower frames while it runs). Load of 1024 nodes takes 117 ms. A second save or load is refused while one runs. The whole file is read into memory (no lazy chunk streaming).
- **Show chunk bounding boxes** draws the 12 edges of every resident node over the Game and Scene views. Colour by state (green drawn, red outside the view, grey hidden by a finer or coarser node) or by LOD level.
- Stats: nodes resident, pending, wanted and in view, pool slots, triangles, GPU cull counters (drawn, frustum culled, Hi-Z culled), memory, generation time, tick time, preview time, camera.
- The panel talks to the world only through `TerrainUiBridge` (`src/Features/Terrain/Editor/TerrainUiBridge.h`). The host applies its request at the start of the next frame, so GPU buffers are never touched while the frame is being drawn.

## Camera
Three modes, set in the panel or with `GET /terrain/cam?mode=script|free|orbit`.
| Mode | Control in the Game view |
|---|---|
| `script` | The scripted flight (`/terrain/flight`) or a fixed camera. |
| `free` | Hold the right mouse button and drag to look. W A S D move, E up, Q down, Shift boost, wheel changes the speed. The speed follows the height above ground (0.8 per second of height, 6 to 3500 m/s). The camera never goes below 3 m above the land or the sea. |
| `orbit` | Drag with the left or right button to rotate, wheel to zoom, W A S D move the centre over the ground. |
Input only counts while the cursor is over the Game image (or a drag that started there is held). The Scene view camera is not touched.

Scripted test input goes through the same code as the mouse and keys:
```text
GET /terrain/cam?mode=free&x=-1000&z=-1500&alt=400&yaw=40&pitch=12     place the camera (alt = metres above ground)
GET /terrain/cam_input?fwd=1&boost=true&frames=90                       hold W and Shift for 90 frames
GET /terrain/cam_input?lookx=10&frames=40                               drag the mouse 10 px per frame for 40 frames
```

## Map picture
`GET /terrain/map?size=768&mode=state` returns a PNG (no text, colours carry the meaning). +X is right, +Z is up, the camera is the red dot in the middle and the white lines are its view wedge (left edge, right edge, heading).
- `mode=lod`: one colour per LOD level (top-left strip: level 0 to 11). Darker means lower ground.
- `mode=state`: green drawn and in view, amber resident but outside the view, grey hidden by another node.
- `range` is the half side in metres (default 1.15 times the view distance). The bar at the bottom left is 1 km.

## Recipes
Fixed viewpoint and a picture:
```text
GET /activate_tab?name=Game
GET /terrain/open?profile=lean
GET /terrain/flight?kind=line&x=-1000&z=-1500&heading=40&speed=0.5&alt=400&lookDown=12
GET /get_game_view
```
Cost of one mode, read the GPU rows with the profiler:
```text
GET /terrain/set?hiZCulling=false
GET /profiler/table?view=flat&lane=gpu&limit=256     rows: GpuDrivenScene.L0.P0.Draw, Shadow.Cluster.L*.P0.Draw
GET /profiler/frames?count=120
```
A moving benchmark: `GET /terrain/flight?kind=line&speed=400&alt=250&fixedStep=0.016` and sample `state` every few seconds.

## Measure like this (read this before you trust a number)
- Start the editor with `--no-vsync`, activate the Game tab, and hide the Scene panel (`GET /panel_visible?name=Scene&visible=false`). The Scene view also draws the terrain and adds about 6 ms at 1280x720.
- Always write down the Game view size. The same scene is 11 ms at 719x359 and 17 ms at 1280x720.
- Use interleaved A/B runs and medians of profiler rows. This GPU swings by 1 to 3 ms between runs.
- `NoCull` draws stale slots, so its numbers are not a real baseline.

## CPU-GPU overlap (the default)
- The editor runs with `--frames-in-flight=2` by default: the CPU builds frame N+1 while the GPU draws frame N. The terrain pool is written only into slots that no frame in flight reads, and the cluster table is swapped (not patched) while frames overlap.
- Serial A/B: start the editor once with `--frames-in-flight=1` and once without the flag. The mode is a process flag, so alternate launches (at least 3 per mode) and compare medians of `GET /profiler/frames`.
- Judge speed by the `cpu` (frame) metric. With overlap the per-pass GPU rows and `gpu_span` describe an older frame, and `cpu_busy` is not reliable in GPU-bound scenes.
- The state counters `gpu*` in `GET /terrain/state` are two or more frames old in both modes.
- Expect more frame-time spread with overlap (short frame, long frame, short frame). The mean and median get better when the CPU and GPU parts are both large. A scene that is fully GPU-bound gains nothing.

## Known limits
- Hi-Z saves triangles only when mountains hide other mountains (57 of 375 nodes in the best test) and it does not help the shadow passes. It costs about 0.3 ms. Frustum culling is the real saver.
- The shadow passes cost more than the terrain colour pass. The `budget` option lowers them. See [Terrain performance notes](#cost-summary).
- Water has its own feature now (waves, shore, bodies, carve; see [Water world](water-world.md)). Still missing: triplanar texturing and LOD morphing (levels pop). Erosion strength above 2 shows a grid at node borders. The pop is small: at the split distance the p99 height step between a parent and its children is 1 to 2.7 px at 720p, the worst vertex 4 to 10 px (aliased peaks). A real fix is vertex morphing, which needs a new cluster shader (about 2 days, medium risk). Run `TerrainBench.DISABLED_LodPopError` to measure it.
- No in-world wireframe boxes: the engine has no scene line pipeline. The boxes are an ImGui overlay (Game and Scene views, not in `/get_game_view`; `/get_swapchain` shows them). Use `/terrain/map` for a capture that always works.

## Cost summary
Measured on this iGPU with `--frames-in-flight=1` (serial), lean profile with budget, `lodDistanceFactor` 2.0, about 0.9 M triangles in view. The default overlap mode removes most of the CPU part of the frame: in a 1280x720 window (Game view 719x360, Scene panel hidden) the fixed view went from 17.6 to 15.3 ms mean frame time and the flight from 19.1 to 16.3 ms. The GPU part is unchanged, so it is the floor.

| View size | Frame | GPU (Game view) | Terrain colour pass | Shadow passes | Atmosphere |
|---|---|---|---|---|---|
| 719x359 | 15 to 17 ms | 10 to 11 ms | 2.0 ms | 2.4 ms | 4.5 ms |
| 1280x720 | 22 to 27 ms | 16 to 19 ms | 2.9 ms | 2.4 ms | 5.3 ms |

Terrain alone (`geometry_only`): 7.4 ms frame at 719x359, 12 ms at 1280x720.
Cost grows linearly with triangles, about 3 ms per million (draw plus shadow). View distance 4 km to 20 km only adds about 0.4 M triangles.

## Recommended settings for an Intel iGPU
Measured A/B (3 launches each, 1280x720 window, Game view 719x360, overlap on). Before = shadow `maxDistance` 1500 and aerial volume 128x128x32. After = 800 and 96x54x32.

| View | Frame before | Frame after | GPU before | GPU after |
|---|---|---|---|---|
| high (400 m, look down) | 13.8 ms | 12.9 ms | 10.8 ms | 9.8 ms |
| low (40 m) | 14.1 ms | 11.8 ms | 10.9 ms | 8.8 ms |

- Open with `profile=lean&budget=true` (2 cascades, 1024 map, shadow distance 800, 4 aerial samples), `lodDistanceFactor` 2.0, `maxVisibleNodes` 384.
- What still costs most (GPU ms, 719x360): aerial volume 1.2, atmosphere LUTs 1.6, terrain colour pass 1.9, shadow cascades 2.0, TAA 0.5.
- Tried and not worth it: `cascadeCount` 1 (saves 1.2 to 1.7 ms but the near shadows get coarse), Bloom off (0.3 ms), 512 shadow map (0.4 ms, within noise), a 32x32 multi-scattering LUT (slower: 3.1 ms against 1.2 ms).
- The memory type of the pool vertex buffer was not measured. On this shared-memory GPU the host-visible and device-local heaps are the same RAM, so no gain is expected from a device-local copy.
- Water: open with `water=false` (the flat quad is an occluder trick that the Water feature replaces). Keep `cullDeepSeabed` on, keep SSR off, and let half resolution switch on by itself above 600000 view pixels. Cost: 0.3 to 1.2 ms on top of the terrain, and open-ocean views get cheaper. See [Water world](water-world.md).

## Related
- [HTTP routes (debug)](http-routes-debug.md), [HTTP cheat sheet](http-cheat-sheet.md), [Profiling](profiling.md)
- [GPU-driven scene](../reference/render-features/gpu-driven-scene.md), [GPU-driven rendering](../architecture/gpu-driven-rendering.md)
