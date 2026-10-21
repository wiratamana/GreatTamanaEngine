# Atmosphere Scattering

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The engine has a physically based, real-time sky: transmittance and multi-scattering LUTs, a per-view sky-view LUT, an
aerial perspective froxel volume, a sky background and an aerial perspective composite. The math is hand-ported from
the reference implementation `hoffstadt/pl-sky` (never vendored). The feature is always compiled in; there is no CMake
switch for it. Follow these rules when touching it.

## Structure

`AtmosphereFeature` (`src/Features/Atmosphere/AtmosphereFeature.h/.cpp`) is an `fx::Feature` on the feature kit (see
[render-feature-authoring.md](render-feature-authoring.md)). It is registered with `GTE_REGISTER_RENDER_FEATURE`; its
editor panel reaches it with `fx::FeatureOf<AtmosphereFeature>`. FeaturePurityCheck has no Atmosphere entry.

| Registration | Stage | Work |
|---|---|---|
| `AtmosphereSharedLut` | `PreOpaqueShared` | Transmittance LUT (256x256 RGBA8) and multi-scattering LUT (64x64 RGBA16F) as shared `PersistentTarget`s. Published through a `Channel` that every view reads. |
| `AtmosphereViewLut` | `PreOpaque` | Per view: sky-view LUT (200x100 RGBA16F, `PersistentTarget`) and aerial perspective volume (128x128x32 RGBA16F, `PersistentVolume`). |
| `Atmosphere.SunLight` | `PreOpaque` (`Gate::Always`) | Sun transmittance and sky light factors for materials and light shafts, published as `SunLightModulation` (`src/Game/Lighting/SunLightModulation.h`). |
| `DrawSkyBackground` | `PostOpaque` | `ViewBackground`: a fullscreen draw into the view color where depth is still the clear value. Counted in the per-view draw statistics. |
| `AtmosphereComposite` | `PostComposite` (alpha over) | Aerial perspective composite compute into a `ViewOutput`, exposed as `GameViewComposited` / `SceneViewComposited`, then a blit to the private target. |

Toggle names (Render Graph panel and `/render_graph/set_pass_enabled`): `AtmosphereTransmittanceLutPass`,
`AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`,
`DrawSkyBackground`, `AtmosphereAerialPerspectiveCompositePass`, `AtmosphereComposite.CopyToPrivateTarget`.

Sky background replay: the Frame Debugger replays the Game view sky background through `rg::FullscreenReplay` (see
[frame-debugger.md](frame-debugger.md)). Composite output: the Frame Debugger shows the exposed
`ViewOutput` textures; `GET /get_texture?texture_name=GameViewComposited` captures one by name (linear scene color, shown
with the default tone map). `GET /get_game_view` captures the final Game view image (display values after ToneMapping).
When the composite pass is off, the view still holds the scene color of the earlier passes.

## Rules

- **`AtmosphereMath.h/.cpp` is the CPU oracle** for the density profile, optical depth, transmittance and phase
  functions. Every shader under `src/Features/Atmosphere/Shaders/` (through the shared `AtmosphereCommon.glsl`) is a GLSL
  transcription of it. If a shader and the oracle disagree, the oracle is right and the shader is fixed. Add the CPU
  math first when adding a LUT.
- **Shader binding names are the C++ contract.** The compute shaders name their storage blocks `params`
  (`AtmosphereParametersGpu`, one shared `GpuParams`) and `frame` (`AtmosphereFrameUniforms`, per view). Images and
  samplers keep their GLSL names: `transmittanceLutImage`, `multiScatteringLutImage`, `destinationImage`,
  `destinationVolume`, `transmittanceLut`, `multiScatteringLut`, `skyViewLut`, `sourceColor`, `sourceDepth`,
  `aerialPerspectiveVolume`. Rename a variable and the chain logs the valid names.
- **`AtmosphereParametersGpu` and `AtmosphereFrameUniforms` are read-only storage buffers** (`layout(std430, ...)
  readonly buffer`), never a uniform block. Scene Services binding 8 (`src/Shaders/DirectionalLighting.glsl`)
  is the engine's one uniform-buffer exception.
- **The aerial perspective volume is a real 3D `VolumeTexture`**, imported into the graph with `Tex3D` dimension and
  kept alive by `PersistentVolume`. A texture is only a graph root when kept; a pass whose only write is a volume must be
  kept or the compiler culls it.
- **Every texture dimension is one `rg::TextureHandle`.** The shape lives in `TextureDesc::dimension`, `depth` and
  `arrayLayers` (`src/Renderer/RenderGraph/RenderGraphTypes.h`). Owning classes are `Texture2D`, `VolumeTexture`,
  `TextureArray2D` and `RenderTexture`, all subclasses of `Texture`. A volume is import-only. Not supported: hardware
  depth-comparison sampler, mip-mapping, a debug-registry entry for arrays.
- **`GET /get_texture` resolves 2D and volume textures by name.** A volume capture is rendered on demand by a fixed-camera
  raymarch (`VolumeTexturePreviewRenderer`), not copied. The aerial perspective volume uses its own camera and tapering
  frustum proxy (`VolumeTexturePreviewMath.h/.cpp`), chosen by the `texture_name` prefix; other volumes use the generic
  box path. See [Networking](networking.md), "Named Texture Capture".
- **`DirectionalLight` (`src/ECS/Components/DirectionalLight.h`) drives the sun and every lit material shader.**
  `ResolveActiveDirectionalLight()` (`src/Game/Lighting/DirectionalLightResolver.h`) picks the first entity with
  `active == true`. Only one sun is supported. With no active light it returns a fixed placeholder sun, so a scene
  without a sun still shows a sky. Do not remove the fallback. SceneLighting and VolumetricLight scale the sun and
  the ambient with the published `SunLightModulation` record (next rule).
- **Scene light follows the atmosphere.** `Atmosphere.SunLight` (priority 5, `Gate::Always`) publishes a
  `SunLightModulation` (`src/Game/Lighting/SunLightModulation.h`) once per view: a per-channel `sunTint` and an
  `ambientScale`, both 1 for an overhead sun and 0 below the horizon. DirectionalLight colour and illuminance mean
  "sun straight overhead after the atmosphere". The math is `ComputeSunTransmittance` / `ComputeAtmosphereSunLight` in
  `AtmosphereMath`, evaluated at the camera height. The switch is `AtmosphereSettings::sceneLightFollowsAtmosphere`;
  off or invalid input publishes an inactive record and the consumers keep their own lighting. A sun below the horizon
  gives black light for materials and fog, while the sky background can still show twilight. A DirectionalLight that
  points up gives a black scene. The sky background and the aerial perspective composite already do their own
  transmittance and must NOT use the factors. Consumers call `SanitizeSunLightModulation` and include only the shared
  header. The channel has no toggle and no textures, so it needs no side-channel rule.
  `GET /feature_debug/state?name=Atmosphere` shows the published record per view; SceneLighting and VolumetricLight
  show the record they applied.
- **Aerial perspective is a full-screen post-process composite, never baked into a mesh shader.** `Mesh.frag`,
  `TexturedMesh.frag`, `MeshPreview.frag` and `SceneGrid.frag` stay free of atmosphere sampling. A feature that needs
  aerial perspective extends the composite pass.
- **Everything the feature writes is linear HDR radiance.** The sky background writes `skyViewLut * skyIntensity` (setting
  `AtmosphereSettings::skyIntensity`, default 6.0, a plain linear multiplier, clamped to stay finite in 16-bit float).
  It applies no curve. The composite output (`m_composited`) is RGBA16F and computes `scene * transmittance + inScattering`
  in linear space. The ToneMapping feature runs after it and owns exposure, the curve and the sRGB encode. The transmittance
  LUT stays RGBA8_UNorm.
- **The composite is a pass-through on pixels with no geometry** (`rawDepth >= 0.999999`, the clear depth). The sky
  background already finished those pixels. The decision is a CPU oracle in
  `AtmosphereAerialPerspectiveCompositeMath.h/.cpp` (`ShouldBypassAerialPerspectiveComposite`,
  `ComputeAerialPerspectiveCompositeColor`) and is covered by Tier-1 tests. The threshold constant must equal the shader
  literal.
- **The froxel tunables are `AtmosphereSettings` fields**, not shader constants: `aerialPerspectiveMaxDistanceKm`,
  `aerialPerspectiveDepthExponent`, `aerialPerspectiveSamplesPerSlice`, `aerialPerspectiveScatteringExaggeration`.
  They reach the volume shader through `AtmosphereFrameUniforms` and the composite shader through push constants. They are
  live sliders in the "Atmosphere" panel (`Editor/AtmospherePanel.cpp`). Shipped defaults: 0.5 km max distance, depth
  exponent 2.0, 8 samples per slice, scattering exaggeration 8.0 (real-Earth scale values are too faint at the
  distances the test content uses). The exaggeration multiplies only the extinction and scattering inside the volume
  shader; the shared oracle functions used by the LUTs are never scaled.
- **`AtmosphereSettings` and `DirectionalLight` are not serialized.** `Scene/SceneTextFormat.h` stores only primitive and
  mesh-asset sources. Settings live for the running session.
- **Per-pass GPU timing for these passes reads `Absent`** when the synchronous timing-slot budget is exhausted. Do not add
  `GpuTimingSlot` enumerators per atmosphere pass.
