# VolumetricLight (light shafts and fog)
> Sun light shafts and height fog in a froxel volume. For anyone tuning or debugging fog and god rays.
> Verified against code: 2026-10-30

## TL;DR
- A 160x90x64 froxel medium, lit by the sun and its cascade shadows.
- PostOpaque compute passes build and integrate the volume. A PostComposite compute pass applies it.
- Uses the sun cascades from [Shadow](shadow.md). Without them you get fog with no shafts.
- Off in the Scene view by default. On in the Game view.
- One of two features that use `fx::PersistentVolume` (the other is [Atmosphere](atmosphere.md)).

## Where in the frame
| Registration | Stage | Priority | Toggle names |
|---|---|---|---|
| `VolumetricLight.FrameStamp` | PreOpaque | 45 | `Always` (frame clock for snapshot age) |
| `VolumetricLight.Volume` | PostOpaque | 60 | `VolumetricLight.Inject.Dispatch`, `...Temporal.Dispatch`, `...Integrate.Dispatch` |
| `VolumetricLight.Composite` (`AlphaOver`) | PostComposite | -250 | `VolumetricLight.Composite.Dispatch`, `VolumetricLight.Composite.CopyToPrivateTarget` |

Code: `src/Features/VolumetricLight/`. Shaders: `VolumetricLightInject.comp`, `...Temporal.comp`, `...Integrate.comp`, `...Composite.comp`, `VolumetricLightCommon.glsl`. Panel: **VolumetricLight**.
The composite runs after the shadow darkening (-500), so the shadow mask never dims the shafts.

### Inputs
| Channel | From | Use |
|---|---|---|
| `Shadow.Receiver` | [Shadow](shadow.md) | Cascade maps for shadow taps. Read even in views that show no surface |
| `Atmosphere.SunLight` | [Atmosphere](atmosphere.md) | Sun tint and ambient scale. Also removes the night ambient floor |

The sun comes from the first active ECS `DirectionalLight`.

## How it works
1. **Inject:** per froxel: density, sun light with cascade shadow taps, ambient. Writes `VolumetricLight.Raw`.
2. **Temporal** (optional): blends with a history atlas (8x8 tiles of 160x90). Halton jitter, cycle 16. Writes `Blended`.
3. **Integrate:** front to back. Writes rgb in-scatter and `a` transmittance at each slice far boundary into `Integrated`.
4. **Composite:** reads scene color and depth, applies the volume, writes the `ViewOutput` texture, then blits to the private target.

Resources per view: 3 `PersistentVolume`s (`Raw`, `Blended`, `Integrated`, RGBA16F) and 2 history atlases.
Slice distance is `maxDistance * (slice / 64) ^ depthExponent`. Effective max distance is `min(maxDistance, last cascade far)` when cascades exist.

History is dropped on a camera cut, when the sun moves more than 1 degree (weight fades in between), or when any lit setting changes.
Any early exit also drops history. A disabled `Integrate` pass publishes nothing, so the composite does nothing.

## Settings
Sanitized on write (`SanitizeVolumetricLightSettings`). NaN and infinity become the default. Not saved in the scene file.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `enabled` | true | bool | Off gives status `Disabled` |
| `density` | 0.010 | 0..1 | Extinction per world unit |
| `albedo` / `anisotropyG` | 0.9 / 0.7 | 0..1 / -0.95..0.95 | Scatter ratio / forward scatter |
| `heightDensity`, `heightBase`, `heightFalloff` | 0, 0, 0.1 | 0..1, any Y, 0..2 | Extra fog near `heightBase` Y |
| `noiseAmount`, `noiseScale` | 0, 0.15 | 0..1, 0.001..4 | Density noise |
| `windX`, `windZ` | 0, 0 | -20..20 | Noise drift, units per second |
| `maxDistance` | 60 | 1..2000 | Fog range. Shadow coverage clamps it |
| `startDistance` | 0 | 0..`maxDistance` | Fog fades in from here |
| `depthExponent` | 2 | 1..4 | Slice spread. Higher = finer near the camera |
| `sunIntensity` | 1 | 0..16 | On top of the material sun scale |
| `ambientR/G/B` | 0.35/0.40/0.50 | 0..4 | Fog ambient color |
| `ambientIntensity` | 0.03 | 0..16 | Fog ambient strength |
| `ambientShadowedFraction` | 0.15 | 0..1 | Ambient kept in shadowed air. 1 ignores shadows |
| `shadowSamples` | 4 | 1, 2, 4, 8 | Shadow taps per froxel. Other values snap to the nearest |
| `shadowFilterTexels` / `shadowBiasTexels` | 1.5 / 1.0 | 0..8 / 0..16 | Shadow filter and bias |
| `temporal` / `jitter` | true / true | bool | History blend / Halton jitter |
| `historyBlend` | 0.9 | 0..0.98 | Temporal blend |
| `strength` | 1.0 | 0..1 | Composite strength. 0 draws nothing |
| `skyDistanceFraction` | 0.3 | 0..1 | Share of `maxDistance` fog that sky pixels see |
| `debugView` | `Off` | `Off`, `InScatterOnly`, `Transmittance` | Show one part of the volume |

## Debug keys
Name for `/feature_debug`: `VolumetricLight`. Action: `reset_defaults` (no arguments). Every setting above is writable.

| Read-only key (prefix `game.` or `scene.`) | Meaning |
|---|---|
| `status` | `NotRendered`, `Ok`, `Disabled`, `NoValidCamera`, `ShadersNotReady`, `TargetFailed`, `PassDisabled` |
| `receiver`, `shadowAvailable`, `cascadeCount` | `NotChecked`, `Used`, `Missing`, `Rejected`. Cascades in use |
| `effectiveMaxDistance` | Range used this frame |
| `historyUsed`, `jitterIndex`, `frameIndex` | Temporal state |
| `atmosphereLight`, `sunTintR/G/B`, `ambientScale` | Values taken from the Atmosphere channel |
| `textureBytes`, `grid` | Memory this frame. Grid size string |

Texture for `/get_texture`: `GameViewVolumetricLight` (also `SceneViewVolumetricLight`).
`NotRendered` also means the view did not render for a few frames. A bad key or value gives `400` and nothing is applied.

## Quick check
```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=VolumetricLight"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=VolumetricLight&density=0.05&debugView=InScatterOnly"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
```
Status `Ok` with `receiver=Used` means shafts are lit by shadows. Set `debugView=Off` after the test. More recipes: [HTTP cheat sheet](../../debugging/http-cheat-sheet.md).

## Limits
- With cascades present, fog range is clamped to the far plane of the last cascade.
- `kVolumetricLightSunDirectScale` (0.65) must equal `kDirectScale` of the materials.
- Night ambient floor is 15% unless the Atmosphere light is active.
- Wind offset wraps at 65536 world units.
- Without a usable receiver there are no shadow taps. The shader still needs valid depth textures, so scene depth is bound as a dummy.
- A view with no valid `invViewProjection` (or zero size) gives `NoValidCamera`.
- Memory per view: 3 volumes of 160x90x64 RGBA16F (about 7 MB each) plus 2 history atlases when temporal runs.

## Related
- [Render features](README.md) · [Shadow](shadow.md) · [Atmosphere](atmosphere.md) · [Tone mapping](tone-mapping.md)
- [Volumetric resources](../../conventions/volumetric-resources.md) · [Render feature kit](../feature-kit.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) · [HTTP debug routes](../../debugging/http-routes-debug.md)
