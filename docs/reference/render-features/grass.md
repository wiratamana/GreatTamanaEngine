# Grass (GPU-driven procedural grass)
> Blades are placed, culled and built on the GPU. No mesh asset, no CPU per-blade work. For anyone tuning, extending or debugging grass.
> Verified against code: 2026-11-16

## TL;DR
- A compute pass places blades per 16 m chunk and writes the survivors and three draw commands. Three indirect draws (one per LOD) build each blade from its vertex index.
- Terrain height comes from a **height tile** that the editor layer submits (`GrassFeature::SubmitHeightTile`). Without a tile, nothing is drawn (status `NoTile`).
- Wind is shared with the clouds through the `WindState` channel. Gust and tip flutter are procedural in the vertex shader.
- Opaque, depth-writing, drawn before the water. No shadows reach grass yet.
- Off in the Scene view by default.

## Where in the frame
| Registration | Stage | Priority | Toggle names |
|---|---|---|---|
| `Grass.FrameStamp` | PreOpaque | 38 | `Always` (wind clock, synthetic tile) |
| `Grass.Draw` | PostOpaque | -20 | `Grass.Init`, `Grass.Cull`, `Grass.Stats.Copy`, `Grass.Hiz.Base`, `Grass.Hiz.Top`, `Grass.Draw.L0`, `Grass.Draw.L1`, `Grass.Draw.L2` |

Code: `src/Features/Grass/`. Shaders: `GrassInit.comp`, `GrassCull.comp`, `Grass.vert`, `Grass.frag`, `GrassCommon.glsl`.
Priority -20 runs after the terrain (Opaque stage), before the shadow mask (0), the water (25) and the sky (100). Grass writes depth, so the later passes see it.

## Passes per frame
1. `Grass.Hiz.*` (optional): farthest-depth pyramid of the final terrain depth (`fx::HizPyramid`). About 0.1 to 0.15 ms.
2. `Grass.Init`: zero the counters, write the three draw commands with their vertex counts (30, 12, 3).
3. `Grass.Cull`: one 64-thread group per chunk. See below.
4. `Grass.Stats.Copy`: copy the 128 byte counter block to a readback (two frames late).
5. `Grass.Draw.L0/L1/L2`: `Fullscreen` verb with `IndirectDraw` (see [writing a render feature](../../guides/writing-a-render-feature.md)). Triangle list, no culling, depth test `LessEqual`, depth write on.

There are three draws and not one, because the vertex counts differ (30, 12 and 3). Each draw has its own push constant with the first instance of its region (`firstInstance` must stay 0).

## How `GrassCull.comp` works
Per chunk (16 m, a grid of `gridCells` x `gridCells` around the camera):
1. Horizontal distance over `lod2Distance`: leave.
2. Chunk outside the height tile: leave (`chunksCulledNoData`).
3. Reduce the min, max and highest dry height of the chunk from the tile. No dry texel above the waterline: leave.
4. 3D distance of the chunk box (with blade height) over `lod2Distance`: leave.
5. Frustum test (6 planes, eye-relative, box padded by 0.9 x blade height for lean and wind).
6. Hi-Z test of the same box (only when the pyramid was built).
7. Blade loop. Candidate `i` of a chunk:
   - Position from a low-discrepancy sequence plus a per-chunk shift. Every prefix of the sequence covers the chunk evenly, so a lower density keeps a subset of the same blades.
   - Bilinear height and exact gradient from the tile. LOD comes from the 3D distance of the blade. `i >= candidates(LOD)` drops it (density step).
   - Water: dropped when the tile flag is set or the height is under `seaLevel + waterMargin`.
   - Slope: dropped over `maxSlopeDeg` (angle of the tile gradient).
   - Density multiplier from meadow noise, slope fade, waterline fade and far edge fade. A stable per-blade hash decides.
   - Survivors take a slot per LOD with one atomic per round. A full region drops the rest (`dropped0..2`). The draw never reads past its region.

Placement uses the integer world chunk index as the hash seed, so blades do not move when the grid scrolls with the camera.
All shader numbers are relative to the grid corner or the eye (computed in double on the CPU). The world is +-16 km, floats alone would shake.

## Blade shape (`Grass.vert`)
- No vertex input. Vertex `v` of the list is mapped to a strip vertex: side = `v & 1`, row = `v >> 1`, `t` = row / segments.
- LOD0: 5 segments (30 list vertices). LOD1: 2 segments (12). LOD2: one triangle (3), wider blades (`lod2WidthScale`).
- Width tapers with `t^1.5`. The curve is quadratic from the root to the tip: the control point is straight above the root, so the base stays upright.
- Bend direction = rest lean + wind push. Random yaw per blade.
- Tip flutter: two fast sines, amplitude grows with `t^2`.
- The fragment shader flips the normal toward the viewer (two-sided without relying on winding).

## Lighting (`Grass.frag`)
Linear HDR, same energy scale as the lit materials (`kDirectScale` 0.65).
`colour = albedo * (hemisphere ambient * ao + sun * wrap diffuse * sqrt(ao))` plus a fake translucency term when the sun is behind the blade.
Sun and its tint come from `ResolveActiveDirectionalLight` and the Atmosphere sun light record. Ambient uses the SceneLighting defaults times the Atmosphere ambient scale.
There is no cascade shadow, no cloud shadow and no AO on grass. Only the screen-space shadow mask (a later pass) darkens it, like the terrain under it.

## Wind
There was no `GlobalWindUBO` in the engine. Now:
- `src/Game/Lighting/WindState.h`: `WindState { directionX, directionZ, speed, offsetX, offsetZ }` and channel `VolumetricCloud.Wind`.
- `VolumetricCloudFeature` publishes it every frame from its always-on PreOpaque entry. The offset is the cloud drift, never wrapped. It moves against the wind, like the cloud noise lookup, so grass waves travel with the clouds.
- Grass reads it with `fx::ChannelReader<WindState>`. With the cloud feature off, or `followCloudWind=false`, Grass uses `windSpeedFallback` and `windDirectionDegFallback` and its own drift.
- There is no wind noise texture. A slow sine wave along the wind direction and a value-noise gust are computed in the shader (`windWaveLengthM`, `windGustLengthM`, `gustStrength`).

## Height tile (what the editor layer must give)
```cpp
struct GrassHeightTile {            // src/Features/Grass/GrassTypes.h
    uint32_t size;                  // texels per side, at most kGrassMaxTileTexels (512)
    float texelSize;                // metres per texel (0.5 .. 4)
    double originX, originZ;        // world xz of texel (0,0)
    float seaLevel;                 // world Y of the ocean
    uint64_t revision;
    std::vector<float> data;        // 2 floats per texel: height, submerged flag (0 or 1). Index (j * size + i) * 2
};
grassFeature->SubmitHeightTile(std::make_shared<const GrassHeightTile>(...));   // main thread
```
- The tile must reach `2 x (lod2Distance + 32 m)` around the camera. `Settings().heightTileTexels` and `heightTileTexelSize` give a size that does.
- Heights must be the drawn surface (the terrain node meshes with erosion and carve), not the raw noise. Raw noise floats or sinks blades by metres.
- Set the flag to 1 for lake, river and ocean texels. The shader also drops anything under `seaLevel + waterMargin`.
- Submit a new tile object when the camera moves out of the middle of the tile, or when the terrain or the water changes. Grass uploads the tile once per frame slot.
- Blades outside the tile are dropped. That is not an error.

## Settings
Sanitized on write (`SanitizeGrassSettings`). NaN and infinity become the default. Not saved in the scene file.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `enabled` | true | bool | Off gives status `Disabled` |
| `hizCulling` | true | bool | Occlusion test of chunks against the terrain depth |
| `collectStats` | true | bool | Counters in `game.stats.*` |
| `debugView` | `Normal` | `Normal`, `LodColor`, `Sway`, `ChunkBounds` | LOD colour: LOD0 green, LOD1 yellow, LOD2 red. Chunk bounds: 16 m checker with white borders |
| `densityLod0` / `densityLod1` / `densityLod2` | 32 / 5 / 0.6 | 0..200, non-increasing | Blades per m2 |
| `patchiness` / `patchScaleM` / `bareThreshold` | 0.6 / 45 / 0.35 | 0..1 / 2..500 / 0..1 | Meadow patches and bare ground |
| `bladeHeight` / `bladeWidth` / `bladeTaper` | 0.6 / 0.045 / 0.85 | 0.05..3 / 0.002..0.5 / 0..1 | Blade size |
| `heightVariation` / `lean` / `maxBend` | 0.35 / 0.25 / 1.2 | 0..0.9 / 0..1.2 / 0.1..1.5 | Variation, rest lean (rad), largest tip angle (rad) |
| `lod1WidthScale` / `lod2WidthScale` | 2.2 / 4.5 | 1..8 / 1..16 | Wider blades when fewer are drawn |
| `maxSlopeDeg` / `slopeFadeDeg` | 40 / 8 | 1..89 / 0..45 | No grass on steeper ground, thinning before it |
| `waterMargin` / `waterFadeBand` | 0.6 / 1.5 | -2..20 / 0..20 | Metres above the water where grass starts, and its fade |
| `lod0Distance` / `lod1Distance` / `lod2Distance` | 30 / 80 / 150 | ordered, lod2 up to 360 | LOD ends. Over `lod2Distance` nothing is drawn |
| `edgeFade` | 0.2 | 0.01..1 | Share of the last LOD where blades thin out |
| `windBend` / `gustStrength` | 1 / 0.5 | 0..4 / 0..3 | Wind push and gust |
| `flutterSpeed` / `flutterStrength` | 9 / 1 | 0..60 rad/s / 0..4 | Tip flutter |
| `windWaveLengthM` / `windGustLengthM` | 28 / 14 | 2..400 | Pattern sizes |
| `followCloudWind` | true | bool | Use the shared cloud wind |
| `windSpeedFallback` / `windDirectionDegFallback` | 4 / 45 | 0..100 / any | Own wind (0 deg = +X, 90 = +Z) |
| `translucency` / `rootAo` / `tintVariation` / `sunVisibility` | 0.6 / 0.35 / 0.5 / 1 | | Look |
| `colorRoot` / `colorTip` | dark / light green | `r,g,b` 0..2 | Linear albedo |
| `gridCells` | 22 | 2..48 | Chunks per side. Raised on its own to cover `lod2Distance`; it never shrinks by itself |
| `heightTileTexels` / `heightTileTexelSize` | 384 / 1 | 64..512 / 0.5..4 | Tile the editor layer should build |
| `instanceCapLod0/1/2` | 400k / 300k / 200k | 1k..2M, 4M in total | Blades per LOD region (32 bytes each, 28.8 MB by default) |
| `seed` | 1 | uint | Placement seed |

Growing a capacity reallocates the instance memory. That is refused while frames overlap.

## Debug
Name for `/feature_debug`: `Grass`. Every setting above is writable: `/feature_debug/set?name=Grass&key=value`. Colours are `r,g,b`.

| Action (`/feature_debug/invoke?name=Grass&action=...`) | Arguments |
|---|---|
| `reset_defaults` | none |
| `synthetic_tile` | `kind=flat\|hills\|cliff`, `baseHeight`, `amplitude`, `wavelength`, `seaLevel`, `lake=true\|false`, `lakeX`, `lakeZ`, `lakeRadius`, `lakeDepth`, `lakeLevel`, `cliffX`. A made-up landscape that follows the camera |
| `clear_tile` | none |

| Read-only key (prefix `game.` or `scene.`) | Meaning |
|---|---|
| `status` | `NotRendered`, `Ok`, `Disabled`, `NoValidCamera`, `NoTile`, `ShadersNotReady`, `TargetFailed`, `PassDisabled` |
| `hizUsed`, `windShared`, `gridCells`, `instanceBytes`, `tileRevision` | What this frame used |
| `stats.*` | Counters from the GPU, two frames old: `chunksInGrid`, `chunksCulledDistance / NoData / Water / Frustum / Hiz`, `chunksLod0..2`, `candidates`, `accepted0..2`, `rejectNoData / LodStep / Water / Slope / Noise`, `dropped0..2`, `requested0..2` |

Also `tile.*` (size, origin, sea level, revision), `synthetic.enabled`, `eye`.

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/invoke?name=Grass&action=synthetic_tile&kind=flat&baseHeight=0.3&seaLevel=-100&lake=false"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Grass&debugView=LodColor"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
curl.exe "http://127.0.0.1:8080/profiler/table?view=flat&lane=gpu&limit=256"
```
Rows: `Grass.Init`, `Grass.Cull`, `Grass.Hiz.Base`, `Grass.Hiz.Top`, `Grass.Stats.Copy`, `Grass.Draw.L0..L2`.
A synthetic tile hangs in the air over the real terrain. Use a flat terrain (`/terrain/set?heightScale=3&erosionEnabled=false`) or a ground height above the terrain to look at it.
To turn the feature off for one view use the registration name: `/render_graph/set_feature_enabled?name=Grass.Draw&enabled=false&view=primary`.

## Cost (Intel Iris Xe, 15 W, lean terrain, overlap off, medians of 2 to 3 rounds)
Frame ms, grass off against grass on. Camera 4 to 8 m above the ground looking along it.

| Game view | Settings | Blades L0 / L1 / L2 | off | on | Cull | Draw |
|---|---|---|---|---|---|---|
| 719x360, flat terrain | default | 40k / 20k / 6k | 7.6 | 10.6 | 0.7 | 1.7 to 2.2 |
| 1264x629, flat terrain | default | 45k / 27k / 6k | 12.3 | 18.4 | 0.75 | 3.0 to 3.5 |
| 1264x629, flat terrain | light (16 / 2.5 / 0.3, 20 / 60 / 120 m) | 9k / 9k / 2k | 12.3 | 15.7 | 0.43 | 1.1 |
| 1264x629, flat terrain | heavy (64 / 10 / 1.2, 40 / 100 / 200 m) | 150k / 80k / 22k | 12.3 | 26.4 | 1.5 | 9.4 |
| 1264x629, real terrain | default | 33k / 26k / 6k | 12.1 | 15.0 | 0.7 | 2.0 to 2.4 |
| 1264x629, real terrain | heavy | 100k / 77k / 19k | 12.1 | 20.8 | 1.5 | 6.0 to 7.0 |

- Draw cost is vertex bound. It follows the vertex count (about 0.6 to 0.7 million vertices per ms), not blade width or height. A trivial vertex shader halves the LOD0 draw (7.3 to 3.4 ms at 140k blades): half of the cost is vertex math.
- The frame grows a little more than the Grass rows add up to: later passes (blends, copies) also get slower.
- Hi-Z costs 0.1 to 0.15 ms and culled 36 to 88 chunks in a hilly spot.
- Everything here is far above the 10 to 15 FPS goal. Raise the distances or the density before you cut anything.

## Limits
- No shadows on grass (no cascade, no cloud shadow). The cascade maps are scene-service bindings that the fullscreen verb cannot bind.
- The Hi-Z pyramid is built from the terrain depth of the same frame. It does not hide blades behind other grass.
- Density steps at the LOD borders are visible as a change in blade count. No cross-fade.
- LOD2 blades are single flat triangles. They shimmer without TAA.
- The terrain camera of the editor has a 3 m near plane. Grass nearer than that is clipped. Ground-level walking needs a smaller near plane.
- The pipelined frame mode (`PipelinedDeferredReadback`) refuses every CPU parameter upload, so no feature with `GpuParams` draws in it. Grass is the same.
- `GrassCull.comp` returns early for a whole group before its barriers. All 64 threads of a group take the same path. Keep it so.
- Not verified: the Frame Debugger replay of the indirect draws, Vulkan validation (the layer is not installed on the test machine).

## Tests
`tests/Features/Grass/`: settings sanitizing, grid and plane math, params packing, synthetic tile, shader reflection of all four shaders (names, access, push size, fullscreen rules) and the shared constants between the GLSL text and the C++ side.

## Related
- [Render features](README.md) · [Water](water.md) · [Volumetric cloud](volumetric-cloud.md) · [GPU-driven scene](gpu-driven-scene.md)
- [Writing a render feature](../../guides/writing-a-render-feature.md) · [Render feature kit](../feature-kit.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) · [Terrain debugging](../../debugging/terrain.md)
