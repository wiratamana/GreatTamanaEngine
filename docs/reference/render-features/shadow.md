# Shadow

> Cascaded shadow maps for the sun, fitted to the camera every frame. For anyone tuning or debugging shadows.
> Verified against code: 2026-10-30

## TL;DR
- Feature `Shadow` in `src/Features/Shadow/`. Up to 4 cascades. Directional light only.
- Cascades are fitted to the visible depth range (SDSM). No box to tune by hand.
- Materials read the cascade maps from scene slots 0..3 and remove only the sun term. Ambient stays.
- A screen-space mask path exists for debug views. It runs only when asked.
- SceneLighting packs the cascade data for materials. See [Scene lighting](scene-lighting.md).

## Where in the frame

| Registration | Stage, priority | Work | Toggle names |
|---|---|---|---|
| `Shadow.FrameStamp` | PreOpaque, 30 | Frame clock for snapshots | none (`Always`) |
| `Shadow.Cascades` | PreOpaque, 10 | One depth draw per cascade. GPU-culled cluster lanes | `Shadow.Cascade0..3.Draw`, `Shadow.Cluster.L<c>.P<s>.Reset/Cull/Draw` |
| `Shadow.Mask` | PostOpaque, 0 | Screen-space mask and cascade id (R8). Only for debug views or `applyInMaterials` off | `Shadow.Mask.Dispatch` |
| `Shadow.DepthRange` | PostOpaque, 100 | Nearest and furthest visible depth, copied to CPU | `Shadow.DepthRange.Reduce` (+ `.Clear`, `.Readback`, one unit) |
| `Shadow.Composite` | PostComposite, -500, `ScreenSpaceMask` | Debug views. With `applyInMaterials` off it also darkens | `Shadow.Composite.Draw` |

- Channels: `Map`, `Mask`, `CascadeId`, and `Receiver` (a `SunShadowReceiver`).
- Scene slots: `Shadow.Cascade0..3.Slot`, kind `Depth2D`, index = cascade index.
- `Map`, `Receiver` and the slots are published only if every active cascade pass was recorded. A cascade pass toggled off switches mask and composite off too.
- Textures per view: `Shadow.Cascade0..3` (depth). `Shadow.Mask` and `Shadow.CascadeId` exist only while the mask pass runs. Set `debugView` first.
- Active in both views.

## How it works

| Part | Rule |
|---|---|
| Splits | "Practical" scheme. `splitLambda` 0 = uniform, 1 = logarithmic |
| Cascade shape | Bounding sphere of the slice, relative to the eye, rounded up to 1/16 unit |
| Stable edges | Light-space center snaps to whole texels in double precision (`stabilizeCascades`) |
| Near plane | `casterRange` beyond the sphere toward the sun. No depth clamp, no hardware bias |
| SDSM | Reduce pass finds min and max depth. `fx::GpuReadback` returns it 2 frames later |
| SDSM smoothing | Range is padded, grows at once, shrinks over `sdsmShrinkSeconds`, snaps to a log grid |
| SDSM fallback | `useSdsm` off, a pass of the group off, shader missing, or no sample for 120 frames: fixed splits over `[near, maxDistance]` |
| Caster culling | `CullToFrustum(casterCullPaddingFraction)`. Meshes without bounds are never culled. Skinned meshes use rest-pose bounds |
| Depth pipelines | One depth-only pipeline per vertex layout. A layout with no depth-only shader casts no shadow |
| Cluster lanes | One lane per cascade, 4 part slots. Order: `Plan()`, CPU draw with `Exclude`, then `Record()` |
| Lane culling | Frustum only. Never the back-face cone. No padding. `cullCasters` off = no cluster culled |
| Capture | A frame with the Frame Debugger capture armed uses the CPU draw only |
| Alpha-tested casters | Stay on the CPU depth draw, drawn opaque |

### Map size
- Real size per view: 2 texels per pixel of the longer view side, at least 512, power of two, capped by `maxMapResolution`.
- A new size applies only after it was the target for 30 frames in a row. A resize waits for the GPU once.
- A refused resize keeps the old size and retries later.
- Cascades above `cascadeCount` shrink to 4x4.
- Memory per view: `4 x resolution^2 x 4 bytes`. 64 MB at 2048.

### Cascade skip
- A view skips its cascades when:
  - the last 3 depth samples found no surface,
  - no `Range` sample is newer than 6 frames,
  - and no reader needs the map in that view.
- Atmosphere (aerial volume) and VolumetricLight are such readers. They count only while enabled in that view.
- So the Game view never skips by default. The Scene view can.
- A skipped view publishes nothing and reports `Skipped`. The maps stay allocated.
- Expect 2 to 3 frames of missing shadow when the camera turns from empty sky to a surface.

## Material slot contract
- Cascade `c` is `layout(set = 1, binding = c)` in material shaders. Bindings 4..7 are free.
- An unpublished slot is a 1x1 dummy. The shader never reads a cascade at or above the count.
- `ShadowMask.comp` and `SunShadowAmount()` in `DirectionalLighting.glsl` use the same algorithm: cascade pick, normal offset, texel-scaled bias, Vogel-disk PCF, cascade blend, distance fade.
- Air shading (Atmosphere, VolumetricLight) reads `Receiver` and shares `Shaders/OrthoMapSetVisibility.glsl`.
- `SunShadowReceiver.depthFitted` says the splits were fitted to a measured range. `SunShadowReceiverUsable()` is the one validity check for all readers.

## Settings

| Key | Default | Range | Meaning |
|---|---|---|---|
| `cascadeCount` | 4 | 1..4 | Number of cascades |
| `maxMapResolution` | 2048 | 512, 1024, 2048, 4096 | Upper cap per cascade |
| `maxDistance` | 20000 | 1..100000 | Shadow distance, clamped to camera far |
| `splitLambda` | 0.85 | 0..1 | Uniform to log splits |
| `cascadeBlendFraction`, `distanceFadeFraction` | 0.10, 0.10 | 0..0.5 | Blend band, fade band |
| `casterRange` | 200 | 1..100000 | Extra distance toward the sun |
| `cullCasters`, `casterCullPaddingFraction` | true, 0.25 | padding 0..2 | Caster culling |
| `constBiasTexels`, `slopeBiasTexels`, `normalOffsetTexels` | 1.5, 1, 1 | 0..16 | Bias |
| `filterRadiusTexels`, `filterSamples` | 1.5, 4 | 0..8; 1, 4, 8, 16 | PCF filter |
| `strength` | 1 | 0..1 | Fraction of the sun term removed in shadow |
| `applyInMaterials` | true | bool | false = `Shadow.Composite` darkens the final color |
| `stabilizeCascades` | true | bool | Texel snapping |
| `debugView` | `Off` | `Off`, `CascadeColors`, `Mask` | Debug view |
| `useSdsm` | true | bool | false = fixed splits |
| `sdsmPaddingFraction`, `sdsmShrinkSeconds`, `sdsmQuantizeSteps` | 0.15, 0.3, 4 | 0..1; 0..10; 0..16 | SDSM tuning |

- The shader slope bias is `max(slopeBiasTexels, filterRadiusTexels)`.

### Caster range
- Cascade depth span is `2 * radius + casterRange`. World bias does not change with it.
- Needed: `casterRange >= relief / sin(sunElevation)`, up to 100000. 1000 m of relief at 3 degrees needs about 19 km.
- Default stays 200. A larger value costs depth precision in small cascades.

## Debug keys

- Writable: every setting above, plus action `reset_defaults` (no arguments).
- Read-only, per view with prefix `game.` or `scene.`:

| Key | Meaning |
|---|---|
| `status`, `mask.status` | `NotRendered`, `Ok`, `NoValidCamera`, `ShadersNotReady`, `TargetFailed`, `PassDisabled`, `Skipped` |
| `resolutionInUse`, `cascadeBytes` | Real map size and memory. Never the cap |
| `sunElevationDegrees`, `effectiveMaxDistance`, `frameIndex`, `timing.deltaSeconds` | Frame facts |
| `cascade<i>.sliceStart`, `.sliceEnd`, `.radius`, `.texelWorldSize`, `.depthRange` | Cascade shape |
| `cascade<i>.gridX`, `.gridY` | Snapped center in texels. Edges may move only when these change |
| `sdsm.active`, `.rawNear`, `.rawFar`, `.nearDepth`, `.farDepth`, `.ageFrames` | Depth-range fit |

## Quick check

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=Shadow"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Shadow&debugView=CascadeColors"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
```

- Use the Game tab and `/activate_tab?name=Game` first. More: [HTTP cheat sheet](../../debugging/http-cheat-sheet.md).

## Limits
- Directional light only. Transparent receivers get no shadow.
- A surface that faces away from the sun counts as shadowed.
- Materials use the vertex normal. The mask uses a depth-derived normal. Tune `normalOffsetTexels` and `slopeBiasTexels` for acne or peter-panning.
- A lit pixel costs up to 2 cascades x `filterSamples` taps.
- A caster farther than `casterRange` from a cascade is clipped.
- A low sun gives long shadows and needs a large slope bias on coarse cascades.
- The depth range lags 2 frames. A resolution or SDSM switch re-samples shadows once.
- Settings are not saved in scene files.

## Related
- [Render features](README.md) · [Scene lighting](scene-lighting.md) · [Atmosphere](atmosphere.md) · [Volumetric light](volumetric-light.md)
- [GPU-driven rendering](../../architecture/gpu-driven-rendering.md) · [PBR materials](../../conventions/pbr-materials.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md)
