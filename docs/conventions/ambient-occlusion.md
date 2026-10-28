# Ambient occlusion (GTAO)

The Gtao feature (`src/Features/Gtao/`) darkens creases and contact areas with screen-space ground-truth ambient occlusion.
It reads only the scene depth. It is a normal kit feature (see `render-feature-authoring.md`): Vulkan-free, one callback
per stage. The CPU oracle is `GtaoMath.h/.cpp`; the shaders mirror it.

## Design

- **Eye-relative world space.** The feature needs no view or projection matrix. Every pixel is unprojected with
  `invViewProjection` into the eye-relative world space of the frame. `fx::DeriveCameraBasis` (`ScreenSpaceView.h`) reads the camera axes and the
  pixel scale from that one matrix. A camera that is not a square-pixel perspective camera gives no AO
  (`NoValidCamera`, one log line).
- **AO texel mapping.** AO texel `i` reads depth pixel `i * divisor`. With divisor 2 the AO texture is half size, rounded up.
- **Sampling.** `sliceCount` slices with `stepCount` steps per side. Horizons are integrated analytically (cosine-weighted
  arc). The sample radius in pixels follows the view depth and is capped at 192 px.
- **Denoise.** A bilateral spatial filter (depth-aware) and a temporal filter. The history is reprojected with the previous
  view-projection, rejected on depth gaps, camera cuts and size changes, and blended by coverage.
- **Composite.** The AO multiplies the linear scene color after the scene is composed. The result is
  `color * mix(1, ao, strength)`. There is no sRGB path: tone mapping runs after this pass.

## Passes and textures

| Pass (toggle name) | Stage | Purpose |
|---|---|---|
| `Gtao.Occlusion` (group `Gtao.Main.Dispatch`, `Gtao.Resolve.Dispatch`) | PostOpaque, priority 50 | Sampling and denoise |
| `Gtao.Composite` (`Gtao.Composite.Draw`) | PostComposite, priority -1500, `ScreenSpaceMask` | Multiply and debug view |
| `Gtao.FrameStamp` | PreOpaque, priority 40, always on | Frame clock for the snapshot age |

Textures per view (`<Name>.GameView`, `<Name>.SceneView`): `Gtao.Raw`, `Gtao.History0`, `Gtao.History1`, all RGBA16F.
Layout: `x` = visibility, `y` = view depth (0 = sky). Channel `Result` carries the texture shown, the divisor and the camera
basis from the occlusion pass to the composite pass. The two history textures swap by frame parity.

## Toggle behaviour

- `Gtao.Main.Dispatch` off: no AO is computed and none is drawn. Status `PassDisabled`.
- `Gtao.Resolve.Dispatch` off: the raw AO is shown, with no denoise and no history. The history is dropped.
- `Gtao.Composite.Draw` off: the AO is still computed but not applied to the image. `/get_texture` still returns it.
- Disabled for a view scope (`enabled_primary` / `enabled_secondary` false): no pass is declared in that view and its status reads
  `NotRendered`. Gtao is off in the secondary view by default (`DefaultSecondaryViewEnabled(false)`).

## Settings

Edited in the dockable **Gtao** window or over HTTP. They are sanitized on write (`SanitizeGtaoSettings`) and not saved in
the scene file. A NaN or infinite float is replaced by the default.

| Key | Default | Range / note |
|---|---|---|
| `radius` | 0.7 | 0.05..10 world units |
| `falloffFraction` | 0.6 | 0.05..1 |
| `sliceCount` | 2 | 1..8 |
| `stepCount` | 3 | 1..12 per side |
| `sampleDistributionPower` | 2.0 | 1..4 |
| `thinOccluderCompensation` | 0.0 | 0..0.7 |
| `finalPower` | 1.6 | 0.5..4 |
| `minVisibility` | 0.03 | 0..0.5 |
| `strength` | 0.85 | 0..1 |
| `maxDistance` | 60 | 1..100000 view depth |
| `distanceFadeFraction` | 0.25 | 0..0.9 of `maxDistance` |
| `resolutionDivisor` | 2 | 1 or 2 (any value above 1 becomes 2) |
| `spatialDenoise` | true | bilateral filter |
| `temporal` | true | history blend and per-frame noise rotation |
| `historyWeight` | 0.9 | 0..0.98 |
| `depthSigma` | 0.03 | 0.001..0.5 relative depth tolerance |
| `debugView` | Off | `Off`, `AoOnly` |

## HTTP recipe

All routes are plain `GET` (see `networking.md`). The Game tab must be visible for `/get_game_view`.

```
POST /load_scene {}
GET  /activate_tab?name=Game
GET  /feature_debug/list
GET  /feature_debug/state?name=Gtao
GET  /feature_debug/set?name=Gtao&debugView=AoOnly                 then GET /get_game_view
GET  /feature_debug/set?name=Gtao&debugView=Off&resolutionDivisor=1
GET  /get_texture?texture_name=Gtao::Gtao.Raw.GameView
GET  /render_graph
GET  /render_graph/set_pass_enabled?name=Gtao.Composite.Draw&enabled=false
GET  /feature_debug/invoke?name=Gtao&action=reset_defaults
GET  /get_logs?min_level=warning
```

State keys: every setting above, plus read-only keys per view with the prefix `game.` or `scene.`: `status`, `frameIndex`,
`aoExtent.w`, `aoExtent.h`, `divisor`, `historyUsed`, `screenRadiusAt5m` (sample radius in depth pixels at 5 units) and
`textureBytes`. A status reads `NotRendered` when its view has not rendered for a few frames. A bad key or value is a 400
that names the key; nothing is applied. `reset_defaults` takes no arguments.

## Limits

- The multiply also darkens direct light. There is no albedo or multi-bounce term, so bright surfaces look too dark.
- Normals come from the depth buffer. Edges and thin features can show wrong normals.
- There is no depth mip chain. Large radii on close surfaces are capped at 192 px and lose detail.
- Transparent surfaces write no depth. The composite darkens them with the AO of the opaque surface behind them. They do not occlude anything.
- Perspective cameras only. An orthographic or skewed camera gives status `NoValidCamera`.
- The composite target is RGBA16F linear scene color, so there is no banding from the AO itself. Banding is handled by the ToneMapping dither.
- A size or divisor change drops the history; it is used again after a few stable frames.
- A target resize that is refused (for example while a view runs in the pipelined regime) is logged as an error. The old-size
  texture stays in use, the status stays `Ok`, and the AO is stretched at the lower and right edges (shaders clamp to the texture size).
- `TargetFailed` happens only when the target acquire returns an invalid texture. For example: a second request for the same
  target in one frame, a changed format, or a device without RGBA16F storage-image support.
- Memory per view: 3 textures x 8 bytes per AO texel. At 1080p that is about 50 MB with divisor 1 and 12 MB with divisor 2.
