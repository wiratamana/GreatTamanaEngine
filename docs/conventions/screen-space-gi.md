# Screen-space GI (Ssgi)

The Ssgi feature (`src/Features/Ssgi/`) adds one bounce of diffuse light from visible geometry to the linear HDR scene color.
It reads only the scene color and depth. It is a normal kit feature (see `render-feature-authoring.md`): Vulkan-free, with a
PostOpaque callback for the compute work and a PostComposite callback for the draw. The CPU oracle is `SsgiMath.h/.cpp`;
the shaders mirror it.

## Design

- **Per pixel.** Cosine-weighted rays leave the surface along its normal. Each ray is marched across the screen. A hit reads
  the scene color of the surface it hits. The result is `albedo * mean(hit radiance)`; a miss counts as 0. The hemisphere
  ambient already lights the open sky, so Ssgi only adds bounce.
- **Shared screen-space code.** The camera basis, history rules and resolve block live in `Core/FeatureKit/ScreenSpaceView.h`.
  The depth, normal and reprojection helpers live in `src/Shaders/ScreenSpaceCommon.glsl`. Gtao uses the same code.
- **GI texel mapping.** GI texel `i` reads depth pixel `i * divisor`. With divisor 2 the GI textures are half size, rounded up.
- **Hit rule.** A ray point at view depth `z` hits a surface at depth `s` when `bias < z - s < reach`. `reach` is the
  thickness plus the depth advance of the step while the ray was in front of the surface. A back face blocks the ray.
  The origin offset and `bias` scale with the GI texel footprint, because view depth is stored in a 16-bit float.
- **Albedo and normals.** The engine has no albedo or normal buffer. Normals come from the depth buffer. Albedo takes its hue
  from the scene color and its brightness from `albedoLuma`.
- **Denoise.** A bilateral 3x3 filter and a temporal filter with the history clamped to the filter footprint.
- **Composite.** `scene + albedo * GI * intensity`, written with alpha 1 where geometry exists and alpha 0 elsewhere.

## Passes and textures

| Pass (toggle name) | Stage | Purpose |
|---|---|---|
| `Ssgi.Trace` (group `Ssgi.Prepare.Dispatch`, `Ssgi.Trace.Dispatch`, `Ssgi.Resolve.Dispatch`) | PostOpaque, priority 55 | Prepare, trace, resolve |
| `Ssgi.Composite` (`Ssgi.Composite.Draw`) | PostComposite, priority -1600, `ScreenSpaceMask` | Add and debug views |
| `Ssgi.FrameStamp` | PreOpaque, priority 35, always on | Frame clock for the snapshot age |

Textures per view (`<Name>.GameView`, `<Name>.SceneView`): `Ssgi.Color`, `Ssgi.Normal`, `Ssgi.Raw`, `Ssgi.History0`,
`Ssgi.History1`, all RGBA16F at GI size. `Ssgi.Color` holds radiance in `rgb` and view depth in `a` (0 = no surface).
Channel `Result` carries the texture shown, the normals, the divisor and the camera basis to the composite pass.

The composite is first in the PostComposite chain, so Gtao and the distance attenuation act on the lit image.
Every compositor entry costs two full-size RGBA16F targets per view and one blend pass. The compositor names that pass
`ProjectFeatureSlot<N>_Game_Blend` or `ProjectFeatureSlot<N>_Scene_Blend`; it carries no feature name.

## Toggle behaviour

- `Ssgi.Prepare.Dispatch` or `Ssgi.Trace.Dispatch` off: status `PassDisabled`, nothing is declared, the picture has no GI.
- `Ssgi.Resolve.Dispatch` off: the raw GI is shown (noisy) and the history is dropped.
- `Ssgi.Composite.Draw` off: the GI is still computed but not applied. `/get_texture` still returns it.
- `intensity` 0: status `PassDisabled`, no pass is declared.
- Disabled for a view scope (`enabled_primary` / `enabled_secondary` false; the secondary view is off by default): no pass is
  declared in that view and its status reads `NotRendered`.
- A shader that is not built gives `ShadersNotReady` and the picture without GI.

## Settings

Edited in the dockable **Ssgi** window or over HTTP. They are sanitized on write (`SanitizeSsgiSettings`) and not saved in
the scene file. A NaN or infinite float is replaced by the default.

| Key | Default | Range / note |
|---|---|---|
| `intensity` | 1.0 | 0..4; 0 turns the feature off |
| `maxRayDistance` | 4.0 | 0.1..100 world units |
| `thickness` | 0.5 | 0.01..5 |
| `depthBias` | 0.03 | 0..1 minimum ray origin offset |
| `rayCount` | 2 | 1..8 per pixel per frame |
| `stepCount` | 12 | 4..48 per ray |
| `stepDistributionPower` | 2.0 | 1..4 |
| `endFadeFraction` | 0.3 | 0..0.9 of the ray length |
| `maxRadiance` | 8.0 | 0.5..64 luminance cap |
| `albedoLuma` | 0.5 | 0..0.95 |
| `albedoSaturation` | 0.7 | 0..1 |
| `maxDistance` | 80 | 1..100000 view depth |
| `distanceFadeFraction` | 0.25 | 0..0.9 of `maxDistance` |
| `resolutionDivisor` | 2 | 1 or 2 (any value above 1 becomes 2) |
| `spatialDenoise` | true | bilateral filter |
| `temporal` | true | history blend and per-frame noise rotation |
| `historyWeight` | 0.92 | 0..0.98 |
| `depthSigma` | 0.03 | 0.001..0.5 relative depth tolerance |
| `debugView` | Off | `Off`, `GiOnly`, `Normals` |

## HTTP recipe

All routes are plain `GET` (see `networking.md`). The Game tab must be visible for `/get_game_view`.

```
POST /open_preset_scene {"preset":"sponza"}
GET  /activate_tab?name=Game
GET  /feature_debug/list
GET  /feature_debug/state?name=Ssgi
GET  /feature_debug/set?name=Ssgi&debugView=GiOnly                       then GET /get_game_view
GET  /feature_debug/set?name=Ssgi&debugView=Normals                      then GET /get_game_view
GET  /feature_debug/set?name=Ssgi&debugView=Off
GET  /get_texture?texture_name=Ssgi::Ssgi.Raw.GameView
GET  /render_graph
GET  /render_graph/set_pass_enabled?name=Ssgi.Composite.Draw&enabled=false
GET  /feature_debug/invoke?name=Ssgi&action=reset_defaults
GET  /get_logs?min_level=warning
```

State keys: every setting above, plus read-only keys per view with the prefix `game.` or `scene.`: `status`, `frameIndex`,
`giExtent.w`, `giExtent.h`, `divisor`, `historyUsed` and `textureBytes`. A status reads `NotRendered` when its view has not
rendered for a few frames. A bad key or value is a 400 that names the key; nothing is applied. `reset_defaults` takes no
arguments.

## Limits

- Screen space only. Light from off-screen or hidden surfaces does not bounce, and the GI changes as light sources enter and leave the view.
- Albedo is a guess. Dark or strongly colored surfaces get the wrong amount of bounce.
- Normals come from the depth buffer. Edges and thin features can show wrong normals and leaks.
- The GI adds light on top of the hemisphere ambient. It is not energy-correct; lower the ambient to compare.
- One bounce only. Transparent surfaces neither emit nor occlude.
- View depth lives in a 16-bit float. Beyond a few hundred units the depth step passes `thickness`; `maxDistance` keeps GI in range.
- Perspective cameras only. An orthographic or skewed camera gives status `NoValidCamera`.
- Ssgi runs in the Game view only by default (`DefaultSecondaryViewEnabled(false)`). Turn it on for the Scene view with
  `GET /render_graph/set_feature_enabled?name=Ssgi&enabled=true&view=secondary`; that doubles the cost.
- Memory per view: 5 textures x 8 bytes per GI texel (3 without the resolve pass), plus the two compositor targets.
- Pass names are shared between views, so the GPU timing of a pass shows the view that ran last.
