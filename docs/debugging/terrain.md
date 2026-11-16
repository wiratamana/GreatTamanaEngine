# Terrain: debug and benchmark routes
> The streamed procedural world: how to open it, change it, fly over it and read its cost over HTTP. For anyone who tests or benchmarks the terrain.
> Verified against a live run: 2026-11-16 (Intel Iris Xe, Game view 719x359 and 1280x720)

## TL;DR
- The terrain is one mega-mesh with one cluster per node. `GpuDrivenScene` culls it (frustum, optional Hi-Z) and draws it with one indirect draw.
- Open it with `GET /terrain/open`. It replaces the current scene. Close it with `GET /terrain/close`.
- All settings change live with `GET /terrain/set?key=value&...`. Height changes rebuild every node.
- Read everything with `GET /terrain/state`. Look at the world with `GET /get_game_view` after `GET /activate_tab?name=Game`.
- Move the camera with `GET /terrain/flight` (repeatable) or `POST /set_entity_trs` with `"name":"Terrain Camera"`.
- Code: `src/Terrain/` (pure CPU), `src/Editor/Terrain/` (GPU pool, world, commands), `src/Network/NetworkTerrainRoutes.*`.

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
| `/terrain/save?path=C:/x/world.gta` | Bakes `bakeNodesPerSide` squared finest nodes around the camera and writes the `.gta`. |
| `/terrain/load?path=...` | Reads a baked file, applies its settings, serves nodes inside its region from the file. |

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
- No water waves, no shore blend, no triplanar texturing, no LOD morphing (levels pop). Erosion strength above 2 shows a grid at node borders.
- `/terrain/map` (top-down image) does not exist yet. Use `/get_game_view`.

## Cost summary
Measured on this iGPU with `--frames-in-flight=1` (serial), lean profile with budget, `lodDistanceFactor` 2.0, about 0.9 M triangles in view. The default overlap mode removes most of the CPU part of the frame: in a 1280x720 window (Game view 719x360, Scene panel hidden) the fixed view went from 17.6 to 15.3 ms mean frame time and the flight from 19.1 to 16.3 ms. The GPU part is unchanged, so it is the floor.

| View size | Frame | GPU (Game view) | Terrain colour pass | Shadow passes | Atmosphere |
|---|---|---|---|---|---|
| 719x359 | 15 to 17 ms | 10 to 11 ms | 2.0 ms | 2.4 ms | 4.5 ms |
| 1280x720 | 22 to 27 ms | 16 to 19 ms | 2.9 ms | 2.4 ms | 5.3 ms |

Terrain alone (`geometry_only`): 7.4 ms frame at 719x359, 12 ms at 1280x720.
Cost grows linearly with triangles, about 3 ms per million (draw plus shadow). View distance 4 km to 20 km only adds about 0.4 M triangles.

## Related
- [HTTP routes (debug)](http-routes-debug.md), [HTTP cheat sheet](http-cheat-sheet.md), [Profiling](profiling.md)
- [GPU-driven scene](../reference/render-features/gpu-driven-scene.md), [GPU-driven rendering](../architecture/gpu-driven-rendering.md)
