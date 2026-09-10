# ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md

### Child document 6 of 9 — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.
### Depends on: Phase 2 (`VolumeTexture` + RenderGraph 3rd resource kind), Phase 3 (`"AtmosphereTransmittanceLut"`), Phase 4 (`"AtmosphereMultiScatteringLut"`), Phase 5 (`AtmosphereFrameUniforms` resolution helper).

This is the pass that actually produces **aerial perspective** — everything
before this phase only produced sky-background data. This is also the second
highest-risk phase in the campaign (after Phase 2), since it is the first
real WORKLOAD exercising the new `VolumeTexture`/3rd-resource-kind
infrastructure Phase 2 only proved with a throwaway probe.

## Step 1: The Goal

Compute a small, camera-frustum-aligned 3D texture (the "camera volume" /
froxel volume — e.g. 32×32×32 per `ATMOSPHERE_REFERENCE_NOTES.md`) where each
voxel `(x, y, z)` stores the accumulated **in-scattered light (RGB)** and
**transmittance (alpha)** from the camera to that voxel's position — `x, y`
map to screen-space froxel columns (covering the view frustum's horizontal/
vertical field of view), `z` maps to view-space depth via a NON-LINEAR
slice distribution (more slices packed near the camera, where aerial
perspective changes fastest per unit depth). Phase 7's composite pass then
looks up, for each REAL on-screen pixel, the voxel at that pixel's
screen-space column and its actual depth-mapped Z slice, and blends:
`finalColor = opaqueColor * transmittance.rgb + inScattering.rgb`.

## Step 2: The Situation

- The froxel volume is genuinely PER-VIEW (Game View and Scene View have
  different cameras/frustums, exactly like the Sky-View LUT in Phase 5) —
  the same per-view decision Phase 5 made applies here too; default to one
  volume per view (`"AtmosphereAerialPerspectiveVolume_GameView"`/
  `"...SceneView"`) unless Phase 5's actual final decision went the other
  way, in which case mirror whatever it did for consistency.
- Per Phase 2's own Step 2 analysis (confirm its actual completion-report
  answer before starting this phase): the volume texture is almost certainly
  an IMPORTED, persistently-owned resource (mirrors `ComputeBlurValidation`'s
  `blurredOutput`), not a pooled/transient `CreateVolumeTexture()` one — its
  size is fixed and known at startup, never resized with the view panel
  (unlike `RenderTexture`, which DOES resize with its panel — this is a
  DELIBERATE, real difference: the froxel volume's X/Y resolution is a fixed
  froxel-grid size, e.g. 32×32, completely independent of the actual pixel
  resolution of the Game/Scene View panel it will later be sampled against
  in Phase 7 — the mapping from a real screen pixel to a froxel column is a
  simple `pixelUv * froxelGridSize` computation Phase 7 performs at
  composite time, not something this pass needs to know about at all).
- Computing this volume is naturally a "ray march per froxel COLUMN,
  accumulating through Z slices in order" workload — the standard technique
  (confirm against the actual cloned reference) dispatches one compute
  thread per `(x, y)` froxel column and has that SINGLE thread invocation
  loop over all Z slices internally, writing each slice's accumulated result
  via `imageStore()` as it goes (each Z slice's result depends on the
  PREVIOUS slice's accumulated transmittance/in-scattering — a genuine
  sequential dependency along Z, which is exactly why one thread owns a whole
  column rather than trying to parallelize across Z too). Confirm this
  matches the actual reference before assuming it — a per-voxel (fully 3D
  dispatch, no intra-thread Z loop) approach is also possible but would need
  a different accumulation strategy (e.g. computing transmittance-to-slice
  analytically rather than incrementally) and is meaningfully more complex;
  prefer the column-major, intra-thread-Z-loop approach unless the reference
  clearly does otherwise.
- This pass reads `"AtmosphereTransmittanceLut"` and
  `"AtmosphereMultiScatteringLut"` (both `sampler2D`, `ComputeShaderRead`/
  `ShaderRead` per Phase 3/4's own precedent) and writes the new
  `VolumeTextureHandle` (`ComputeShaderWrite`, `image3D`) — this is precisely
  the resource-usage combination Phase 2's plumbing must already support
  cleanly; if it does not, that is a Phase 2 gap to go back and fix, not
  something to work around here with a hack.

## Step 3: The Plan

- Extend `AtmosphereCommon.glsl` with the froxel volume's specific
  Z-slice<->view-depth mapping functions (`FroxelSliceToViewDepth()`/
  `ViewDepthToFroxelSlice()` — transcribed exactly from the reference's own
  non-linear distribution) and its X/Y<->view-ray-direction mapping (given a
  froxel column and the view's inverse projection/inverse view matrices,
  reconstruct the world-space ray direction that column represents).
- `src/Shaders/AtmosphereAerialPerspectiveVolume.comp` — `local_size_x =
  local_size_y = 8, local_size_z = 1` (one invocation per froxel COLUMN, per
  Step 2's own column-major design; confirm against
  `ATMOSPHERE_REFERENCE_NOTES.md`'s recorded resolution when picking the
  exact local size). Bindings: `AtmosphereParametersGpu`,
  `AtmosphereFrameUniforms` (this pass ALSO needs the view's inverse
  view-projection matrix to reconstruct ray directions per column — extend
  `AtmosphereFrameUniforms`, per Phase 1's own forward-looking note that it
  would likely grow, with an `invViewProjection` `mat4` field, or pass it as
  a separate small per-view uniform buffer/push-constant if that fits the
  existing convention better — either is fine, document the choice),
  `sampler2D transmittanceLut`, `sampler2D multiScatteringLut`, `image3D
  destinationVolume` (format matches Phase 2's `VolumeTexture`, e.g.
  `rgba16f`). Each invocation loops `z` from 0 to `depth-1`, converting `z`
  to a view-space depth via the shared mapping function, ray-marching from
  the PREVIOUS slice's end point to this slice's end point (accumulating
  in-scattering weighted by transmittance-so-far, then updating
  transmittance-so-far by this segment's own optical depth — sampling
  `transmittanceLut` for the segment's contribution rather than
  re-integrating from scratch), and `imageStore()`s the running
  `(inScattering.rgb, transmittance.r)` result at `(x, y, z)` before
  continuing to `z + 1`.
- `AtmosphereLutRenderer` gains
  `VolumeTextureHandle AddAerialPerspectiveVolumePass(RenderGraphBuilder&,
  Renderer&, const AtmosphereParametersGpu&, const AtmosphereFrameUniforms&,
  TextureHandle transmittanceLutHandle, TextureHandle
  multiScatteringLutHandle, const char* outputVolumeName)` — owns (lazily
  created, per view) its own `VolumeTexture`, `ComputePipeline`,
  `ComputeDescriptorSet`. Uses `RenderGraphBuilder::ImportVolumeTexture()`
  (Phase 2) plus `PassBuilder::WriteVolumeTexture(...,
  ResourceAccess::ComputeShaderWrite)`, and returns the resulting
  `VolumeTextureHandle` for Phase 7 to later `ReadVolumeTexture(...,
  ResourceAccess::ShaderRead)` from its own composite pass.
- Extend the running temporary validation call site to add this pass (Game
  View only is sufficient proof for this phase) right after the Sky-View LUT
  pass. Since `/get_texture` cannot capture a 3D volume directly (Phase 2's
  own explicit scope note), prove correctness here via: (a) confirming zero
  Vulkan validation errors/warnings through the whole sequence, and (b) a
  SMALL, throwaway CPU-side readback (a one-off `vkCmdCopyImageToBuffer` of
  the whole volume into a mapped staging buffer, read directly in C++ —
  mirrors Phase 2's own disposable-validation-readback technique) checking
  a small number of hand-picked voxels for plausibility (transmittance
  should be close to 1.0 at `z = 0` — right at the camera — and monotonically
  DECREASE as `z` increases for a fixed `x, y`; in-scattering should be close
  to 0 at `z = 0` and generally increase then plateau). Delete this
  throwaway readback code once confirmed — it is a one-time validation aid
  for THIS phase, not the permanent Phase 9 validation tool (which gets its
  own, properly-built version — see Phase 9).

## Step 4: What We Will NOT Do

- No configurable froxel grid resolution exposed to the Editor — fixed,
  hardcoded constants (documented in a shader/C++ comment), matching the
  reference's own defaults.
- No attempt at this phase to make the composite step (Phase 7) work yet —
  this phase's deliverable is the CORRECT VOLUME DATA existing and being
  provably plausible; consuming it is entirely Phase 7's job.
- No support for a froxel volume whose X/Y resolution matches the actual
  screen resolution 1:1 — the whole point of a froxel volume (vs. a
  per-pixel ray-march) is that it is DELIBERATELY much lower-resolution than
  the screen and relies on Phase 7's trilinear sampling (from the 3D
  texture's own `VK_FILTER_LINEAR` sampler, per Phase 2) to smoothly
  interpolate between froxels — do not "improve" this by increasing
  resolution to match the screen; that defeats the technique's entire
  performance rationale.

## Step 5: Their Role

- This is the phase most likely to surface a real gap in Phase 2's
  infrastructure (e.g. a barrier-planning edge case Phase 2's own simpler
  probe shader never exercised, since this is the first REAL, sequential-
  per-column workload). If you find one, fix it in `RenderGraph*`/
  `VolumeTexture` directly (going back into Phase 2's files) rather than
  working around it inside this pass's own code — note the fix explicitly in
  this phase's completion report so Phase 2's own document/completion report
  can be cross-referenced/updated for future readers.
- The plausibility checks in 3's readback step are this phase's real
  correctness gate, in the absence of a full CPU oracle for this LUT (same
  "visual/plausibility, not bit-exact" tier as Phase 4's Multi-Scattering
  LUT) — do not skip them just because they're more effort than a texture
  capture.
