# Shadow mapping (cascaded shadow maps with depth-range fitting)

The Shadow feature (`src/Features/Shadow/`) draws shadows of the active directional light with up to four cascaded
shadow maps. It fits the cascades to the camera every frame. No box has to be tuned by hand. It is a normal kit feature
(see `render-feature-authoring.md`): Vulkan-free, one callback per stage.

## Design

- **Cascades.** The visible depth range is split with the "practical" scheme (`splitLambda`: 0 uniform, 1 logarithmic).
  Each cascade is the bounding sphere of its frustum slice, computed relative to the eye and rounded up to 1/16 unit.
  The light-space center snaps to whole texels in double precision (`stabilizeCascades`), so edges do not shimmer.
  The near plane is pulled back toward the sun by `casterRange`. There is no depth clamp and no hardware depth bias.
- **Depth-range fitting (SDSM).** A compute pass finds the nearest and furthest visible pixel of the view's scene depth.
  The value is copied to the CPU with `fx::GpuReadback` and used two frames later (the cascades are drawn before the
  scene depth of the same frame exists). The range is padded (`sdsmPaddingFraction`), grows at once, shrinks with the
  time constant `sdsmShrinkSeconds` (real time from `Frame::DeltaSeconds()`) and is rounded onto a log grid
  (`sdsmQuantizeSteps` per octave). With `useSdsm` off, with any pass of the group off, with the shader not built, or
  with no sample for 120 frames, the cascades use fixed splits over `[camera near, maxDistance]`.
- **Caster culling.** Each cascade pass uses `CullToFrustum(casterCullPaddingFraction)`: an object is drawn only when its
  padded world box touches the cascade box. Meshes without bounds are never culled. Skinned meshes use rest-pose bounds, so
  raise the padding or set `cullCasters=false` for large animations.
- **Two receive paths, one algorithm.** `ShadowMask.comp` does cascade selection, normal offset, texel-scaled bias, Vogel-disk
  PCF, cascade blending and the distance fade for the screen-space mask. Material shaders sample the cascade maps (scene
  slots 0..3) with the same algorithm and remove only the sun term, so ambient light stays. `ShadowComposite.frag` draws the
  debug views, and the gray multiply of the final color when `applyInMaterials` is off.
- **Sun uniforms.** The `SceneLighting` feature writes the sun, the ambient and the shadow data into scene uniform binding 8
  for every view. `DirectionalLighting.glsl` falls back to a default sun when the block was never written. Turning
  Shadow off never turns lit materials black.
- **Resolution.** `mapResolution` (512, 1024, 2048, 4096) applies at once: `OffscreenTarget::Acquire` resizes each
  cascade target (one GPU idle wait). A failed allocation backs off and is logged once. Cascades above `cascadeCount`
  are trimmed to 4x4.

## Passes and textures

| Pass (toggle name) | Stage | Purpose |
|---|---|---|
| `Shadow.Cascade0.Draw` .. `Shadow.Cascade3.Draw` | PreOpaque | One depth draw per cascade |
| `Shadow.DepthRange.Clear`, `.Reduce`, `.Readback` | PostOpaque | Depth-range measurement (one unit) |
| `Shadow.Mask.Dispatch` | PostOpaque | Screen-space shadow mask and cascade id |
| `Shadow.Composite.Draw` | PostComposite | Debug views; with `applyInMaterials` off also the darkening |

Textures per view (`<Name>.GameView`, `<Name>.SceneView`): `Shadow.Cascade0` .. `Shadow.Cascade3` (depth),
`Shadow.Mask` and `Shadow.CascadeId` (R8). The `Map` channel, the `Receiver` channel and the four cascade scene slots are
published only when every active cascade pass was recorded, so a disabled cascade pass switches the mask and composite off.

## Material slot contract

The cascade depth maps are scene slots (`DeclareSceneSlot`, kind `Depth2D`, names `Shadow.Cascade0.Slot` .. `Shadow.Cascade3.Slot`).
The preferred index is the cascade index, a durable contract: material shaders read `layout(set = 1, binding = c)` for cascade
`c`. Bindings 4..7 stay free. An unpublished slot is a 1x1 dummy; the shader never reads a cascade at or above the count.
The `SceneLighting` feature reads the `Receiver` channel (`SunShadowReceiver`) and packs the cascade matrices, splits, bias and
filter values into the 448-byte scene uniform block (limit 512 bytes). `ShadowMask.comp` and `SunShadowAmount()` in
`DirectionalLighting.glsl` use the same algorithm.

## Settings

Edited in the dockable **Shadow** window or over HTTP. They are sanitized on write and not saved in the scene file.

| Key | Default | Range / note |
|---|---|---|
| `cascadeCount` | 4 | 1..4 |
| `mapResolution` | 2048 | snaps to 512, 1024, 2048 or 4096 |
| `maxDistance` | 150 | 1..100000, clamped to the camera far plane |
| `splitLambda` | 0.85 | 0..1 |
| `cascadeBlendFraction` / `distanceFadeFraction` | 0.10 / 0.10 | 0..0.5 |
| `casterRange` | 200 | 1..10000 |
| `cullCasters` / `casterCullPaddingFraction` | true / 0.25 | padding 0..2 |
| `constBiasTexels` / `slopeBiasTexels` / `normalOffsetTexels` | 1.5 / 1.0 / 1.0 | 0..16 |
| `filterRadiusTexels` / `filterSamples` | 1.5 / 4 | radius 0..8, samples 1, 4, 8 or 16 |
| `strength` | 1.0 | 0..1, fraction of the sun term removed in shadow (ambient stays) |
| `applyInMaterials` | true | material shaders apply the shadow; false = `Shadow.Composite` darkens the final color by `strength * mask` |
| `stabilizeCascades` | true | texel snapping |
| `debugView` | Off | `Off`, `CascadeColors`, `Mask` |
| `useSdsm` | true | off = fixed splits |
| `sdsmPaddingFraction` / `sdsmShrinkSeconds` / `sdsmQuantizeSteps` | 0.15 / 0.3 / 4 | 0..1 / 0..10 / 0..16 |

A NaN or infinite value is replaced by the default. The slope bias used by the shader is
`max(slopeBiasTexels, filterRadiusTexels)`.

## HTTP recipe

All routes are plain `GET` (see `networking.md`). The Game tab must be visible for `/get_game_view`.

```
POST /load_scene {}
GET  /activate_tab?name=Game
GET  /feature_debug/state?name=Shadow
GET  /feature_debug/set?name=Shadow&debugView=CascadeColors      then GET /get_game_view
GET  /feature_debug/set?name=Shadow&debugView=Off&mapResolution=1024
GET  /list_textures                                               cascade maps now 1024
GET  /get_texture?texture_name=Shadow.Cascade3.GameView&channel=depth
GET  /get_texture?texture_name=Shadow::Shadow.Mask.GameView
GET  /render_graph                                                draw_call_count per cascade pass
GET  /render_graph/set_pass_enabled?name=Shadow.DepthRange.Reduce&enabled=false
GET  /feature_debug/invoke?name=Shadow&action=reset_defaults
GET  /get_logs?min_level=warning
```

State keys: every setting above, plus read-only keys per view with the prefix `game.` or `scene.`: `status` (cascade
stage), `mask.status` (mask stage), `frameIndex`, `sunElevationDegrees`, `effectiveMaxDistance`, `resolutionInUse`,
`cascadeBytes` (size of the maps that really exist), `timing.deltaSeconds`,
`cascade<i>.sliceStart|sliceEnd|radius|texelWorldSize|depthRange|gridX|gridY` and `sdsm.active|rawNear|rawFar|nearDepth|
farDepth|ageFrames`. `gridX` and `gridY` are the snapped cascade center in whole texels: shadow edges may move only when
they change. A bad key or value is a 400 that names the key; nothing is applied. `reset_defaults` takes no arguments.
A status reads `NotRendered` when its view has not rendered for a few frames, or when its pass is toggled off.

## Limits

- Directional light only. Transparent receivers get no shadow. Alpha-tested casters are drawn opaque.
- A surface that faces away from the sun counts as shadowed.
- Material shadows use the interpolated vertex normal, not the depth-derived normal of the mask pass. Tune
  `normalOffsetTexels` and `slopeBiasTexels` when acne or peter-panning shows on curved surfaces.
- A lit fragment takes up to 2 cascades x `filterSamples` depth taps. Overdraw multiplies the cost.
- A caster further than `casterRange` from a cascade toward the sun is clipped. Raise `casterRange` for tall geometry.
- A low sun gives very long shadows and needs a large slope bias on coarse cascades.
- The depth range lags the camera by 2 frames. Padding and instant growth limit the effect.
- A resolution change or an SDSM switch changes the texel size once; shadows re-sample at that moment.
- A resolution change is refused while a view runs in the pipelined regime.
- Memory: 4 x 2048^2 x 4 bytes = 64 MB per view. 4096 is four times that.
