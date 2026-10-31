# Ssgi (screen-space GI)
> One bounce of diffuse light, traced in screen space. For anyone tuning or debugging indirect light.
> Verified against code: 2026-10-31

## TL;DR
- Adds one diffuse bounce from visible geometry to the linear HDR scene color.
- Reads scene color and scene depth only. No albedo or normal buffer.
- Cosine rays are marched across the screen. A hit reads the scene color there.
- Composite: `scene + albedo * GI * intensity`.
- Off in the Scene view by default. Turning it on there doubles the cost.

## Where in the frame
| Registration | Stage | Priority | Toggle names |
|---|---|---|---|
| `Ssgi.FrameStamp` | PreOpaque | 35 | `Always` (frame clock for snapshot age) |
| `Ssgi.Trace` | PostOpaque | 55 | `Ssgi.Prepare.Dispatch`, `Ssgi.Trace.Dispatch`, `Ssgi.Resolve.Dispatch` |
| `Ssgi.Composite` (`ScreenSpaceMask`) | PostComposite | -1600 | `Ssgi.Composite.Draw` |

Code: `src/Features/Ssgi/`. Shaders: `SsgiPrepare.comp`, `SsgiTrace.comp`, `SsgiResolve.comp`, `SsgiComposite.frag`, `SsgiCommon.glsl`. Panel: **Ssgi**.
The CPU reference is `SsgiMath.cpp`. The composite is first in the PostComposite chain, so [Gtao](gtao.md) and distance fade act on the lit image.

### Toggle behaviour
| Toggle off or state | Result |
|---|---|
| `Ssgi.Prepare.Dispatch` or `Ssgi.Trace.Dispatch` | Status `PassDisabled`. No GI. |
| `intensity` = 0 | Status `PassDisabled`. Nothing is declared. |
| `Ssgi.Resolve.Dispatch` | Raw GI (noisy). History is dropped. |
| `Ssgi.Composite.Draw` | GI is computed but not applied. `/get_texture` still returns it. |
| Shader not built | Status `ShadersNotReady`. Picture without GI. |
| Feature off for one view | No pass in that view. Status `NotRendered`. |

## How it works
1. **Prepare:** per GI texel, averages scene color (luminance capped by `maxRadiance`) and view depth into `Ssgi.Color`. Builds `Ssgi.Normal` from depth.
2. **Trace:** `rayCount` cosine rays per pixel, `stepCount` steps each. Result: `albedo * mean(hit radiance)`. A miss counts as 0.
3. **Hit rule:** a ray point at view depth `z` hits a surface at depth `s` when `bias < z - s < reach`. A back face blocks the ray.
4. **Bias:** the origin offset scales with the GI texel footprint, because view depth is a 16-bit float.
5. **Resolve:** bilateral 3x3 filter plus temporal blend. History is clamped to the filter footprint.
6. **Composite:** alpha 1 where geometry exists, alpha 0 on sky.

GI texel `i` reads depth pixel `i * divisor`. The camera basis and history rules are shared with Gtao (`src/Core/FeatureKit/ScreenSpaceView.h`).

Albedo is a guess: hue from the scene color, brightness from `albedoLuma`. The hemisphere ambient already lights open sky, so Ssgi only adds bounce.

Textures per view (RGBA16F, GI size): `Ssgi.Color` (rgb radiance, `a` view depth, 0 = no surface), `Ssgi.Normal`, `Ssgi.Raw`, `Ssgi.History0`, `Ssgi.History1`.

## Settings
Sanitized on write (`SanitizeSsgiSettings`). NaN and infinity become the default. Not saved in the scene file.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `intensity` | 1.0 | 0..4 | Bounce multiplier. 0 turns the feature off |
| `maxRayDistance` | 4.0 | 0.1..100 | World units a ray may travel |
| `thickness` | 0.5 | 0.01..5 | Depth band that counts as a hit |
| `depthBias` | 0.03 | 0..1 | Minimum ray origin offset |
| `rayCount` / `stepCount` | 2 / 12 | 1..8 / 4..48 | Quality. Rays per pixel, steps per ray |
| `stepDistributionPower` | 2.0 | 1..4 | Higher = steps closer to the pixel |
| `endFadeFraction` | 0.3 | 0..0.9 | Hit weight fades over this share of the ray |
| `maxRadiance` | 8.0 | 0.5..64 | Luminance cap of a light source |
| `albedoLuma` / `albedoSaturation` | 0.5 / 0.7 | 0..0.95 / 0..1 | Assumed receiver color |
| `maxDistance` | 80 | 1..100000 | GI fades toward this view depth |
| `distanceFadeFraction` | 0.25 | 0..0.9 | Fade share of `maxDistance` |
| `resolutionDivisor` | 2 | 1 or 2 | Any value above 1 becomes 2 |
| `spatialDenoise` / `temporal` | true / true | bool | Bilateral filter / history blend |
| `historyWeight` | 0.92 | 0..0.98 | Temporal blend |
| `depthSigma` | 0.03 | 0.001..0.5 | Relative depth tolerance of the filters |
| `debugView` | `Off` | `Off`, `GiOnly`, `Normals` | Show only GI, or the depth-derived normals |

## Debug keys
Name for `/feature_debug`: `Ssgi`. Action: `reset_defaults` (no arguments). Every setting above is writable.

| Read-only key (prefix `game.` or `scene.`) | Meaning |
|---|---|
| `status` | `NotRendered`, `Ok`, `NoValidCamera`, `ShadersNotReady`, `TargetFailed`, `PassDisabled` |
| `frameIndex`, `historyUsed` | Last frame that ran. Was history blended |
| `giExtent.w`, `giExtent.h`, `divisor` | GI texture size |
| `textureBytes` | GI memory this frame |

Texture for `/get_texture`: `Ssgi::Ssgi.Raw.GameView` (also `Ssgi.Color`, `Ssgi.Normal`, `.SceneView`).
`NotRendered` also means the view did not render for a few frames. A bad key or value gives `400` and nothing is applied.

## Quick check
```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Ssgi&debugView=GiOnly"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
curl.exe "http://127.0.0.1:8080/render_graph/set_feature_enabled?name=Ssgi&enabled=true&view=secondary"
```
The Game tab must be visible for `/get_game_view`, unless the Frame Debugger is enabled. Set `debugView=Off` after the test. More recipes: [HTTP cheat sheet](../../debugging/http-cheat-sheet.md).

## Limits
- Screen space only. Off-screen or hidden light does not bounce. GI changes as lights enter and leave the view.
- Albedo is a guess. Dark or strongly colored surfaces get the wrong bounce.
- Normals come from depth. Edges and thin features can leak.
- Not energy-correct. It adds on top of the hemisphere ambient. Lower the ambient to compare.
- One bounce. Transparent surfaces neither emit nor occlude.
- View depth is a 16-bit float. Far away, the depth step passes `thickness`. `maxDistance` keeps GI in range.
- Perspective cameras only. Ortho or skewed gives `NoValidCamera`.
- Each composite entry adds a full-size private target and a blend pass per view.
- Pass names are shared by both views. GPU timing shows the view that ran last.
- Memory per view: 5 textures x 8 bytes per GI texel (3 without resolve), plus the compositor targets.

## Related
- [Render features](README.md) · [Gtao](gtao.md) · [Tone mapping](tone-mapping.md)
- [Render feature kit](../feature-kit.md) · [Writing a render feature](../../guides/writing-a-render-feature.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) · [HTTP debug routes](../../debugging/http-routes-debug.md)
