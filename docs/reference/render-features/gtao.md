# Gtao (ambient occlusion)
> Screen-space ambient occlusion (GTAO) from scene depth. For anyone tuning or debugging contact shadows.
> Verified against code: 2026-10-31

## TL;DR
- Reads only the scene depth. No normal or albedo buffer.
- Samples the horizon per pixel, then denoises (bilateral + temporal).
- The composite multiplies the linear HDR color: `color * mix(1, ao, strength)`.
- Runs before ToneMapping, so there is no sRGB step here.
- Off in the Scene view by default. On in the Game view.

## Where in the frame
| Registration | Stage | Priority | Toggle names |
|---|---|---|---|
| `Gtao.FrameStamp` | PreOpaque | 40 | `Always` (frame clock for snapshot age) |
| `Gtao.Occlusion` | PostOpaque | 50 | `Gtao.Main.Dispatch`, `Gtao.Resolve.Dispatch` |
| `Gtao.Composite` (`ScreenSpaceMask`) | PostComposite | -1500 | `Gtao.Composite.Draw` |

Code: `src/Features/Gtao/`. Shaders: `GtaoMain.comp`, `GtaoResolve.comp`, `GtaoComposite.frag`. Panel: **Gtao**.
The CPU reference is `GtaoMath.cpp`. The shaders mirror it.

### Toggle behaviour
| Toggle off | Result |
|---|---|
| `Gtao.Main.Dispatch` | No AO. Status `PassDisabled`. |
| `Gtao.Resolve.Dispatch` | Raw AO (noisy). History is dropped. |
| `Gtao.Composite.Draw` | AO is computed but not applied. `/get_texture` still returns it. |
| Feature off for one view | No pass in that view. Status `NotRendered`. |

## How it works
1. Each pixel is unprojected with `invViewProjection`. `fx::DeriveCameraBasis` gets the camera axes and pixel scale.
2. AO texel `i` reads depth pixel `i * divisor`. Divisor 2 gives half-size textures (rounded up).
3. `sliceCount` slices, `stepCount` steps per side. The sample radius in pixels follows view depth. Cap: 192 px.
4. Resolve: bilateral filter, then temporal blend with the previous frame.
5. History is used only if the last frame resolved, size and divisor match, and there was no camera cut.
6. Textures per view (RGBA16F): `Gtao.Raw`, `Gtao.History0`, `Gtao.History1`. `x` = visibility, `y` = view depth (0 = sky).

The camera basis, history rules and resolve block are shared with [Ssgi](ssgi.md) (`src/Core/FeatureKit/ScreenSpaceView.h`).

## Settings
Sanitized on write (`SanitizeGtaoSettings`). NaN and infinity become the default. Not saved in the scene file.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `radius` | 0.7 | 0.05..10 | World units |
| `falloffFraction` | 0.6 | 0.05..1 | Share of radius where occluders fade |
| `sliceCount` / `stepCount` | 2 / 3 | 1..8 / 1..12 | Quality. Steps are per side |
| `sampleDistributionPower` | 2.0 | 1..4 | Higher = samples closer to the pixel |
| `thinOccluderCompensation` | 0 | 0..0.7 | Less dark behind thin objects |
| `finalPower` / `minVisibility` | 1.6 / 0.03 | 0.5..4 / 0..0.5 | Contrast shaping |
| `strength` | 0.85 | 0..1 | How much AO darkens |
| `maxDistance` | 60 | 1..100000 | AO fades toward this view depth |
| `distanceFadeFraction` | 0.25 | 0..0.9 | Fade share of `maxDistance` |
| `resolutionDivisor` | 2 | 1 or 2 | Any value above 1 becomes 2 |
| `spatialDenoise` / `temporal` | true / true | bool | Bilateral filter / history blend |
| `historyWeight` | 0.9 | 0..0.98 | Temporal blend |
| `depthSigma` | 0.03 | 0.001..0.5 | Relative depth tolerance of the filters |
| `debugView` | `Off` | `Off`, `AoOnly` | Show only the AO |

## Debug keys
Name for `/feature_debug`: `Gtao`. Action: `reset_defaults` (no arguments). Every setting above is writable.

| Read-only key (prefix `game.` or `scene.`) | Meaning |
|---|---|
| `status` | `NotRendered`, `Ok`, `NoValidCamera`, `ShadersNotReady`, `TargetFailed`, `PassDisabled` |
| `frameIndex`, `historyUsed` | Last frame that ran. Was history blended |
| `aoExtent.w`, `aoExtent.h`, `divisor` | AO texture size |
| `screenRadiusAt5m` | Sample radius in depth pixels at view depth 5 |
| `textureBytes` | AO memory this frame |

Texture for `/get_texture`: `Gtao::Gtao.Raw.GameView` (also `.SceneView`, `Gtao.History0/1`).
`NotRendered` also means the view did not render for a few frames. A bad key or value gives `400` and nothing is applied.

## Quick check
```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=Gtao"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Gtao&debugView=AoOnly"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
```
The Game tab must be visible for `/get_game_view`, unless the Frame Debugger is enabled. Set `debugView=Off` after the test. More recipes: [HTTP cheat sheet](../../debugging/http-cheat-sheet.md).

## Limits
- The multiply also darkens direct light. No albedo, no multi-bounce. Bright surfaces look too dark.
- Normals come from depth. Edges and thin features can be wrong.
- No depth mip chain. Close surfaces with a big radius lose detail at the 192 px cap.
- Transparent surfaces write no depth. They get the AO of the opaque surface behind them.
- Perspective cameras only. Ortho or skewed gives `NoValidCamera` and one log line.
- A refused target resize is logged as an error. The old-size texture stays, status stays `Ok`, and AO is stretched at the lower and right edges.
- `TargetFailed` only means the target acquire returned an invalid texture.
- Memory per view: about 50 MB at 1080p with divisor 1, about 12 MB with divisor 2.

## Related
- [Render features](README.md) · [Ssgi](ssgi.md) · [Tone mapping](tone-mapping.md)
- [Render feature kit](../feature-kit.md) · [Writing a render feature](../../guides/writing-a-render-feature.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) · [HTTP debug routes](../../debugging/http-routes-debug.md)
