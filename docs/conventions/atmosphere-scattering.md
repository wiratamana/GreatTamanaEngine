# Atmosphere Scattering

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

A nine-phase campaign (`task_manager/atmosphere-scattering-1/`,
`ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md`, `ATMOSPHERE_CAMPAIGN_COMPLETION_REPORT.md`)
gave the engine a physically-based, real-time atmosphere-scattering + aerial-
perspective system, hand-ported from a cloned (never vendored, never
committed) reference implementation, `hoffstadt/pl-sky` — see `README.md`'s
own "Status" entry for the full user-facing rundown. Follow these rules
whenever touching this feature:

- **`src/Renderer/Atmosphere/AtmosphereMath.h/.cpp` is the PERMANENT CPU
  ORACLE for every density-profile/optical-depth/transmittance/phase-function
  formula this campaign uses — the exact same discipline this file already
  establishes for `Animation/VertexSkinning.cpp` under
  [GPU Vertex Skinning](gpu-vertex-skinning.md)
  above.** Every `.comp`/`.frag` shader under `src/Shaders/Atmosphere*` (via
  the shared `src/Shaders/AtmosphereCommon.glsl` include) is a faithful GLSL
  transcription of this file's own math, written and checked BY HAND against
  it — if a shader and this CPU oracle ever disagree, the CPU oracle is right
  by definition and the SHADER is what needs fixing, never the reverse. This
  is not just a stated rule: Phase 9's own
  `src/Editor/AtmosphereTransmittanceLutValidation.h/.cpp` tool (a "Validate
  Transmittance LUT" button in the Editor's "Atmosphere" panel, mirroring
  `GpuSkinningValidation`'s own proven pattern — this repository still has no
  live-`VkDevice`-requiring automated test infrastructure, see `TESTING.md`/
  `TODO.md`'s own "Tier 2" bucket) reads back the REAL, currently-computed
  `"AtmosphereTransmittanceLut"` texture and numerically compares every texel
  against this oracle, with a documented tolerance (0.01 — UNORM8
  quantization alone accounts for ~0.004 of it) that must never be loosened
  to make a genuine mismatch disappear; fix whichever side is actually wrong
  instead. A future contributor extending/adding a new LUT should add the
  matching CPU-oracle math here FIRST, the same order every phase in this
  campaign already followed.
- **The Render Graph's resource vocabulary now has a genuine THIRD kind,
  `VolumeTexture`/`VolumeTextureHandle`/`rg::ResourceKind::VolumeTexture`**
  (`src/Renderer/VolumeTexture.h`, `src/Renderer/RenderGraph/RenderGraphTypes.h`)
  — a real 3D (`VK_IMAGE_TYPE_3D`) Vulkan image, added specifically for the
  Aerial Perspective froxel volume. A `VolumeTextureHandle` can **never** be a
  `finalOutputs` root (that vector is texture-only) — a pass whose only write
  is a volume texture must call `RenderGraphBuilder::KeepVolumeTextureOutput()`
  explicitly, or `RenderGraphCompiler::Compile()` silently culls it every
  frame (a genuine infrastructure gap this campaign found and fixed — see
  `ATMOSPHERE_PHASE6_COMPLETION_REPORT.md`). There is deliberately still no
  `CreateVolumeTexture()` (pooled/transient) counterpart to
  `ImportVolumeTexture()` — this campaign's one and only volume-texture
  consumer is a fixed-size resource created ONCE (via
  `Renderer::CreateVolumeTexture()`) and re-imported fresh every frame,
  exactly like `ComputeBlurValidation`'s own persistent output — do not build
  `CreateVolumeTexture()` speculatively; only a genuine future need (more
  than one distinct volume texture with varying sizes across frames)
  justifies it. `RenderGraphDebugVolumeTextureRegistry`
  (`src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h/.cpp`,
  `network-impl-6` campaign) is the volume-texture sibling of
  `RenderGraphDebugTextureRegistry` (`GET /get_texture`/`GET /list_textures`,
  `network-impl-4` campaign) — auto-populated by
  `RenderGraph::ExecuteCompiledGraph()` exactly like the 2D registry, with
  zero opt-in required from whichever pass declared the volume texture (see
  [Networking](networking.md) above, "Named Texture Capture", for the endpoint's own full
  contract). `GET /get_texture` now transparently resolves EITHER kind by
  name — a volume capture is rendered FRESH, on demand, via a single,
  fixed-camera raymarch (`VolumeTexturePreviewRenderer`), never a raw pixel
  copy, since a 3D voxel grid has no direct 2D pixel representation to copy
  the way an ordinary 2D render target does. Phase 9's own small, permanent
  "debug slice" mirror
  (`AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()`,
  copying one Z-slice into a real, registered 2D texture,
  `"AtmosphereAerialPerspectiveVolumeDebugSlice"`) still exists and remains a
  perfectly valid, narrower way to get a literal-slice view of a volume
  texture — the two approaches are complementary, not redundant; this is no
  longer a "deliberate, out-of-scope non-goal" (see
  `task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md` for the full
  six-phase campaign that lifted this restriction).
- **`AtmosphereParametersGpu`/`AtmosphereFrameUniforms`
  (`src/Renderer/Atmosphere/AtmosphereTypes.h`) are ALWAYS bound as read-only
  STORAGE buffers (`layout(std430, ...) readonly buffer`), NEVER a true
  `uniform`/`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` block** — this engine has no
  UBO descriptor plumbing anywhere (confirmed by a fresh grep before every
  phase that added a new binding), and every atmosphere shader in this
  campaign follows this exact same convention, established in Phase 3's own
  "Revision Notes" and never deviated from since. A future new per-frame/
  per-session GPU-uniform-shaped struct for this feature must follow the same
  rule, not introduce this engine's first real UBO without a fresh, deliberate
  discussion.
- **`DirectionalLight` (`src/ECS/Components/DirectionalLight.h`) exists ONLY
  to drive the atmosphere's own sun direction/illuminance — it is explicitly
  NOT wired into `Mesh.frag`/`TexturedMesh.frag`/`MeshPreview.frag`'s existing
  fixed-direction lambert term, and this campaign adds no point/spot light of
  any kind.** `src/Renderer/Atmosphere/DirectionalLightResolver.h`'s
  `ResolveActiveDirectionalLight()` picks the FIRST entity (in
  `ComponentStorage<DirectionalLight>` order) with `active == true`, exactly
  mirroring `RenderSystem::ResolveActiveCameraViewProjection()`'s own
  "first active `Camera` wins" convention — never assume more than one
  simultaneously-active `DirectionalLight`/sun is supported, and never add
  multi-light blending without a fresh design discussion. Falls back to a
  fixed placeholder sun direction/color when no active `DirectionalLight`
  exists at all, so a scene with no Sun entity still renders a plausible sky
  — do not remove this fallback.
- **Aerial perspective is applied to already-rendered opaque geometry via a
  full-screen POST-PROCESS COMPOSITING PASS
  (`Shaders/AtmosphereAerialPerspectiveComposite.comp`,
  `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`) — it is
  NEVER baked directly into a forward mesh shader.** `Mesh.frag`/
  `TexturedMesh.frag`/`MeshPreview.frag`/`SceneGrid.frag` are all untouched by
  this entire campaign, and must stay that way — a future contributor adding
  a new mesh-shading feature that "also needs aerial perspective" should
  extend the composite pass (which already reads the scene's final color+
  depth), never add per-material atmosphere sampling to a forward shader.
- **The Aerial Perspective froxel volume's own max-distance/depth-exponent/
  samples-per-slice/scattering-exaggeration are real, Editor-tunable
  `AtmosphereSettings` fields, NOT hardcoded per-shader `const` literals** —
  `aerialPerspectiveMaxDistanceKm`/`aerialPerspectiveDepthExponent`/
  `aerialPerspectiveSamplesPerSlice`/`aerialPerspectiveScatteringExaggeration`
  (`AtmosphereTypes.h`), threaded through `AtmosphereFrameUniforms` (consumed
  by `AtmosphereAerialPerspectiveVolume.comp`) and the composite pass's own
  push constants (`AtmosphereAerialPerspectiveComposite.comp`), added by the
  `atmosphere-scattering-2` campaign's Phase 1
  (`task_manager/atmosphere-scattering-2/`) — previously three of these four
  were separate, disconnected hardcoded literals duplicated across THREE
  files with no single source of truth and no way to tune them without
  hand-editing GLSL and recompiling shaders. All four are live sliders in the
  Editor's "Atmosphere" panel (`Panels/AtmospherePanel.cpp`), mirroring
  `aerialPerspectiveStrength`'s own pre-existing pattern exactly — same
  "no persistence/serialization" limitation as every other `AtmosphereSettings`
  field (see this section's own "No scene (de)serialization" bullet below).
  **The shipped defaults changed from `pl-sky`'s own original 10km max
  distance / 1.0x exaggeration to `0.5` km max distance and `30.0`x
  scattering exaggeration** (Phase 3) — the original 10km/real-Earth-scale
  defaults left the effect ~2-3 orders of magnitude too faint to see at the
  few-meters-to-few-hundred-meters distances this engine's real test content
  actually lives at (a genuine SCALE MISMATCH, not a logic bug — see
  `task_manager/atmosphere-scattering-2/AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`,
  kept as permanent historical record). The `aerialPerspectiveScatteringExaggeration`
  multiplier is applied strictly LOCALLY inside
  `AtmosphereAerialPerspectiveVolume.comp`'s own per-sample
  extinction/scattering coefficients — `AtmosphereMath.h`/`AtmosphereCommon.glsl`'s
  shared oracle functions (`ComputeExtinctionCoefficientAtHeight()`/
  `RayleighDensityAtHeight()`/etc., also used unmodified by the Sky-View/
  Transmittance/Multi-Scattering LUTs) are never touched by this multiplier,
  keeping the sky's own physically-accurate rendering completely unaffected.
  A dedicated numeric CPU-readback inspection tool,
  `src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp` (Phase 5,
  mirroring `AtmosphereTransmittanceLutValidation`'s own proven shape),
  reports live min/max/mean transmittance and in-scattering magnitude plus a
  `likelyVisibleAtDefaultExposure` heuristic via an "Inspect Aerial
  Perspective LUT" button in the "Atmosphere" panel — a live post-Phase-3/4
  reading confirmed minimum transmittance dropped to ~0.71 (from ~0.9956
  pre-campaign) and maximum in-scattering magnitude grew to ~0.0039 (from
  ~4.6e-5 pre-campaign), both comfortably crossing the "likely visible"
  threshold. The `atmosphere-scattering-3` campaign's own Phase 1
  (`task_manager/atmosphere-scattering-3/PHASE1_ROOT_CAUSE_INSTRUMENTATION_AND_REGRESSION_TESTS.md`)
  extended this same tool to also report Near/Mid/Far per-band
  (`AerialPerspectiveBandSummary`) transmittance/in-scattering means, not just
  the whole-volume min/max/mean — this is what let that campaign confirm, as a
  permanent checked fact, that the LUT's own near/far gradient is real data,
  not merely a preview-rendering illusion.
- **The Aerial Perspective volume's HTTP/LLM-agent preview also had a SECOND,
  complementary bug beyond the color-interpretation fix above, found and fixed
  by the `atmosphere-scattering-3` campaign**
  (`task_manager/atmosphere-scattering-3/PHASE0_MASTER_STRATEGY.md`) — the
  generic volume-preview renderer's `ComputeVolumeCameraSetup()` sizes its
  raymarch proxy box directly proportional to the volume's own raw texel
  counts (correct for a genuine spatial volume, where every axis measures the
  same kind of physical length) and views it from a single, fixed, generic
  isometric camera angle, but the Aerial Perspective volume's three axes are
  NOT comparable units at all (X/Y are screen-space froxel column/row indices,
  Z is a camera-relative distance SLICE index) — feeding its real `128x128x32`
  dimensions into that formula squashed the one axis carrying its entire
  near/far story to 1/4 the size of the other two, producing a thin, nearly
  flat, unreadable preview even after the color-interpretation fix above. A
  SECOND, dedicated camera + proxy-shape setup function,
  `ComputeAtmosphereAerialPerspectivePreviewCameraSetup()` (Phase 2), plus a
  literal, tapering `FrustumProxy`/`IntersectRayFrustum()`/
  `MapFrustumLocalPositionToUvw()`/
  `ComputeAtmosphereAerialPerspectivePreviewFrustum()` (Phase 3, a MANDATORY
  deliverable) and `VolumeTexturePreview.comp`'s new `shapeMode` push-constant
  branch (all in `VolumeTexturePreviewMath.h/.cpp`/
  `VolumeTexturePreviewRenderer.h/.cpp`), auto-selected by the exact SAME
  `texture_name`-prefix check `Application.cpp` already uses for the
  color-interpretation mode above — zero new HTTP parameter, zero
  `Application.cpp` changes — now makes the preview genuinely WIDEN away from
  the camera with a legible near/far haze gradient, matching the reference
  paper diagram's own receding, hazier-with-distance fan-of-quads concept.
  Every OTHER (non-Aerial-Perspective) volume texture is completely
  unaffected — it still resolves through the ORIGINAL, byte-for-byte-unchanged
  `ComputeVolumeCameraSetup()`/`IntersectRayBox()` path, confirmed both by code
  inspection and by re-confirming a live, non-volume 2D capture
  (`GET /get_swapchain`) behaves identically throughout. Phase 4 of this same
  campaign then iteratively tuned the new camera/frustum's own
  distance-margin/angle/exposure constants against the reference image, never
  touching `kDensityScale`/`kStepCount` (shared with the generic path). See
  `task_manager/atmosphere-scattering-3/CAMPAIGN_COMPLETION_REPORT.md` for the
  full five-phase writeup.
- **This feature is ALWAYS compiled in — there is no `GTE_ENABLE_ATMOSPHERE`
  CMake switch, and there must never be one.** It is a core rendering
  feature, the same tier as the Render Graph or GPU Vertex Skinning (neither
  of which has an on/off switch of its own) — every `.comp`/`.vert`/`.frag`
  shader and every `.cpp` file this campaign added is registered
  unconditionally in `CMakeLists.txt`, including Phase 9's own debug-slice
  shader.
- **Per-render-graph-pass GPU timing remains generally unimplemented — this
  is a pre-existing, campaign-external gap, not something unique to
  atmosphere scattering, and Phase 9 deliberately did NOT attempt to close
  it.** Every atmosphere pass shows up correctly (name, resource read/write
  chips, correct ordering/culling) in the Editor's "Render Graph" panel
  because `RenderGraphSnapshot.h`'s `BuildRenderGraphSnapshot()` already
  reshapes ANY compiled graph generically — no atmosphere-specific code was
  needed for that — but its GPU-time column reads "N/A" for these passes
  exactly like every other render-graph pass today. Do not add new
  `GpuTimingSlot` enumerators for individual atmosphere passes to work around
  this — `GpuTimingService`'s fixed, hand-maintained slot set would need a
  broader, dedicated generalization (see `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`'s
  own "Still open" list) to support arbitrary per-pass timing at all, which
  is out of scope here.
- **No scene (de)serialization of `DirectionalLight`/`AtmosphereSettings`.**
  `Scene/SceneTextFormat.h`'s existing `PrimitiveSource`/`MeshAssetSource`-only
  format is NOT extended by this campaign — a `DirectionalLight` entity
  created via the Editor, and any tuned `AtmosphereSettings` value, exist only
  for the current running session, same as a `Camera` entity today (see
  `TODO.md`'s new "Atmosphere Scattering" section for the tracked follow-up).
- **The Aerial Perspective Composite pass (`Shaders/AtmosphereAerialPerspectiveComposite.comp`)
  is a PURE PASS-THROUGH for any pixel with no opaque geometry drawn into it
  this frame (`rawDepth >= 0.999999`, the frame's own clear-depth value) -
  see `task_manager/atmosphere-scattering-4/PHASE0_MASTER_STRATEGY.md`.**
  Before this campaign, the composite shader ran its full blend
  UNCONDITIONALLY, double-fogging a sky pixel the Sky Background pass had
  ALREADY finished, correctly, earlier in the same frame - confirmed and
  fixed by the `atmosphere-scattering-4` campaign (see
  `task_manager/atmosphere-scattering-4/AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md`
  for the original root-cause investigation). The bypass DECISION (not the
  volume's own trilinear sample/Z-slice math, which has no CPU equivalent -
  see that campaign's own Locked Design Decision 5) is codified as a small,
  dedicated, Tier-1-tested CPU oracle,
  `src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h/.cpp`'s
  `ShouldBypassAerialPerspectiveComposite()`/
  `ComputeAerialPerspectiveCompositeColor()` - deliberately its OWN new file,
  never added to `AtmosphereMath.h` (see that campaign's own Locked Design
  Decision 2 for why not). A permanent, automated regression guard,
  `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp`'s
  `ValidateAerialPerspectiveSkyPurity()` (a "Validate Aerial Perspective Sky
  Purity" button in the Editor's "Atmosphere" panel, mirroring
  `AtmosphereTransmittanceLutValidation`'s own proven shape), numerically
  confirms every sky pixel's post-composite color still exactly matches its
  pre-composite color, every session, on demand - a future edit that
  reintroduces double-compositing onto background pixels will show up here as
  a non-zero `mismatchingSkyPixelCount` rather than only being caught by a
  human eyeballing a screenshot. `AtmosphereSettings`'s own aerial-perspective
  tunables (`aerialPerspectiveMaxDistanceKm`/`aerialPerspectiveScatteringExaggeration`/
  etc.) were NOT changed by this campaign - they remain exactly as
  `atmosphere-scattering-2` shipped them, since they are correctly, and
  separately, tuned for real opaque geometry.
