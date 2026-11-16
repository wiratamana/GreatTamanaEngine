# Water (screen-space water)
> Ocean, lakes and rivers drawn per pixel from the scene depth. For anyone tuning, extending or debugging water.
> Verified against code: 2026-11-16

## TL;DR
- No water mesh. A compute pass traces each view ray against a wave height field. A fullscreen draw writes colour and depth.
- The water depth goes into the scene depth. Atmosphere haze, GTAO, SSGI, TAA and the shadow depth range see water as a surface.
- Ocean is infinite and cheap. Lakes (box or polygon) and rivers (polyline) are bodies in a table of up to 16.
- Shore fade and foam come from the real scene depth, so the shore always matches the terrain.
- Reflection is the Atmosphere sky-view table. Screen-space reflection (SSR) is optional and off by default.
- Off in the Scene view by default. The Game camera must be above the water.

## Where in the frame
| Registration | Stage | Priority | Toggle names |
|---|---|---|---|
| `Water.FrameStamp` | PreOpaque | 36 | `Always` (wave clock, snapshot age) |
| `Water.Surface` | PostOpaque | 25 | `Water.Shade.Dispatch`, `Water.Composite.Draw` |

Code: `src/Features/Water/`. Shaders: `WaterShade.comp`, `WaterComposite.vert`, `WaterComposite.frag`.
Priority 25 runs before Gtao (50), Ssgi (55), VolumetricLight (60) and the sky background (100). They all read the water depth.
The feature has no PostComposite entry, so it pays no compositor copy and blend passes.

## How it works
1. **Ray:** unproject the pixel with the eye-relative inverse view-projection. The TAA jitter is removed (`ndc - jitter`).
2. **Body pick:** for each body the eye looks down on, hit its plane. Test the hit against the body shape. Keep the nearest hit that is in front of the scene.
3. **Waves:** a sum of up to 8 sharpened sines, `h = A (s^e - mean)`, `s = 0.5 + 0.5 sin(theta)`. Three fixed-point steps refine the hit. Slopes are analytic.
   This is a Gerstner-style height field, not true Gerstner. There is no horizontal displacement and no vertex mesh.
4. **LOD:** a wave fades out when its wavelength is under about 4 to 14 pixels. Ripples fade by pixel size. Far water is flat and cheap.
5. **Ripples:** two drifting layers of periodic value noise. No texture asset. The lattice wraps with the eye, so there is no pop.
6. **Shading:** Fresnel (Schlick), sky reflection, GGX sun glint, Beer-Lambert style absorption per channel, shallow to deep colour by vertical depth.
7. **Refraction:** the seabed colour is read at a pixel shifted by the slope. It is skipped in deep water. It is kept only where it lies behind the water.
8. **Foam:** crest foam from height, slope and noise. Shore foam from the vertical depth under the surface.
9. **Wet shore:** dry pixels up to `wetBand` metres above a body level are darkened. They are written with their own depth, so nothing else changes.
10. **Composite:** a fullscreen draw with depth test `Always` and `gl_FragDepth` from the shade pass. Alpha 0 is discarded.

Far water is written at depth 0.99999. Features treat depth 0.999999 and above as sky.

Targets per view: `Water.Color` (RGBA16F, rgb + coverage) and `Water.Depth` (R32F). 12 bytes per pixel.

## Bodies
Bodies come from two places: bodies added through `/feature_debug/invoke` (local), and an `IWaterBodySource` set by the editor layer (`WaterFeature::SetBodySource`).
With no body at all, one ocean at `seaLevel` is drawn.

| Kind | Shape | Level |
|---|---|---|
| `Ocean` | Infinite plane | `level` |
| `LakeBox` | Axis-aligned rectangle `boxMin`..`boxMax` | `level` |
| `LakePolygon` | 3..32 points, even-odd rule | `level` |
| `River` | 2..64 points. Per point: surface level, half width | Interpolated per point. `flowSpeed` moves the ripples |

- Caps: 16 bodies and 256 points in total. Invalid bodies are dropped (non-finite numbers, too few points, inverted box).
- The nearest water surface along the ray wins. A lake above the sea hides the sea under it.
- A body only draws when the eye is above its level.
- Water over land needs a basin in the terrain. Without it a lake is a flat sheet over the ground.
- `WaterSampleSpline` turns control points into a Catmull-Rom polyline (capped at 64 points).

## Settings
Sanitized on write (`SanitizeWaterSettings`). NaN and infinity become the default. Not saved in the scene file.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `enabled` | true | bool | Off gives status `Disabled` |
| `seaLevel` | 0 | -10000..10000 | Ocean height when no body is given |
| `waveCount` | 6 | 1..8 | Waves summed. Fewer is faster |
| `waveHeight` | 1.2 | 0..30 | Crest to trough, metres |
| `choppiness` | 0.6 | 0..1 | Crest sharpness |
| `waveSpeed` | 1.0 | 0..4 | Multiplier of the deep-water phase speed |
| `windDirectionDeg` / `windSpread` | 30 / 35 | any / 0..90 | Main wave direction and fan |
| `baseWavelength` | 70 | 4..400 | Longest wave, metres |
| `rippleStrength` / `rippleScale` | 0.6 / 1 | 0..2 / 0.25..4 | Small detail |
| `waveLodScale` | 1 | 0.25..4 | Higher drops fine waves sooner |
| `shallowColor` / `deepColor` / `foamColor` | teal / navy / white | `r,g,b` 0..4 | Colours, linear |
| `clarity` | 14 | 0.5..200 | Metres until the deep colour wins |
| `absorption` | 1 | 0..8 | How fast the scene below fades |
| `refractionStrength` | 0.05 | 0..0.3 | Seabed shift by the ripples |
| `roughness` / `sunSpecular` / `reflectionStrength` | 0.07 / 1 / 1 | 0.02..0.6 / 0..8 / 0..2 | Glint width, glint power, mirror power |
| `crestFoam` / `shoreFoam` / `shoreWidth` | 0.8 / 1 / 2.5 | 0..2 / 0..2 / 0.1..30 | Foam amounts, shore band in metres of depth |
| `ssr` / `ssrSteps` / `ssrMaxDistance` | false / 16 / 400 | bool / 4..48 / 10..20000 | Screen-space reflection |
| `wetShore` / `wetBand` / `wetStrength` / `wetFadeDistance` | true / 1.5 / 0.45 / 3000 | bool / 0.1..20 / 0..1 / 100..50000 | Wet ground |
| `maxDistance` | 100000 | 100..1e6 | Longest water ray, metres |
| `halfRes` / `halfResAuto` | false / true | bool / bool | Shade at half resolution always / when the view has 600000 pixels or more |
| `shadowStrength` | 0 | 0..1 | Reserved. Sun shadow on water is not bound |
| `debugView` | `Off` | see below | Show a mask instead of the picture |

## Debug
Name for `/feature_debug`: `Water`. Every setting above is writable with `/feature_debug/set?name=Water&key=value`. Colours are `r,g,b`.

| Action (`/feature_debug/invoke?name=Water&action=...`) | Arguments |
|---|---|
| `reset_defaults` | none (settings only) |
| `add_ocean` | `level`, `waveScale` |
| `add_lake` | box: `minX minZ maxX maxZ level waveScale`. Polygon: `points=x,z;x,z;...` |
| `add_river` | `points=x,z[,level[,halfWidth]];...` `level` `halfWidth` `flow` `spline=1` `samples` `waveScale` |
| `remove_body` | `index` |
| `clear_bodies`, `dump_bodies` | none |
| `probe` | `x`, `z` world metres. CPU answer: `probe.body`, `probe.level`, `probe.waveHeight`, `probe.surfaceY` |

`probe` knows the bodies, not the terrain. An infinite ocean always reports water, even far inland. Use `/water/probe` for the real answer: its `submerged` field also checks the carved ground height.

| Read-only key (prefix `game.` or `scene.`) | Meaning |
|---|---|
| `status` | `NotRendered`, `Ok`, `NoValidCamera`, `ShadersNotReady`, `TargetFailed`, `PassDisabled`, `Disabled`, `CameraUnderwater`, `NoBodies` |
| `frameIndex`, `extent.w`, `extent.h` | Last frame and size |
| `waveCount`, `bodyCount`, `skyValid`, `textureBytes` | Waves used, packed bodies, sky table found, memory |

`bodies.local` and `bodies.<n>` list the bodies added through the endpoint. `profiler.rows` names the timing rows.

### Debug views
Set `debugView` to one of: `Depth`, `WaterDepth`, `ShoreMask`, `Foam`, `Normal`, `WaveHeight`, `Reflection`, `BodyId`, `WetMask`.
The shade pass fills the whole frame, so `/get_game_view` shows the mask without a screenshot of the editor.

- Set ToneMapping to `mode=Clamp exposure=1 saturation=1 dither=0` to read values.
- Atmosphere haze is applied after the water. Distant pixels are tinted.
- Values: `Depth` view distance /2000 m, `WaterDepth` depth /clarity, `Normal` n*0.5+0.5, `WaveHeight` 0.5 = level.
- Set `debugView=Off` and restore ToneMapping after the test.

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Water&debugView=ShoreMask"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
curl.exe "http://127.0.0.1:8080/profiler/table?view=flat&lane=gpu&limit=256"
```
Rows: `Water.Shade.Dispatch`, `Water.Composite.Draw`.

## Cost (Intel Iris Xe, lean profile, SSR off, overlap mode, medians of 2 rounds)
Frame ms. `none` = Water off, no flat quad. `quad` = the old flat quad. `water` = default (deep seabed skipped; half resolution automatic on big views).

| Game view | View | none | quad | water | water rows |
|---|---|---|---|---|---|
| 719x360 | coast | 8.5 | 8.2 | 8.8 | 0.37 |
| 719x360 | high | 7.8 | 7.9 | 7.6 | 0.40 |
| 719x360 | open ocean | 7.7 | 8.0 | 6.1 | 0.53 |
| 1264x629 | coast | 13.2 | 13.1 | 13.2 | 0.39 |
| 1264x629 | high | 12.0 | 12.2 | 12.5 | 0.45 |
| 1264x629 | open ocean | 12.1 | 12.2 | 10.0 | 0.56 |

- Full-resolution shading at 1264x629 costs 0.8 to 1.7 ms (`halfResAuto=false`). Half resolution cuts the shade pass by about 70 % and the composite by about 45 %.
- Open ocean gets faster than no water at all: the skipped seabed saves 1.1 ms of terrain draw and 1.4 ms of shadow at 719x360.
- SSR with 16 steps adds 0.2 to 0.5 ms at 719x360 and 0.6 to 1.4 ms at 1264x629 (full resolution). Keep it off on this iGPU.
- A lake and a river add 0.4 to 0.9 ms at 719x360. At 1264x629 full resolution they add 1.0 to 2.9 ms; with half resolution 0.4 to 0.9 ms.
- Flight at 300 m/s over the coast: frame median 12.3 ms (none) and 12.8 ms (water), p95 14.2 and 14.8 ms at 1264x629; no stutter from streaming or carve.
- The cost floor is memory traffic: 12 bytes written by the shade pass, 12 read and 12 written by the composite.

## Limits
- Flat silhouettes: waves change normals, not the outline.
- No camera under water. The body is skipped when the eye is below its level.
- Ocean is drawn past the camera far plane. Land beyond the far plane is not in the depth buffer, and the terrain draws none there either, so the water does not hide any drawn land. It only fills the band between the far plane and the true horizon.
- Deep seabed is skipped by the terrain (`cullDeepSeabed`, see [Water world](../../debugging/water-world.md)). That brings back the saving the old flat quad gave. A node counts as hidden when its top is below the ocean by `max(seabedCullDepth, 2 x clarity)` metres. Very clear water with a low `clarity` still shows the seabed above that depth.
- Half resolution (`halfRes`, or automatic from 600000 view pixels) shades one pixel per 2x2 block. The land edge stays exact (depth test in the composite), but foam lines on long beaches show small steps and ripples are a little softer.
- No sun shadow on water. No cloud reflection. The sky table has no sun disk, so the glint is analytic.
- Water has no motion vectors. Moving waves can ghost slightly under TAA.
- Shallow water over a very wide area is bandwidth bound at 720p and above.

## Related
- [Render features](README.md) · [Atmosphere](atmosphere.md) · [Tone mapping](tone-mapping.md)
- [Render feature kit](../feature-kit.md) · [Writing a render feature](../../guides/writing-a-render-feature.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) · [Terrain debugging](../../debugging/terrain.md)
