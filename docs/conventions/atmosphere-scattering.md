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
| `AtmosphereSharedLut` | `PreOpaqueShared` (priority -10) | Transmittance LUT (256x256 RGBA8) and multi-scattering LUT (64x64 RGBA16F) as shared `PersistentTarget`s. Published through the `SharedLuts` channel that every view reads. |
| `AtmosphereViewLut` | `PreOpaque` (priority 0) | Per view: sky-view LUT (200x100 RGBA16F, `PersistentTarget`). Published through the `ViewLuts` channel (sky-view LUT and frame values). Gate: `AtmosphereSkyViewLutPass`. |
| `Atmosphere.SunLight` | `PreOpaque` (priority 5, `Gate::Always`) | Sun transmittance and sky light factors for materials and light shafts, published as `SunLightModulation` (`src/Game/Lighting/SunLightModulation.h`). |
| `AtmosphereAerialVolume` | `PostOpaque` (priority 20) | Per view: aerial perspective volume (128x128x32 RGBA16F, `PersistentVolume`) and shaft delta image (128x128 RGBA16F, `PersistentTarget`), shadowed by the Shadow `Receiver`. Published through the `AerialVolume` channel. Gate: `AtmosphereAerialPerspectiveVolumePass`. |
| `DrawSkyBackground` | `PostOpaque` (priority 100) | `ViewBackground`: a fullscreen draw into the view color where depth is still the clear value. Counted in the per-view draw statistics. |
| `AtmosphereComposite` | `PostComposite` (priority -1000, alpha over) | Aerial perspective composite compute into a `ViewOutput`, exposed as `GameViewComposited` / `SceneViewComposited`, then a blit to the private target. Reads the `AerialVolume` channel. |

Data flow per view: `SharedLuts` (shared) -> `ViewLuts` (sky background) and, with the Shadow `Receiver` (published in
`PreOpaque`), `AerialVolume` -> composite. A reader always sits in a later stage or at a larger priority than its
publisher. The aerial volume pass appears in the Frame Debugger under "Compute Dispatches (Post-GameView)".

Toggle names (Render Graph panel and `/render_graph/set_pass_enabled`): `AtmosphereTransmittanceLutPass`,
`AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`,
`DrawSkyBackground`, `AtmosphereAerialPerspectiveCompositePass`, `AtmosphereComposite.CopyToPrivateTarget`.
With the volume pass off nothing is published, so the composite is not declared and the view holds the plain scene color.

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
  (`AtmosphereParametersGpu`), `frame` (`AtmosphereFrameUniforms`, per view) and, in the aerial volume shader, `shadow`
  (`AerialShadowParamsGpu`, 384 bytes). Images and samplers keep their GLSL names: `transmittanceLutImage`,
  `multiScatteringLutImage`, `destinationImage`, `destinationVolume`, `destinationShaftDelta`, `transmittanceLut`,
  `multiScatteringLut`, `shadowDepth0..3`, `sceneDepth`, `skyViewLut`, `sourceColor`, `sourceDepth`,
  `aerialPerspectiveVolume`, `aerialShaftDelta`. Rename a variable and the chain logs the valid names.
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
- **Sky pixels only lose shaft light in the composite.** A pixel with no geometry (`rawDepth >= kClearDepthThreshold`, 0.999999,
  defined once in `src/Shaders/SceneDepthConstants.glsl`) becomes `sceneColor + shaftDelta * strength * skyIntensity`,
  clamped at 0. A zero delta returns the scene color exactly, the result is never brighter than the scene color, and the
  volume is ignored for sky. Geometry pixels use the volume: `scene * transmittance + inScattering`. The decision is a CPU
  oracle in `AtmosphereAerialPerspectiveCompositeMath.h/.cpp` (`IsClearDepth`, `ComputeAerialPerspectiveCompositeColor`)
  covered by Tier-1 tests; a test pins the CPU constant to the GLSL file. The composite push block is 112 bytes.
- **The aerial volume pass runs in `PostOpaque`, after the sun cascades exist.** `AtmosphereAerialVolume` (priority 20)
  fetches `SharedLuts` and the Shadow `Receiver` channel and tests the cascades per march sample through the shared
  `src/Shaders/OrthoMapSetVisibility.glsl` (one copy shared with `VolumetricLightInject.comp`; the includer defines the
  `ORTHO_*` macros first). In-scattering is shadowed by mountains. The multi-scattering LUT already holds sun shadowing and
  is not shadowed again. Air outside all cascades counts as lit. With no usable receiver the cascade count is 0 and no
  shadow tap runs.
- **The receiver is used only when it is safe.** `SunShadowReceiverUsable()` (`Game/Lighting/SunShadowReceiver.h`) must be
  true and `Dot(receiver.directionTowardSun, this frame's sun) >= 0.9999`. The state is `Used`, `Missing`, `Rejected` or
  `Mismatch` (`NotChecked` when the pass did not run). `Rejected` and `Mismatch` are reported once with `ReportOnce`.
- **Shaft delta.** The same pass writes a 128x128 RGBA16F image (`PersistentTarget`) with the physical, unexaggerated sun
  light that shadows remove along each column. It is always zero or negative and stops at the aerial max distance. The
  composite applies it to sky pixels. The dispatch always writes it, so it is valid whenever the volume is.
- **The pass publishes one record.** `AtmosphereAerialResult` (`AtmosphereFeature.h`: volume, shaft delta, max distance,
  depth exponent) goes out on the `AerialVolume` channel with a keep list, only after the dispatch was declared. The
  composite takes max distance and depth exponent from it, never from `AtmosphereSettings`. The pass binds the scene depth
  (`sceneDepth`) only so the `PostOpaque` callback declares a read of it. Each pass owns its own `GpuParams` object
  (`skyFrameParams`, `aerialAtmosphereParams`, `aerialFrameParams`, `aerialShadowParams` per view).
- **Auto scale picks the haze range.** `AtmosphereSettings::autoScale` (default true) feeds `ComputeAerialScale`
  (`AtmosphereAerialPerspectiveMath.h`) with `visibleFarKm`, the last cascade split divided by 1000 from a receiver whose
  `depthFitted` is true. With `x = visibleFarKm * 1.25`: `maxDistanceKm = clamp(0.5 * exp2(ceil(log2(x / 0.5) * 4 - 1e-4) / 4),
  0.5, 30)`, `exaggeration = clamp(4 / maxDistanceKm, 1, 8)`, `shadowBlend = smoothstep(0.5, 4.0, maxDistanceKm)`. Bad
  input gives 0.5 km, 8x, blend 0. The grid has 4 steps per octave, so values inside one cell never flicker. The state
  updates only with a fitted receiver; otherwise the last scale is held for up to 120 frames, then the settings values
  apply with blend 0. With no state yet the settings values apply with blend 0. A small scene (far about 0.04 km) gets
  0.5 km, 8x, blend 0, and the volume pass does zero shadow taps.
- **Manual mode.** With `autoScale` off the sliders apply and `shadowBlend` is 1. With it on, the max distance and
  exaggeration sliders are fallbacks only; the panel shows them disabled with the effective values. Depth exponent,
  samples and strength always apply.
- **The froxel tunables are `AtmosphereSettings` fields**, not shader constants: `aerialPerspectiveMaxDistanceKm`,
  `aerialPerspectiveDepthExponent`, `aerialPerspectiveSamplesPerSlice` (1..16), `aerialPerspectiveScatteringExaggeration`.
  The per-frame values reach the volume shader through `AtmosphereFrameUniforms`. They are live sliders in the "Atmosphere"
  panel (`Editor/AtmospherePanel.cpp`). The defaults (0.5 km, depth exponent 2.0, 8 samples, exaggeration 8.0) are the
  small-scene values; larger scenes rely on auto scale. The exaggeration multiplies only the extinction and scattering
  inside the volume shader; the shared oracle functions and the shaft delta are never scaled.
- **Settings are stored only through `SanitizeAtmosphereSettings()`** (`AtmosphereParameters.h`). `Settings()` is const and
  `ApplySettings(candidate)` sanitizes. A non-finite value becomes the field default. Ranges: planet radius 1..50000 km,
  atmosphere thickness 1..500 km, multi-scattering 0..10, Rayleigh and Mie scale height 0.01..100 km, Mie g -0.99..0.99,
  ozone center and half width 0..100 km, scattering and absorption colors and ground albedo 0..1, aerial strength 0..2, sky
  intensity 0..50, aerial max distance 0.01..50 km, depth exponent 1..4, samples 1..16, exaggeration 0.1..50. The panel
  edits a copy and calls `ApplySettings` when a value changed.
- **HTTP: `/feature_debug/state?name=Atmosphere` and `/feature_debug/set?name=Atmosphere&key=value`.** Writable keys:
  `sceneLightFollowsAtmosphere`, `autoScale`, `aerialPerspectiveStrength`, `aerialPerspectiveMaxDistanceKm`,
  `aerialPerspectiveDepthExponent`, `aerialPerspectiveSamplesPerSlice`, `aerialPerspectiveScatteringExaggeration`. One bad key
  (non-finite, text, negative integer, huge) voids the whole request with HTTP 400 naming the key; finite out-of-range floats
  are clamped by the sanitizer. Read-only keys per view (prefix `game.` or `scene.`): `active`, `sunTintR/G/B`,
  `ambientScale`, `frameIndex`, `aerial.maxDistanceKm`, `aerial.exaggeration`, `aerial.shadowBlend`, `aerial.cascadeCount`,
  `aerial.receiver` (`Used`, `Missing`, `Rejected`, `Mismatch`, `NotChecked`), `aerial.scaleHeld`, `aerial.frameIndex`.
  Writing a read-only key fails with `key is read-only`.
- **Limits.** Air outside all cascades is lit, so with depth-fitted cascades shadows of mountains beyond the farthest visible
  pixel do not reach sky rays. The shaft delta stops at the aerial max distance. World coordinates beyond about 100000 m from
  the origin lose shadow-lookup precision. The first frames after a start or a Shadow toggle use the settings values.
- **`AtmosphereSettings` and `DirectionalLight` are not serialized.** `Scene/SceneTextFormat.h` stores only primitive and
  mesh-asset sources. Settings live for the running session; apply them again after every scene load.
- **Per-pass GPU timing for these passes reads `Absent`** when the synchronous timing-slot budget is exhausted. Do not add
  `GpuTimingSlot` enumerators per atmosphere pass.
