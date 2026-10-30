# Render features

> Master table of the 8 built-in render features: order, toggles, data flow, defaults. Read this before any single feature page.
> Verified against code: 2026-10-30

## TL;DR
- 8 features ship in `src/Features/`. All use the feature kit (`fx::`), none touches Vulkan.
- Order in a frame: stage first, then priority (low number runs first).
- Features share data through channels, never by including each other.
- Settings live in memory only. Scene files do not save them. Set them again after a load.
- Every feature has a panel, a `/feature_debug` endpoint and a test folder `tests/Features/<Name>/`.

## Features

| Feature | Does | Page | Panel and debug name |
|---|---|---|---|
| `Atmosphere` | Sky, aerial perspective, sun light tint | [atmosphere](atmosphere.md) | `Atmosphere` |
| `Shadow` | Sun cascade shadow maps | [shadow](shadow.md) | `Shadow` |
| `SceneLighting` | Writes sun, ambient, shadow data for materials | [scene-lighting](scene-lighting.md) | `SceneLighting` |
| `ToneMapping` | Final exposure, curve, sRGB | [tone-mapping](tone-mapping.md) | `ToneMapping` |
| `Gtao` | Ambient occlusion | [gtao](gtao.md) | `Gtao` |
| `Ssgi` | One-bounce screen-space GI | [ssgi](ssgi.md) | `Ssgi` |
| `VolumetricLight` | Light shafts and fog | [volumetric-light](volumetric-light.md) | `VolumetricLight` |
| `GpuDrivenScene` | GPU cluster culling | [gpu-driven-scene](gpu-driven-scene.md) | Panel `Culling`, debug name `GpuDrivenScene` |

## Order in the frame

| Stage | Priority | Registration | Feature | Toggle names |
|---|---|---|---|---|
| PreOpaqueShared | -10 | `AtmosphereSharedLut` | Atmosphere | `AtmosphereTransmittanceLutPass`, `AtmosphereMultiScatteringLutPass` |
| PreOpaque | 0 | `AtmosphereViewLut` | Atmosphere | `AtmosphereSkyViewLutPass` |
| PreOpaque | 5 | `Atmosphere.SunLight` | Atmosphere | none (`Always`) |
| PreOpaque | 10 | `Shadow.Cascades` | Shadow | `Shadow.Cascade0..3.Draw`, `Shadow.Cluster.L<c>.P<s>.Reset/Cull/Draw` |
| PreOpaque | 20 | `SceneLighting.Uniforms` | SceneLighting | none (`Always`) |
| PreOpaque | 30, 35, 40, 40, 45 | `<Feature>.FrameStamp` | Shadow, Ssgi, Gtao, ToneMapping, VolumetricLight | none (`Always`) |
| Opaque | -10 | `GpuDrivenScene.SceneCull` | GpuDrivenScene | none (`Always`) |
| Opaque | 0 | `GpuDrivenScene.Opaque` | GpuDrivenScene | `GpuDrivenScene.*` passes |
| PostOpaque | 0 | `Shadow.Mask` | Shadow | `Shadow.Mask.Dispatch` |
| PostOpaque | 20 | `AtmosphereAerialVolume` | Atmosphere | `AtmosphereAerialPerspectiveVolumePass` |
| PostOpaque | 50 | `Gtao.Occlusion` | Gtao | `Gtao.Main.Dispatch`, `Gtao.Resolve.Dispatch` |
| PostOpaque | 55 | `Ssgi.Trace` | Ssgi | `Ssgi.Prepare/Trace/Resolve.Dispatch` |
| PostOpaque | 60 | `VolumetricLight.Volume` | VolumetricLight | `VolumetricLight.Inject/Temporal/Integrate.Dispatch` |
| PostOpaque | 100 | `DrawSkyBackground` | Atmosphere | `DrawSkyBackground` |
| PostOpaque | 100 | `Shadow.DepthRange` | Shadow | `Shadow.DepthRange.Reduce` (+ `.Clear`, `.Readback`) |
| PostComposite | -1600 | `Ssgi.Composite` | Ssgi | `Ssgi.Composite.Draw` |
| PostComposite | -1500 | `Gtao.Composite` | Gtao | `Gtao.Composite.Draw` |
| PostComposite | -1000 | `AtmosphereComposite` | Atmosphere | `AtmosphereAerialPerspectiveCompositePass`, `AtmosphereComposite.CopyToPrivateTarget` |
| PostComposite | -500 | `Shadow.Composite` | Shadow | `Shadow.Composite.Draw` |
| PostComposite | -250 | `VolumetricLight.Composite` | VolumetricLight | `VolumetricLight.Composite.Dispatch`, `...CopyToPrivateTarget` |
| PostComposite | 1000 | `ToneMapping` | ToneMapping | `ToneMapping.Dispatch`, `ToneMapping.CopyToPrivateTarget` |

- Blend of the composite entries: `ScreenSpaceMask` for Ssgi, Gtao, Shadow. `AlphaOver` for Atmosphere, VolumetricLight. `Replace` for ToneMapping.
- `PostTransparent` and `PreUI` stages are not used by any built-in feature.
- `Always` entries are not render passes. A pass toggle cannot switch them off.

## Data between features

```text
Atmosphere.SharedLuts --> Atmosphere.ViewLuts --> DrawSkyBackground
                      \-> Atmosphere.AerialVolume --> AtmosphereComposite
Atmosphere.SunLight --> SceneLighting.Uniforms
                    \-> VolumetricLight.Volume
Shadow.Cascades --> Shadow.Map --> Shadow.Mask --> Shadow.Composite
                \-> Shadow.Receiver --> SceneLighting.Uniforms
                                    \-> Atmosphere.AerialVolume
                                    \-> VolumetricLight.Volume
                \-> scene slots 0..3 --> material shaders (set 1, binding 0..3)
Gtao, Ssgi, VolumetricLight: Result channel from build pass to composite pass
SceneLighting.Uniforms --> scene uniform block, binding 8 --> every lit material
```

- Plain records in `src/Game/Lighting/` carry the shared data: `SunShadowReceiver.h`, `SunLightModulation.h`, `DirectionalLightResolver.h`.
- The first active ECS `DirectionalLight` is the sun. With none, a fixed placeholder sun is used.
- Atmosphere and VolumetricLight read the shadow receiver even in views that show no surface.

## Scene view defaults

| In the Scene view (secondary) | Features |
|---|---|
| Off at start | Gtao, Ssgi, VolumetricLight, Atmosphere aerial volume, Atmosphere composite |
| On at start | Shadow, SceneLighting, GpuDrivenScene, ToneMapping, sky background, Atmosphere LUTs, sun light |

- Turn one on: `curl.exe "http://127.0.0.1:8080/render_graph/set_feature_enabled?name=Gtao.Occlusion&enabled=true&view=secondary"`.
- `name` is a registration name from the table above. `view` is `primary` or `secondary`. No `view` means both.

## Rules for all features

| Rule | Meaning |
|---|---|
| Settings are sanitized | Each `Set` clamps values. A non-finite value becomes the default |
| `Set` is all or none | One bad key gives HTTP 400 naming the key. Nothing is applied |
| One source of truth | The panel and `/feature_debug/state` read the same snapshot |
| Snapshots go stale | Status reads `NotRendered` if the view did not render for a few frames, or its pass is off |
| Feature off vs pass off | `set_feature_enabled` skips the whole callback. `set_pass_enabled` toggles one pass name |
| Pass names are per name, not per view | Same name in two views shows two rows in `/render_graph` |

## Output textures

| Texture | From | Meaning |
|---|---|---|
| `GameViewComposited`, `SceneViewComposited` | Atmosphere | Linear HDR after aerial perspective |
| `GameViewVolumetricLight`, `SceneViewVolumetricLight` | VolumetricLight | Linear HDR after shafts |
| `GameViewToneMapped`, `SceneViewToneMapped` | ToneMapping | Display values, RGBA8 |

- Per-view feature textures are named `<Owner>::<Name>.GameView` or `.SceneView`. Ask `/list_textures` for the exact name.
- Read with `/get_texture?texture_name=...`. See the [HTTP cheat sheet](../../debugging/http-cheat-sheet.md).

## Related
- [Writing a render feature](../../guides/writing-a-render-feature.md) · [Feature kit reference](../feature-kit.md)
- [Render graph](../../architecture/render-graph.md) · [Render honesty rules](../../conventions/render-honesty.md)
- [HTTP debug routes](../../debugging/http-routes-debug.md) · [Frame debugger](../../debugging/frame-debugger.md)
