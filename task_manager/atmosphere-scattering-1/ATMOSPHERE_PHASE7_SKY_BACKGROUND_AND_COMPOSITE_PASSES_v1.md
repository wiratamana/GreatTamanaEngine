# ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md

### Child document 7 of 9 — see `ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md` for the full campaign map.
### Depends on: Phases 2-6 all landed and individually proven (via their own throwaway/temporary validation call sites).

**This is the phase that makes the feature actually VISIBLE and PERMANENT.**
Every prior phase (3-6) proved its own pass in isolation via a temporary,
clearly-marked `// TODO(ATMOSPHERE_PHASE7): relocate...` call site. This
phase's first job is deleting every one of those temporary call sites and
replacing them with the real, permanent, always-running sequence inside
`Application::Run()`'s actual per-frame render-graph build — then adding the
two NEW passes (sky background, aerial-perspective composite) that finally
consume everything.

## Step 1: The Goal

1. Wire the full LUT chain (Phases 3-6) into the REAL per-frame sequence for
   BOTH the Game View and Scene View, replacing every temporary validation
   call site.
2. Add a full-screen **Sky Background** pass: for every pixel where the
   scene's depth buffer shows "nothing was drawn here" (far plane / no
   geometry), sample the Sky-View LUT (Phase 5) and write a sky color.
3. Add a full-screen **Aerial Perspective Composite** pass (Locked Design
   Decision 2 from Phase 0): for every pixel, sample the aerial-perspective
   froxel volume (Phase 6) at that pixel's screen column + depth-mapped Z
   slice, and blend `finalColor = existingColor * transmittance.rgb +
   inScattering.rgb` — this single pass naturally covers BOTH real geometry
   (extinction/haze on opaque objects) AND the sky background pixels the
   previous pass just wrote (distant "sky" also passes through the near
   froxels' own transmittance/in-scattering, which is correct — the sky
   itself is understood as being "infinitely far," i.e. always samples the
   volume's farthest Z slice).

## Step 2: The Situation

- `Application::Run()` is the one place that already builds the real,
  per-frame Offscreen `RenderGraph::Execute()` regime for Game View + Scene
  View (see `AGENTS.md`'s Render Graph section and the existing
  `ComputeBlurValidation`/`FinalizeRenderTextureForExternalSampling()` call
  sites there for the exact pattern to extend) — this is the ONE place this
  phase's real, permanent wiring lives; read the CURRENT actual sequence
  there (order of Game draw / Scene draw / any post-process step) directly
  before inserting anything.
- The existing Game/Scene draw passes write their own color+depth via
  `RenderSystem::Draw()`/`Renderer::Submit()` into each view's own
  `RenderTexture` (`m_gameView`/`m_sceneView`, owned by `ImGuiEditorLayer` —
  see `AGENTS.md`'s "Editor Module Structure"). This phase's two new passes
  must run AFTER that draw (they need the real depth buffer + real opaque
  color already present) and BEFORE
  `FinalizeRenderTextureForExternalSampling()` (the call that transitions the
  view's texture to `ShaderRead` for ImGui's own sampling — see AGENTS.md's
  "Named Texture Capture" section on `NotifyDebugTextureStateOverride()` and
  its FOUR existing call sites; this phase's new composite pass's own final
  write is a FIFTH thing that needs the SAME correction call if it writes
  into a texture that gets externally sampled afterward by ImGui/`/get_texture`).
- Per Locked Design Decision 2 (Phase 0) and `ComputeBlurValidation`'s own
  precedent (a compute pass never reads and writes the exact SAME storage
  image in the same dispatch in this codebase's existing convention — it
  always writes into a SEPARATE output texture), the composite pass writes
  into a NEW texture (e.g. `"GameViewComposited"`/`"SceneViewComposited"`),
  distinct from the Game/Scene View's own original color texture — this new
  composited texture is what actually gets shown in the Editor's "Game"/
  "Scene" panels and returned by `/get_game_view`/a future equivalent, NOT
  the original pre-atmosphere color texture. Read `Panels/GamePanel.cpp`/
  `ScenePanel.cpp` and `ImGuiEditorLayer.cpp`'s existing descriptor-management
  code (the same "swap which descriptor ImGui::Image() is fed" pattern
  `showBlurredSceneOutput`/`blurredSceneOutputDescriptor` already
  establishes for the debug blur toggle) to see exactly how to swap the
  displayed descriptor over to this new composited texture PERMANENTLY
  (unlike the blur toggle, this is not optional/debug-only — once this phase
  lands, the composited output IS the real Game/Scene View output, always).
- The Sky Background pass needs to know which pixels have "no geometry" —
  this is a depth-buffer test (`depth == far` / `depth == 1.0` in this
  engine's depth convention — confirm the exact convention from
  `DepthBuffer.h`/`Pipeline.cpp`'s depth-compare setup) rather than any
  stencil/tag mechanism; a full-screen fragment shader (not a compute pass)
  with `gl_FragDepth`/an explicit depth-equality test in the fragment shader,
  drawn with `VK_COMPARE_OP_GREATER_OR_EQUAL`/`EQUAL` against the existing
  depth buffer (so it is trivially rejected by the depth test everywhere
  real geometry already exists, at full hardware speed, needing no manual
  per-pixel branch) is the natural, idiomatic Vulkan way to implement this —
  mirrors how `SceneGridRenderer` already draws a full-screen-ish effect
  depth-TESTED (never depth-WRITTEN) against real scene geometry (see
  `AGENTS.md`'s own "Scene" grid entry) — reuse that exact depth-test
  discipline rather than inventing a compute-shader-based "is this pixel
  empty" branch.

## Step 3: The Plan

### 3.1 — Permanent LUT-chain wiring

- In `Application::Run()`'s real per-frame block (or a small, new, dedicated
  free function/class this phase adds specifically to keep `Application.cpp`
  from growing further — e.g. `src/Application/AtmospherePassSequence.h/.cpp`,
  mirroring how `RenderPasses.h`/`.cpp` already exists as a dedicated home for
  Game/Scene/Present pass-building logic, per `AGENTS.md`'s Render Graph
  section — prefer creating this new file over growing `Application.cpp`
  further), call, in order, for EACH of Game View and Scene View:
  `AddTransmittanceLutPass()` (Phase 3; only needs to run ONCE per frame,
  shared across both views, not once per view — the Transmittance/
  Multi-Scattering LUTs are view-INDEPENDENT, unlike the Sky-View LUT/
  aerial-perspective volume, which are view-DEPENDENT; do not duplicate the
  shared LUTs per view), `AddMultiScatteringLutPass()` (Phase 4; also
  shared, once per frame), then PER VIEW: `AddSkyViewLutPass()` (Phase 5),
  `AddAerialPerspectiveVolumePass()` (Phase 6).
- Delete every `// TEMPORARY`/`// TODO(ATMOSPHERE_PHASEn)` marked block from
  Phases 3-6 — this phase is precisely when they get permanently relocated.
- Resolve `AtmosphereFrameUniforms` per view using Phase 5's helper, now
  wired to the REAL camera (Game View's active ECS `Camera`, Scene View's
  `EditorCamera`) — still using Phase 5's hardcoded sun-direction placeholder
  until Phase 8 lands (this phase does not need to wait for Phase 8; leave
  the same `// TODO(ATMOSPHERE_PHASE8)` marker Phase 5 already left, unless
  Phase 8 has already landed by the time this phase is implemented, in which
  case wire it for real now).

### 3.2 — Sky Background pass

- `src/Shaders/AtmosphereSkyBackground.vert` — a full-screen-triangle vertex
  shader synthesized purely from `gl_VertexIndex` (mirrors `SceneGrid.vert`'s
  own "zero mesh/vertex-buffer" technique exactly — reuse that same
  synthesis trick, do not create a new full-screen-triangle vertex buffer).
- `src/Shaders/AtmosphereSkyBackground.frag` — samples the Sky-View LUT
  (bound as `sampler2D`) using this pixel's own view-ray direction (derived
  from the pixel's NDC coordinate + the view's inverse view-projection
  matrix, pushed via push-constant/uniform), writes it as this pixel's color.
  Depth-tested (`VK_COMPARE_OP_GREATER_OR_EQUAL`/whatever this engine's exact
  convention resolves to for "only the empty far-plane pixels pass"), NEVER
  depth-WRITTEN (mirrors `SceneGridRenderer`'s own rule exactly).
- A small dedicated class, `src/Editor/AtmosphereSkyBackgroundRenderer.h/.cpp`
  (or, if `AtmosphereLutRenderer` already comfortably owns graphics
  pipelines too by this point, a method on it instead — decide based on
  which keeps responsibilities cleanest; a graphics `VkPipeline` is a
  meaningfully different kind of object than the compute-only pipelines
  `AtmosphereLutRenderer` has owned through Phases 3-6, so a SEPARATE class
  mirroring `SceneGridRenderer`'s own "bypass `Renderer::CreatePipeline()`/
  `Submit()` entirely, build a dedicated `VkPipeline` directly" pattern is
  the safer default) builds this pipeline once and issues one draw call
  INSIDE the Game/Scene View's own already-open `vkCmdBeginRendering`
  bracket, immediately after the real scene geometry draws (mirrors
  `SceneGridRenderer`'s own exact integration point and the
  write-after-write-hazard reasoning documented for it in `AGENTS.md`) —
  unlike `SceneGridRenderer` (Scene view only), this pass runs in BOTH Game
  View and Scene View.

### 3.3 — Aerial Perspective Composite pass

- `src/Shaders/AtmosphereAerialPerspectiveComposite.comp` — bindings:
  `sampler2D sourceColor` (the view's original color, post-Sky-Background-pass),
  `sampler2D sourceDepth` (the view's depth buffer, sampled — confirm the
  engine's depth attachment can be bound as a sampled input at this point in
  the frame; if not, this may need a small depth-buffer-to-sampled-copy step,
  or reading depth via a resolve/blit — check `DepthBuffer.h`'s real
  capabilities before assuming direct sampling works), `sampler3D
  aerialPerspectiveVolume`, `image2D destinationImage` (the new, separate
  `"GameViewComposited"`/`"SceneViewComposited"` output texture, per Step 2).
  For each pixel: reconstruct view-space depth from `sourceDepth`, map it to
  a froxel Z slice (`AtmosphereCommon.glsl`'s shared function from Phase 6),
  sample `aerialPerspectiveVolume` at `(pixelUv.xy, slice)` (trilinearly, via
  the `VolumeTexture`'s own `VK_FILTER_LINEAR` sampler), and blend as
  described in Step 1 above; `imageStore()` the result.
- `AtmosphereLutRenderer` (or a new sibling class if it is getting large by
  this point — use judgment) gains
  `TextureHandle AddAerialPerspectiveCompositePass(RenderGraphBuilder&,
  Renderer&, TextureHandle sourceColorHandle, TextureHandle sourceDepthHandle,
  VolumeTextureHandle aerialPerspectiveVolumeHandle, VkExtent2D extent, const
  char* outputTextureName)`.
- Wire this pass's output as the new "what the Game/Scene panel actually
  displays" texture (per Step 2's own note on updating `Panels/GamePanel.cpp`/
  `ScenePanel.cpp`/`ImGuiEditorLayer.cpp`'s descriptor plumbing), and add the
  matching `NotifyDebugTextureStateOverride()` call for it (per `AGENTS.md`'s
  "Named Texture Capture" rule — this is now a FIFTH call site, alongside
  the four already named there; update that `AGENTS.md` list too, but only
  in Phase 9, which owns all doc updates per Phase 0's workflow rule — for
  now just make sure the actual call exists and is correct).

## Step 4: What We Will NOT Do

- No change to `/get_game_view`'s/`/get_swapchain`'s own existing behavior
  beyond it now naturally capturing the POST-atmosphere composited texture
  (since that's what `m_gameView`/`m_sceneView`'s displayed content now is) —
  no new network endpoint is added in this phase.
- No attempt to make the aerial-perspective composite pass ALSO affect
  `AssetPreviewMesh`/`BoneViewerWindow`'s own separate offscreen renders — per
  Phase 0's own explicit refusal, those two keep their existing plain lambert
  preview, untouched.
- No user-facing intensity/strength slider for aerial perspective yet — that
  is Phase 8's Editor-controls job (`AtmosphereSettings`), not this phase's.

## Step 5: Their Role

- This is the phase where the feature becomes visually real for the first
  time end-to-end — budget real time for iterating on the depth-reconstruction
  math in 3.3 (view-space depth from a Vulkan `[0, 1]` non-linear depth
  buffer is a classic, easy-to-get-subtly-wrong step; sanity-check it in
  isolation, e.g. by temporarily visualizing the reconstructed depth as a
  grayscale gradient, before trusting the full composite blend on top of it).
- Once this phase lands, EVERY subsequent screenshot/capture a human or this
  engine's own tooling takes of the Game/Scene View includes the atmosphere
  effect — flag this clearly in the completion report so anyone reviewing
  prior/future screenshots understands why the visual output changed
  starting here.
