# Grass (GPU-driven procedural grass)
> Blades are placed, culled and built on the GPU. No mesh asset, no CPU per-blade work. For anyone tuning, extending or debugging grass.
> Verified against code: 2026-11-17
## TL;DR
- A `PreOpaque` compute pass places blades per 16 m chunk and writes the survivors plus six indirect draw commands (three colour, three shadow). Depth-only indirect draws cast L0 and L1 blades into eligible sun shadow cascades before terrain draws; three `PostOpaque` indirect draws shade L0, L1 and L2 into the view.
- Terrain height comes from a **height tile** that the editor layer submits (`GrassFeature::SubmitHeightTile`). Without a tile, nothing is drawn (status `NoTile`).
- Wind is shared with the clouds through the `WindState` channel. Gust and tip flutter are procedural in the vertex shader.
- Opaque, depth-writing, drawn before the water. Blades receive sun cascade and cloud shadows per blade in `Grass.Cull` (before grass writes into the cascades, so grass does not self-shadow through the map) and cast contact shadows onto the terrain.
- Off in the Scene view by default.

## Where in the frame
| Registration | Stage | Priority | Toggle names |
|---|---|---|---|
| `Grass.FrameStamp` | PreOpaque | 38 | `Always` (wind clock, synthetic tile) |
| `Grass.Prepare` | PreOpaque | 50 | `Grass.Init`, `Grass.Cull`, `Grass.Stats.Copy`, `Grass.Shadow.C0.L0`, `Grass.Shadow.C0.L1`, `Grass.Shadow.C1.L0`, `Grass.Shadow.C1.L1`, `Grass.Shadow.C2.L0`, `Grass.Shadow.C2.L1`, `Grass.Shadow.C3.L0`, `Grass.Shadow.C3.L1` |
| `Grass.Draw` | PostOpaque | -20 | `Grass.Draw.L0`, `Grass.Draw.L1`, `Grass.Draw.L2` |

Code: `src/Features/Grass/`. Shaders: `GrassInit.comp`, `GrassCull.comp`, `GrassBlade.glsl`, `Grass.vert`, `Grass.frag`, `GrassShadow.vert`, `GrassShadow.frag`, `GrassCommon.glsl`.
`Grass.Prepare` at `PreOpaque` priority 50 runs after `Shadow.Cascades` (10), `VolumetricCloud.ShadowMap` (12), `Grass.FrameStamp` (38), `TemporalAa.Jitter` (48) and `VolumetricCloud.FrameStamp` (49, shared wind), and before `Opaque` (terrain). `Grass.Draw` at `PostOpaque` priority -20 runs after the terrain, before the shadow mask (0), the water (25) and the sky (100).

## Passes per frame
1. `Grass.Init`: zero the counters, write six indirect draw commands — three colour (`lod0Segments * 6`, 12, 3 vertices) and three shadow (3 vertices each).
2. `Grass.Cull`: 64-thread groups, `extra.w` groups per chunk (1 to 16, 1 per 2048 candidates of the densest LOD). Grants accepted instances to both the colour and shadow indirect commands.
3. `Grass.Stats.Copy`: copy the 128 byte counter block to a readback (two frames late).
4. `Grass.Shadow.C<0..3>.L<0..1>` (optional): depth-only `Fullscreen` verb with `IndirectDraw` into `SunShadowReceiver::maps[c]`, depth compare `Less`, depth write on, no colour target. Draws L0 and L1 regions as single curved 3-vertex triangles (`GrassShadow.vert` / `GrassShadow.frag`). Skipped when `castShadows=false`, when `texelWorld[c] > shadowMaxTexelM`, or when cascade `c`'s slice starts at or past `lod1Distance`.
5. `Grass.Draw.L0/L1/L2`: `Fullscreen` verb with `IndirectDraw` (see [writing a render feature](../../guides/writing-a-render-feature.md)) using the jittered eye-relative view-projection read in `PostOpaque`. Triangle list, no culling, depth test `LessEqual`, depth write on.

There are separate draws per LOD because the vertex counts differ (24, 12 and 3 for colour; 3 for shadow). Each draw pushes the first instance of its region (`firstInstance` must stay 0).

## How `GrassCull.comp` works
Per chunk (16 m, a grid of `gridCells` x `gridCells` around the camera). A dense chunk has thousands of candidates, so up to 16 groups share it (`GrassSlicesPerChunk`): group `s` takes candidate blocks `s, s + k, s + 2k ...` of 256. Groups past the candidates leave right after the distance test. One group per chunk left most of the iGPU idle (cull 1.6 ms, now 0.6 ms).
1. Horizontal distance over `lod2Distance`: leave.
2. Chunk outside the height tile: leave (`chunksCulledNoData`).
3. Reduce the min, max and highest dry height of the chunk from the tile. No dry texel above the waterline: leave.
4. 3D distance of the chunk box (with blade height) over `lod2Distance`: leave.
5. Frustum test (6 planes, eye-relative, box padded by 0.9 x blade height for lean and wind, plus `shadowPadM` when casting shadows so blades just outside the view still cast into it).
6. Hi-Z test (skipped while `Grass.Cull` runs in `PreOpaque`, where scene depth does not exist yet).
7. Blade loop. Candidate `i` of a chunk:
   - Position from a low-discrepancy R2 sequence plus a per-chunk shift, broken up for accepted blades by a candidate-intrinsic hash jitter (`jitterStrength`) and pulled toward 0.55 m tuft centres (`clumpStrength`) that share tuft height, tint and outward fountain lean without drifting as the camera moves.
   - Bilinear height and exact gradient from the tile (re-sampled at the shifted root so sloped blades stay on the mesh and dry). The 3D distance `d` of the blade gives its density `GrassDensityAt(d)`: `densityLod0` close up, falling smoothly (geometric, smoothstep) to `densityLod1` at `lod0Distance` and to `densityLod2` at `lod1Distance`; `densityBlend` is the share of a LOD range the fall takes. `i >= candidates(density)` drops the blade. Lower densities are index prefixes of higher ones, so blades only leave, never move. Thinned blades widen by `(densityLod0 / density) ^ widthCompensation`, at most `maxWidthScale`. Near the far edge they shrink to nothing over the last `farHeightFade` of `lod2Distance`.
   - Water: dropped when the tile flag is set or the height is under `seaLevel + waterMargin`.
   - Slope: dropped over `maxSlopeDeg` (angle of the tile gradient).
   - Density multiplier from meadow noise (floored by `minPatchDensity`, with shorter grass in poor soil instead of bare ground), slope fade, waterline fade and far edge fade. A stable per-blade hash decides.
   - Blade species (`flowerShare`, `weedShare`, `dryShare`): broadleaf rosette weeds widen and bend lower, dry blades shorten and droop more, and wildflowers stand slightly taller.
   - The LOD of the mesh is still chosen by distance (`lod0Distance`, `lod1Distance`), but the blade count no longer jumps there.
   - The survivor does all wind and shadow work here (wave, gust, bend direction, tip angle, flutter, cascade + cloud sun visibility) and writes a packed 32 byte instance: root, height and width (half), yaw and bend direction (snorm8), ground normal, sin of the tip angle and sun visibility (unorm8), flutter (half), terrain colour (rgb 8 bit), a 4-bit blade random (`bits 4..7`) and a 4-bit patch/species nibble (`bits 0..3`: `0..12` meadow/tuft tint, `13` flower, `14` dry, `15` weed). `Grass.vert` then needs no trig except one sqrt.
   - Survivors take a slot per LOD with one atomic per round. A full region drops the rest (`dropped0..2`). The draw never reads past its region.

Placement uses the integer world chunk index as the hash seed, so blades do not move when the grid scrolls with the camera.
All shader numbers are relative to the grid corner or the eye (computed in double on the CPU). The world is +-16 km, floats alone would shake.
## Blade shape (`GrassBlade.glsl`, `Grass.vert`, `GrassShadow.vert`)
- Shared quadratic curve + tip flutter live in `GrassBlade.glsl` (`GrassBladeSample` and `GrassBladePosition`).
- No vertex input. In `Grass.vert`, vertex `v` of the list is mapped to a strip vertex: side = `v & 1`, row = `v >> 1`, `t` = row / segments.
- LOD0: `lod0Segments` quads (default 4, 24 list vertices; 3 shows kinks, 5 to 6 costs 0.3 to 0.5 ms more). LOD1: 2 segments (12). LOD2: one triangle (3), widened by the density rule.
- Width tapers with `t^1.5`; near LOD0 wildflowers thin the stalk and flare the top row x3 into a blossom head before fading into standard blades near `lod0Distance`. The curve is quadratic from the root to the tip: the control point is straight above the root, so the base stays upright.
- Bend direction = rest lean + outward tuft lean + wind push. Random yaw per blade. The cull computes them; the vertex shader only reads the instance and walks the curve.
- Normal: across the blade, mixed with the ground normal of the tile (`groundNormalBlend`), and converging further toward the ground normal over 12..70 m (`lookModern`) for soft field shading. Spine tangent is forwarded to `Grass.frag` for anisotropic sheen.
- Tip flutter: two fast sines, amplitude grows with `t^2`.
- The fragment shader flips the normal toward the viewer (two-sided without relying on winding).
- Shadow caster (`GrassShadow.vert`): 3 vertices per blade (`v = 0` root-left, `v = 1` root-right, `v = 2` curved tip), base width clamped to at least `shadowWidthScale * texelWorld[c]` so thin blades do not sub-sample out of the shadow map, shifted along `sunDir` by `shadowBiasTexels * texelWorld[c]`, and projected by `cascadeVpRel = cascadeVP * Translation(eye)` computed in `double` on the CPU.
## Lighting (`Grass.frag`)
Linear HDR, same energy scale as the lit materials (`kDirectScale` 0.65).
With `lookModern=1` (default; `0` restores the legacy shading model for A/B comparison), blades use distance-flattened root-to-tip gradient and root AO (25..140 m), mild midrib darkening, wrap front diffuse, two-sided yellow-green leaf transmission (`back` underside + forward sun glow), Kajiya-Kay-like tangent sheen, sky Fresnel rim and a subtle wind brightness wave where gusts flatten the grass.
Albedo: de-neoned linear root-to-tip palette (`colorRoot` / `colorTip`), shifted across meadows and tufts toward olive (`patchTint`), with cooler broadleaf weeds, straw-coloured dry blades and a 4-colour LOD0 wildflower table (white, yellow, violet, pink), then pulled toward the **terrain colour** of the ground under the blade (`terrainTint`, strongest at the root). Far blades converge to the terrain colour (`farTerrainBlend`, from 0.2 to 0.9 of `lod2Distance`), so the field melts into the ground. The sun term fades on ground that faces away from the sun (`selfShadow`, ground normal dot sun).
Sun and its tint come from `ResolveActiveDirectionalLight` and the Atmosphere sun light record. Ambient uses the SceneLighting defaults times the Atmosphere ambient scale.
Sun cascade shadows (`SunShadowReceiver`) and cloud shadows (`CloudShadowMap`) are sampled per accepted blade in `Grass.Cull` (with normal and sunward offset, slope-aware bias and rotated spiral PCF), packed into the fourth byte of `ground`, and forwarded flat from `Grass.vert` to `Grass.frag`. `Grass.frag` scales the direct sun and subsurface terms by `sunVis` and adds `cloudFill * (1 - sunVis)` diffuse sky fill so shadowed blades are not black; when no shadow data is published or `applyInMaterials` is off, `sunVis` stays 1. GTAO runs before grass so screen-space AO does not darken blades.

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
    std::vector<float> data;        // 2 floats per texel (kGrassTileStride): height, then GrassTileFlagsColor(): bit 24 submerged, bits 0..23 terrain colour (r | g << 8 | b << 16). Index (j * size + i) * 2
};
grassFeature->SubmitHeightTile(std::make_shared<const GrassHeightTile>(...));   // main thread
```
- The tile must reach `2 x (lod2Distance + 32 m)` around the camera. `Settings().heightTileTexels` and `heightTileTexelSize` give a size that does.
- Heights must be the drawn surface (the terrain node meshes with erosion and carve), not the raw noise. Raw noise floats or sinks blades by metres.
- Set the submerged bit for lake, river and ocean texels. The shader also drops anything under `seaLevel + waterMargin`.
- The terrain colour is the vertex colour of the drawn node (linear, 8 bit per channel); where no node is resident it is the ramp colour of the noise height.
- Submit a new tile object when the camera moves out of the middle of the tile, or when the terrain or the water changes. Grass uploads the tile once per frame slot.
- Blades outside the tile are dropped. That is not an error.

## Real terrain tile (`GrassTileProvider`)
`src/Features/Grass/Editor/GrassTileProvider.*` and `GrassTileBuilder.*` feed the tile while the terrain is open. The `GrassTiles` editor module (`GrassTilesModule`) owns the provider and ticks it after the terrain and water modules.
- **Heights** come from the resident terrain node meshes (`ITerrainWorld::SnapshotSurface`, `src/Features/Terrain/TerrainSurface.*`). The drawn quad is split along the (0,0)-(1,1) diagonal, and the sampler uses the same split, so tile height equals the drawn surface. Where no node is resident it falls back to the noise field.
- **Water flag** is `ground <= SampleAt(x, z).surfaceY + waterMargin` from the live `WaterBodySet` (ocean, lakes, rivers). The tile `seaLevel` is the ocean body level, or a very low value when there is no ocean.
- **Worker thread.** The main thread only collects the node list (about 0.01 ms). One `std::jthread` builds the tile (default 512 x 512 at 2 m, about 50 ms) and the main thread submits the finished tile. Where no node is resident (behind the camera, far away) the noise field is read on every 4th texel and interpolated: that cut a 512 tile from 440 ms to 53 ms.
- **Texel grid.** The origin is snapped to 8 texels, so texels keep their world position between rebuilds. A rebuild never moves a blade.
- **Rebuild policy** (`DecideGrassTileRebuild`, pure and tested): first tile, settings change (tile size, texel, LOD2 distance, water margin), camera drift past the slack (`half - reach - 8 m`, at least 8 m; 172 m with the default 2 m texels and 512 texels), water bodies changed, carve changed or a carve rebuild finished, terrain settings changed, and a level 0 or 1 node became resident, **or a resident node got a new mesh under the same key** (a carve or terrain edit swaps meshes; without this the tile kept the old heights and grass grew inside a new lake). For 4 s after a terrain edit every rebuilt node counts. At most 4 rebuilds per second, one at a time.
- **No garbage.** If less than half of the texels around the camera come from resident nodes, nothing is submitted (`skippedIncomplete` counts them). A teleport shows no grass for a moment instead of grass at height 0.
- **Close and reopen.** When the terrain closes, the provider submits a null tile (grass draws nothing, status `NoTile`). Reopening builds a new tile. A synthetic tile (`synthetic_tile`) switches the provider off until `clear_tile`.
- Measured root error against the drawn mesh (60 random points within 100 m): 1.5 m texels mean 6 mm, p95 18 mm, max 24 mm; 1 m texels p95 4 mm, max 7 mm. The noise height (`/terrain/height`) is off by p50 6 to 8 cm, p95 23 to 30 cm, max 0.33 to 0.46 m on the same points.

## Settings
Sanitized on write (`SanitizeGrassSettings`). NaN and infinity become the default. Not saved in the scene file.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `enabled` | true | bool | Off gives status `Disabled` |
| `hizCulling` | false | bool | Occlusion test setting; safely ignored while `Grass.Cull` runs in `PreOpaque` (no scene depth yet) |
| `collectStats` | true | bool | Counters in `game.stats.*` |
| `debugView` | `Normal` | `Normal`, `LodColor`, `Sway`, `ChunkBounds`, `SunVis` | LOD colour: LOD0 green, LOD1 yellow, LOD2 red. Chunk bounds: 16 m checker with white borders. `SunVis`: per-blade sun visibility (blue shadowed to yellow lit) |
| `preset` | - | `Lean`, `Medium`, `Big`, `Huge` | Write-only shortcut: sets density, distances, capacities and tile size. Defaults are `Big` |
| `densityLod0` / `densityLod1` / `densityLod2` | 40 / 6 / 0.8 | 0..200, non-increasing | Blades per m2 close up, at `lod0Distance`, at `lod1Distance` |
| `densityBlend` | 0.5 | 0.05..1 | Share of a LOD range over which the density falls |
| `patchiness` / `patchScaleM` / `bareThreshold` | 0.6 / 45 / 0.35 | 0..1 / 2..500 / 0..1 | Meadow patches and poor-soil threshold |
| `minPatchDensity` | 0.22 | 0..1 | Minimum density floor in poor meadow patches, paired with shorter grass (`0` = bare patches) |
| `jitterStrength` / `clumpStrength` | 0.6 / 0.6 | 0..1.5 / 0..1 | R2 lattice jitter radius (`1/sqrt(density)`) and 0.55 m tuft clumping (`0` = off) |
| `bladeHeight` / `bladeWidth` / `bladeTaper` | 0.6 / 0.045 / 0.85 | 0.05..3 / 0.002..0.5 / 0..1 | Blade size |
| `heightVariation` / `lean` / `maxBend` | 0.35 / 0.25 / 1.2 | 0..0.9 / 0..1.2 / 0.1..1.5 | Variation, rest lean (rad), largest tip angle (rad) |
| `widthCompensation` / `maxWidthScale` | 0.45 / 5 | 0..1 / 1..16 | Thinned blades widen by (full density / density) to this power, at most this much |
| `lod0Segments` | 4 | 3..6 | Quads of a near blade |
| `maxSlopeDeg` / `slopeFadeDeg` | 50 / 12 | 1..89 / 0..45 | No grass on steeper ground, thinning before it |
| `waterMargin` / `waterFadeBand` | 0.6 / 1.5 | -2..20 / 0..20 | Metres above the water where grass starts, and its fade |
| `sandHeight` | 7 | 0..200 | Metres above the ocean that stay bare (beach), fades in over 3 m. Matches the terrain `sandMaxHeight` |
| `alpineHeight` / `alpineFade` | 450 / 150 | -1000..100000 / 0..5000 | World Y where grass starts to thin out (rock, snow) and the band to zero |
| `lod0Distance` / `lod1Distance` / `lod2Distance` | 40 / 120 / 300 | ordered, lod2 up to 360 | LOD ends. Over `lod2Distance` nothing is drawn |
| `edgeFade` / `farHeightFade` | 0.2 / 0.15 | 0.01..1 / 0..0.6 | Share of the last LOD where blades thin out, and where they shrink to nothing |
| `windBend` / `gustStrength` | 1 / 0.5 | 0..4 / 0..3 | Wind push and gust |
| `flutterSpeed` / `flutterStrength` | 9 / 1 | 0..60 rad/s / 0..4 | Tip flutter |
| `windWaveLengthM` / `windGustLengthM` | 28 / 14 | 2..400 | Pattern sizes |
| `followCloudWind` | true | bool | Use the shared cloud wind |
| `windSpeedFallback` / `windDirectionDegFallback` | 4 / 45 | 0..100 / any | Own wind (0 deg = +X, 90 = +Z) |
| `translucency` / `rootAo` / `tintVariation` / `sunVisibility` | 0.6 / 0.35 / 0.5 / 1 | | Look |
| `receiveSunShadow` / `receiveCloudShadow` | true / true | bool | Sample sun cascades (`Shadow.Receiver`) and cloud shadow (`VolumetricCloud.ShadowMap`) in `Grass.Cull` |
| `shadowStrength` / `cloudFill` | 1 / 0.6 | 0..1 / 0..1 | Shadow darkening strength and diffuse sky fill added to shadowed blades |
| `castShadows` | true | bool | Draw depth-only L0 and L1 shadow blades into eligible sun cascades in `Grass.Prepare` |
| `shadowMaxTexelM` | 0.40 | 0.01..4 | Skip cascades whose `texelWorld` exceeds this size (m) |
| `shadowBiasTexels` | 1.0 | -4..8 | Caster shift along the sun direction in cascade texels |
| `shadowWidthScale` | 1.5 | 0..8 | Minimum shadow blade base width in cascade texels |
| `shadowPadM` | 0.0 | 0..32 | Extra camera-frustum chunk padding while casting shadows (m). Colour draws pay for the extra blades (+0.4 ms at 3 m), chunks are 16 m so 0 already keeps edge chunks |
| `terrainTint` / `farTerrainBlend` | 0.6 / 0.8 | 0..1 | Pull toward the terrain colour (near, far) |
| `groundNormalBlend` / `selfShadow` | 0.5 / 0.7 | 0..1 | Ground normal in the blade normal; sun fade on ground facing away |
| `lookModern` | 1.0 | 0..1 | Blend from legacy shading (`0`) to two-sided translucent + sheen + distance flatten (`1`) |
| `patchTint` | 1.0 | 0..1 | Meadow and tuft hue shift toward olive (`0` = uniform palette) |
| `weedShare` / `dryShare` / `flowerShare` | 0.06 / 0.08 / 0.004 | 0..0.5 / 0..0.5 / 0..0.1 | Shares of broadleaf weeds, dry straw blades and LOD0 wildflowers (`0` = off) |
| `colorRoot` / `colorTip` | `0.022,0.06,0.009` / `0.1,0.2,0.035` | `r,g,b` 0..2 | De-neoned linear root and tip albedo |
| `gridCells` | 41 | 2..48 | Chunks per side. Raised on its own to cover `lod2Distance`; it never shrinks by itself |
| `heightTileTexels` / `heightTileTexelSize` | 512 / 2.0 | 64..512 / 0.5..4 | Tile the editor layer should build |
| `instanceCapLod0/1/2` | 250k / 220k / 180k | 1k..2M, 4M in total | Blades per LOD region (32 bytes each, 19.8 MiB by default) |
| `seed` | 1 | uint | Placement seed |

Growing a capacity reallocates the instance memory. That is refused while frames overlap.

The settings you edit (`UserSettings()`) only get a per-value range clamp. The shaders use `Settings()`, which also orders the LOD distances and densities. Lowering `lod2Distance` below `lod1Distance` therefore changes only the effective copy: raise it again and your `lod1Distance` is back. `/feature_debug/state` shows `effective.*` next to the keys.

### Presets
`preset=Lean|Medium|Big|Huge` sets densities, distances, capacities and the tile only (the look stays).

| Preset | Density L0 / L1 / L2 | LOD ends (m) | Capacity (k) | Instance memory | Tile |
|---|---|---|---|---|---|
| Lean | 16 / 2.5 / 0.3 | 20 / 60 / 120 | 100 / 80 / 60 | 7.3 MiB | 384 at 1.5 m |
| Medium | 32 / 5 / 0.6 | 30 / 80 / 150 | 200 / 160 / 120 | 14.6 MiB | 384 at 1.5 m |
| **Big** (default) | 40 / 6 / 0.8 | 40 / 120 / 300 | 250 / 220 / 180 | 19.8 MiB | 512 at 2 m |
| Huge | 64 / 12 / 1.6 | 60 / 180 / 360 | 500 / 450 / 350 | 39.7 MiB | 512 at 2 m, 48 x 48 chunks |

360 m is the cap of the 48 x 48 chunk grid. Past about 400 m a blade is under one pixel at 1264 px wide, so a bigger grid buys nothing.

## Debug
Name for `/feature_debug`: `Grass`. Every setting above is writable: `/feature_debug/set?name=Grass&key=value`. Colours are `r,g,b`.

| Action (`/feature_debug/invoke?name=Grass&action=...`) | Arguments |
|---|---|
| `reset_defaults` | none |
| `synthetic_tile` | `kind=flat\|hills\|cliff`, `baseHeight`, `amplitude`, `wavelength`, `seaLevel`, `lake=true\|false`, `lakeX`, `lakeZ`, `lakeRadius`, `lakeDepth`, `lakeLevel`, `cliffX`. A made-up landscape that follows the camera |
| `clear_tile` | none |
| `tile` | none. State of the tile provider: `source.*` (revision, origin, size, build ms, coverage, rebuild reasons) |
| `probe` | `x`, `z`. Tile height against the drawn mesh height and the noise height at one point |
| `probe_batch` | `n` (1..500), `radius`, `seed`. Random points around the camera: mean, p50, p95, max error of tile against mesh, and of noise against mesh |

| Read-only key (prefix `game.` or `scene.`) | Meaning |
|---|---|
| `status` | `NotRendered`, `Ok`, `Disabled`, `NoValidCamera`, `NoTile`, `ShadersNotReady`, `TargetFailed`, `PassDisabled` |
| `hizUsed`, `windShared`, `shadow.cascades`, `shadow.cloud`, `shadow.castCascades`, `shadow.castBlades`, `gridCells`, `instanceBytes`, `tileRevision` | What this frame used |
| `stats.*` | Counters from the GPU, two frames old: `chunksInGrid`, `chunksCulledDistance / NoData / Water / Frustum / Hiz`, `chunksLod0..2`, `candidates`, `accepted0..2`, `rejectNoData / LodStep / Water / Slope / Noise`, `dropped0..2`, `requested0..2` |

Also `tile.*` (size, origin, sea level, revision), `source.*` (tile provider), `game.wind.*` (direction, speed and pattern offset the blades used), `synthetic.enabled`, `eye`.
Short routes: `GET /grass/tile`, `GET /grass/probe?x=&z=`, `GET /grass/probe_batch?n=&radius=&seed=`.

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/invoke?name=Grass&action=synthetic_tile&kind=flat&baseHeight=0.3&seaLevel=-100&lake=false"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Grass&debugView=LodColor"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
curl.exe "http://127.0.0.1:8080/profiler/table?view=flat&lane=gpu&limit=256"
```
Rows: `Grass.Init`, `Grass.Cull`, `Grass.Stats.Copy`, `Grass.Shadow.C0..3.L0..1`, `Grass.Draw.L0..L2`.
A synthetic tile hangs in the air over the real terrain. Use a flat terrain (`/terrain/set?heightScale=3&erosionEnabled=false`) or a ground height above the terrain to look at it.
To turn the feature off for one view use the registration names: `/render_graph/set_feature_enabled?name=Grass.Prepare&enabled=false&view=primary` and `/render_graph/set_feature_enabled?name=Grass.Draw&enabled=false&view=primary`.

## Cost (Intel Iris Xe, 15 W, lean terrain, overlap off, medians of 2 to 3 rounds)
Older tables (F1, F2): "default" there means the old settings, now the `Medium` preset with a hard density step and a 30-vertex LOD0. The current numbers are in "Presets, full scene (F3)" below.
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

### Full scene (F2): terrain, water, atmosphere, volumetric cloud and grass
Real terrain, ocean plus lake plus river, clouds on, camera at the west coast (-2000, -800). Frame ms, medians of 3 interleaved rounds. Default grass (32 / 5 / 0.6 per m2, 30 / 80 / 150 m).

| Game view | Camera | off | on | Cull | Hi-Z | Draw | Blades L0 / L1 / L2 |
|---|---|---|---|---|---|---|---|
| 784x359 | 1.7 m above ground | 21.5 | 26.5 | 0.86 | 0.06 | 3.3 | 50k / 24k / 5k |
| 784x359 | 40 m | 18.5 | 18.8 | 0.23 | 0.06 | 0.4 | 0 / 20k / 5k |
| 784x359 | 300 m | 13.4 | 13.5 | 0.04 | 0.06 | 0.01 | 0 |
| 784x359 | hillside, 12 m | 8.9 | 13.4 | 0.9 | 0.07 | 2.2 | 31k / 16k / 1k |
| 1264x629 | 1.7 m above ground | 25.5 | 31.9 | 1.16 | 0.11 | 3.1 | 50k / 24k / 5k |
| 1264x629 | 40 m | 23.3 | 25.6 | 0.26 | 0.11 | 0.5 | 0 / 19k / 5k |
| 1264x629 | 300 m | 21.2 | 20.5 | 0.04 | 0.11 | 0.02 | 0 |
| 1264x629 | hillside, 12 m | 15.0 | 20.6 | 1.14 | 0.12 | 1.9 | 31k / 16k / 2k |
| 1264x629 | big preset, 1.7 m | 25.4 | 34.9 | 1.56 | 0.11 | 5.5 | 90k / 57k / 35k |
| 1264x629 | big preset, hillside | 14.9 | 23.5 | 1.45 | 0.12 | 3.7 | 64k / 24k / 5k |

Big preset: `lod0Distance=40 lod1Distance=120 lod2Distance=300 densityLod0=40 densityLod1=6 densityLod2=0.8 instanceCapLod0=600000 instanceCapLod1=500000 instanceCapLod2=400000` (tile becomes 443 texels at 1.5 m). Even this is about 29 FPS with everything on. The clouds, water and atmosphere cost more than the grass.

### Presets, full scene (F3)
Terrain + ocean + atmosphere + volumetric cloud + grass, 1264x629, Iris Xe, medians of 2 interleaved rounds, frame ms. `off` is grass disabled. Poses: ground (1.7 m at the coast), hillside (12 m), alt40 (40 m at the coast).

| Preset | ground | hillside | alt40 | Blades L0 / L1 / L2 (ground) |
|---|---|---|---|---|
| off | 24.7 | 14.8 | 25.0 | - |
| Lean | 26.8 | 16.6 | 24.5 | 8k / 5k / 2k |
| Medium | 29.2 | 18.5 | 25.2 | 34k / 13k / 5k |
| **Big** | 31.1 | 20.2 | 26.8 | 65k / 31k / 26k |
| Huge | 40.3 | 26.6 | 32.3 | 195k / 120k / 62k |

Big costs +6.3 ms at the worst pose (32 FPS in the whole scene). Huge is 25 FPS. The 10 to 15 FPS goal is beaten at every preset; the clouds, water and atmosphere are 23 of the 25 ms. Dropped blades: 0 in every run.

Cull and draw (GPU ms, `ground`): Lean 0.54 / 0.65, Medium 0.75 / 1.73, Big 1.38 / 2.83, Huge 3.77 / 8.98. The draw cost is about 1.1 ms of vertex and triangle work plus a part that grows with blade width (1.68 to 1.12 ms when the width goes 0.045 to 0.010 at 47k blades).

## Limits
- Sun cascade and cloud shadows are evaluated once per blade root in `Grass.Cull` (8-bit visibility per blade, not per fragment along the blade). Because `Grass.Cull` reads the cascades before `Grass.Shadow.*` writes into them, grass casts contact shadows onto the terrain without self-shadowing through the cascade map.
- While `Grass.Cull` runs in `PreOpaque`, scene depth does not exist yet, so `hizCulling` is off by default and ignored if set.
- The mesh LOD still switches at `lod0Distance` / `lod1Distance` (5 or 4 segments to 2 to 1 triangle). The blade count no longer jumps there, the blade shape does.
- LOD2 blades are single flat triangles. They shimmer without TAA.
- The terrain camera near plane shrinks with the height above ground (0.1 x the height, between 0.15 m and 3 m, `TerrainWorld::AdaptCameraNear`), so you can stand 1.7 m above the grass. The far plane is still 5 to 13.5 km: depth precision at very long range is lower at ground level (not seen as artefacts in the test views). The free camera still keeps 3 m of clearance; use `/terrain/flight` with `minAlt` or `/terrain/cam?mode=script&alt=1.7`.
- Grass takes the terrain colour from the node vertex colours. Biome limits are still settings: `sandHeight` (7 m, like the terrain `sandMaxHeight`), `alpineHeight` / `alpineFade` (450 m and 150 m, below the terrain snow line at 0.55 x heightScale), `maxSlopeDeg` (50, terrain grass slope is 32 to 42).
- The pipelined frame mode (`PipelinedDeferredReadback`) refuses every CPU parameter upload, so no feature with `GpuParams` draws in it. Grass is the same.
- `GrassCull.comp` returns early for a whole group before its barriers. All 64 threads of a group take the same path. Keep it so.
- Validation: core and synchronization validation ran clean for Grass (see [Troubleshooting](../../troubleshooting.md), "Vulkan validation messages"). Not verified: the Frame Debugger replay of the indirect draws, one event at a time.

## Debug views and checks (F3)
- `debugView=LodColor`, `Sway` (blue to red by tip angle), `ChunkBounds` and `SunVis` (per-blade sun visibility) all work on the packed instance.
- Checked live: Scene view enabled (`/render_graph/set_feature_enabled?name=Grass.Draw&enabled=true&view=secondary`, status Ok), Frame Debugger enabled and a capture taken, window resize 640x400 to 1500x800, 3 terrain close and open cycles, `enabled=false` (no Grass rows in the profiler, status `Disabled`). `/get_logs?category=FeatureKit` stays at the one startup Gtao line. The same actions also ran under core and synchronization validation without a Grass message.
- The Grass window is an `EditorWindow` (always dockable, default dock Bottom on a first run). An existing `imgui.ini` without a Grass entry shows it floating, as an own OS window: drag it into a dock. It is not a bug.

## Tests
`tests/Features/Terrain/TerrainSurfaceTests.cpp` (drawn-surface sampling, finest node wins), `tests/Features/Grass/Editor/GrassTileBuilderTests.cpp` (snapping, node sampling, ocean and lake flags, bilinear tile sampling, rebuild policy). `tests/Features/Grass/`: settings sanitizing, grid and plane math, params packing, shadow-cast planning and 30 km eye-relative cascade precision, synthetic tile, shader reflection of all six shaders (names, access, push size, fullscreen and depth-only attachment rules) and the shared constants between the GLSL text and the C++ side.

## Related
- [Render features](README.md) · [Water](water.md) · [Volumetric cloud](volumetric-cloud.md) · [GPU-driven scene](gpu-driven-scene.md)
- [Writing a render feature](../../guides/writing-a-render-feature.md) · [Render feature kit](../feature-kit.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) · [Terrain debugging](../../debugging/terrain.md)
