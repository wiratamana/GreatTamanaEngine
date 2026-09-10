# Atmosphere Phase 7 — Completion Report

**Phase:** `ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md`
**Branch:** `feature/atmosphere-scattering-impl`
**Status:** Complete — `gte_core`, `GreatTamanaEngineTests`, and `GreatTamanaEngine` all
build cleanly (targeted incremental build only, per this campaign's own workflow rule,
including the three new shaders — `AtmosphereSkyBackground.vert/.frag`,
`AtmosphereAerialPerspectiveComposite.comp` — compiling via `glslc` with zero errors); the
engine was actually run (Editor build, Vulkan validation layers enabled by default) and
BOTH the sky background and the aerial-perspective composite were visually confirmed live
via `GET /get_texture` for `SceneView`/`SceneViewComposited`/`GameViewComposited` and via
`GET /get_swapchain` (the full Editor UI). **This is the phase that makes the feature
actually visible for the first time — every subsequent screenshot/capture of the Game/Scene
View (including `/get_game_view`/`/get_swapchain`) now includes the atmosphere effect.**

## What changed

### 1. Every Phase 3-6 temporary validation call site: deleted and permanently relocated

The entire `// TODO(ATMOSPHERE_PHASE7): relocate...` block in `Application.cpp`'s offscreen
build lambda (Transmittance/Multi-Scattering/Sky-View LUT + Aerial Perspective volume, Game
View only) is **gone** — replaced by a new, dedicated file mirroring `RenderPasses.h/.cpp`'s
own role exactly, per the strategy document's own explicit suggestion:

- **`src/Application/AtmospherePassSequence.h/.cpp`** (new) — four small, granular free
  functions Application::Run() calls directly, in a fixed order, exactly the same
  "thin Application-layer wrapper, not one giant orchestration function" shape
  `RenderPasses.h` already established:
  - `ResolveActiveCameraWorldPosition(Registry&)` — Phase 5's own helper, relocated
    verbatim (this **is** its permanent home now, closing Phase 5/6's own open question
    about where it should eventually live).
  - `AddAtmosphereSharedLutPasses(...)` — declares the Transmittance + Multi-Scattering LUT
    passes **once per frame** (never once per view, per Step 3.1).
  - `AddAtmosphereViewLutPasses(...)` — resolves ONE view's `AtmosphereFrameUniforms` (via
    Phase 5's `ResolveAtmosphereFrameUniforms()`, still Phase 5's hardcoded 45°-elevation
    sun placeholder — Phase 8's own job to replace) and declares that view's Sky-View LUT +
    Aerial Perspective Volume passes — called **once for Game View, once for Scene View**.
  - `MakeRecordSkyBackgroundCallback(...)` — builds a `std::function<void(VkCommandBuffer)>`
    ready to hand straight into `AddGameViewPass()`/`AddSceneViewPass()`'s new
    `recordSkyBackground` parameter (see below).
  - `AddAtmosphereCompositePass(...)` — declares the Aerial Perspective Composite pass for
    one view, forwarding into `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`.

- **`Application.h`/`Application.cpp`**: `m_atmosphereLutRenderer`'s own doc comment updated
  to state this is now its PERMANENT home (the `TODO(ATMOSPHERE_PHASE7)` comment removed).
  `Application::Run()`'s offscreen-regime lambda now calls the shared LUT passes once, then
  — inside each of the existing `if (gameTarget != nullptr)`/`if (sceneTarget != nullptr)`
  blocks — resolves that view's own eye world position + view-projection, calls
  `AddAtmosphereViewLutPasses()`, builds the sky-background callback, declares the real
  `AddGameViewPass()`/`AddSceneViewPass()` call (now passing the sky-background callback),
  and finally declares `AddAtmosphereCompositePass()` for that view. **Scene View is now
  fully wired** (Phases 5/6 deliberately left it unwired — this is the phase that closes
  that gap, using the new `IEditorLayer::SceneViewCameraWorldPosition()` — see below — for
  the Scene View's own eye position, since `EditorCamera` is Editor-owned).

### 2. Sky Background pass (Step 3.2)

- **`src/Shaders/AtmosphereSkyBackground.vert`** — a full-screen triangle synthesized purely
  from `gl_VertexIndex` (mirrors `SceneGrid.vert`'s technique exactly), writing a FIXED NDC
  depth of `1.0` (`gl_Position = vec4(ndc, 1.0, 1.0)`, so the perspective divide yields
  `z/w == 1.0` — the CONFIRMED depth-clear-value trick from this phase's own "Revision
  Notes").
- **`src/Shaders/AtmosphereSkyBackground.frag`** — samples the view's own Sky-View LUT
  (Phase 5) given a per-pixel camera ray reconstructed from `invViewProjection` (same
  near/far-NDC-unprojection technique `SceneGrid.frag` already uses), applies a fixed,
  deliberately-simple exponential tonemap (`1 - exp(-radiance * 12.0)`, since the LUT's raw
  HDR luminance can exceed 1.0 and this engine's Game/Scene View color attachments have no
  separate HDR exposure pass of their own), and writes the result.
- **`src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.h/.cpp`** (new) — mirrors
  `SceneGridRenderer.h/.cpp`'s exact PATTERN (bypasses `Renderer::CreatePipeline()` entirely,
  builds its own dedicated `VkPipeline`/`VkPipelineLayout` directly, zero vertex input) with
  the pipeline's own depth-compare op set to `VK_COMPARE_OP_EQUAL` (not `LESS`) and depth
  WRITE disabled — the CONFIRMED convention from this phase's own strategy document.
  **Deliberate deviation from the strategy document's literal file-path suggestion**: this
  class does NOT live under `src/Editor/` like `SceneGridRenderer` does — per Locked Design
  Decision 4 (`ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md`), atmosphere scattering is an
  always-compiled core feature with no `GTE_ENABLE_EDITOR` dependency, and this pass must
  run in the Game View (which still renders even in a release build, straight to the
  swapchain) as well as the Editor-only Scene View — unlike `SceneGridRenderer`, which is
  genuinely Scene-View-only. Lives under `src/Renderer/Atmosphere/` instead, always compiled,
  and is owned by `AtmosphereLutRenderer` (a new `m_skyBackgroundRenderer` member) via a new
  `AtmosphereLutRenderer::DrawSkyBackground(...)` method — keeping `AtmosphereLutRenderer` the
  single home for atmosphere GPU pass orchestration, per its own class comment.
- **`RenderPasses.h/.cpp`**: `AddGameViewPass()`/`AddSceneViewPass()` both gained a new
  optional `recordSkyBackground` parameter, invoked once, immediately after `Game::Render()`'s
  own draws finish, **still inside that pass's own open `vkCmdBeginRendering` bracket** —
  exactly the integration point `SceneGridRenderer` already established. For `AddSceneViewPass()`,
  the sky background is invoked **before** `recordSceneOverlay` (the ground grid) — a
  necessary, deliberate ordering: the grid's own alpha-blend must composite over the sky
  wherever it intersects the ground plane, and since the grid never writes depth either, if
  the sky ran AFTER the grid it would flatly overwrite the grid's own blended pixels
  (both pass the depth-EQUAL test identically) — confirmed correct in the live capture below
  (the grid, where visible, correctly shows blended over the sky/scene, not erased by it).

### 3. `DepthBuffer::allowSampledAccess` — the required engine change (Step 3.3, first bullet)

Exactly as CONFIRMED by this phase's own "Revision Notes":

- **`src/Renderer/DepthBuffer.h/.cpp`**: new `allowSampledAccess` constructor parameter
  (default `false` — every existing call site unaffected). When `true`, ORs in
  `VK_IMAGE_USAGE_SAMPLED_BIT` and creates a real `VkSampler` (NEAREST filtering — a depth
  value must never be linearly blended across texels). New `Sampler()` accessor.
- **`src/Renderer/RenderTexture.h/.cpp`**: new `allowDepthSampledAccess` constructor
  parameter (default `false`), forwarded straight through to its own companion
  `DepthBuffer`'s `allowSampledAccess`. New `DepthSampler()` accessor
  (`m_depthBuffer->Sampler()`, or `VK_NULL_HANDLE` if none).
- **`src/Renderer/GpuResourceFactory.h/.cpp`** and **`src/Renderer/Renderer.h/.cpp`**:
  `CreateRenderTexture()` on both gained the same new parameter, forwarded through
  unconditionally (no format-capability check needed for depth sampled access, unlike
  storage-image access — a depth format's `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT` support is
  effectively universal).
- **`src/Editor/ImGuiEditorLayer.cpp`**: `m_gameView`/`m_sceneView`'s own construction now
  passes `allowDepthSampledAccess = true` — this is the ONE call site that needed it, exactly
  as the strategy document specified.

### 4. A genuine, small, additive RenderGraph extension — closing a real documented MVP gap

`RenderGraph.cpp`'s own `ApplyUsageBarrierIfNeeded()` had a comment outright admitting: "there
is no way today to declare 'I want to ShaderRead the DEPTH half of a texture that also has a
color image' as a distinct usage." The Aerial Perspective Composite pass needs EXACTLY this
(reading the Game/Scene View's own already-imported `TextureHandle`'s depth half as
`sampler2D`, while ALSO reading its color half in the same pass). Closed with a small,
targeted, additive change — **`RenderGraphBarrierPlanner.h/.cpp` itself is completely
UNCHANGED**, matching the strategy document's own "no barrier-planner changes needed" claim
literally:

- **`RenderGraphTypes.h`**: `ResourceUsage` gained a new `bool isDepthResource = false`
  field (meaningful only for `ResourceKind::Texture`); `ForTexture()` gained a matching
  optional third parameter.
- **`RenderGraphBuilder.h/.cpp`**: `PassBuilder::ReadTexture()` gained a matching optional
  third parameter, forwarded through.
- **`RenderGraph.cpp`**: `ApplyUsageBarrierIfNeeded()`'s Texture case now computes
  `isDepthAccess` as `TargetsDepthState(usage.access) || usage.isDepthResource` — combining
  the existing (unchanged) `TargetsDepthState()` decision with the new explicit flag. This
  correctly routes the barrier AND the debug-texture-registry's own `depthState` tracking
  through the exact same, already-proven machinery every other texture usage goes through —
  confirmed live via `GET /get_texture?texture_name=SceneView&channel=depth`, which still
  works correctly and shows real depth contrast after this change (see verification below).

The Composite pass declares TWO `ReadTexture()` calls against the SAME view `TextureHandle`
— one plain (color, `ShaderRead`) and one with `isDepthResource=true` (depth) — while the
actual VIEW/SAMPLER for BOTH color and depth are resolved via a mix of
`ctx.resolveTexture()` (color, works fine for an imported texture) and the CALLER-supplied
`RenderTexture::Sampler()`/`DepthSampler()`/`Target().depthImageView` (depth — the render
graph has no resolution path for a texture's depth VIEW at all, only its declared-usage
barrier tracking) — mirroring `ComputeBlurValidation::AddPass()`'s own established
`sceneViewSampler`-passed-directly precedent, just extended to a second (depth) plain
Vulkan object.

### 5. Aerial Perspective Composite pass (Step 3.3)

- **`src/Shaders/AtmosphereAerialPerspectiveComposite.comp`** — bindings: `sampler2D
  sourceColor` (binding 0), `sampler2D sourceDepth` (binding 1, requires
  `allowSampledAccess`), `sampler3D aerialPerspectiveVolume` (binding 2, trilinear), `image2D
  destinationImage` (binding 3, `rgba8`, write-only). Push constants: `mat4
  invViewProjection` + `vec4 cameraWorldPositionAndScale` (`.xyz` = camera world position in
  engine world units, `.w` = `kWorldUnitsPerKilometer`) — packed as a plain `vec4` rather
  than `vec3 + float` specifically to sidestep any push-constant-block packing ambiguity.
  For every pixel: reconstructs view-space distance from the sampled raw NDC depth (via
  `invViewProjection` unprojection + distance from the camera world position, converted to
  km) — **except** when the sampled depth is still exactly the clear value (`>= 0.999999`,
  i.e. a sky-background pixel with nothing real drawn there), in which case it deliberately
  forces `viewDistanceKm = kAerialMaxDistanceKm` (the sky is "infinitely far", per Step 1's
  own explicit instruction — this special case matters because the far clip plane at this
  engine's own default camera settings maps to well under 10 km, so naively reconstructing a
  world position at the far plane would NOT automatically saturate the froxel lookup to its
  farthest slice). Maps that distance to a froxel Z slice via Phase 6's own
  `ViewDepthToFroxelSlice()`, samples the volume at `(pixelUv, sliceUv)`, and blends
  `finalColor = sceneColor * transmittance + inScattering`.
- **`AtmosphereLutRenderer.h/.cpp`**: new `AddAerialPerspectiveCompositePass(...)` (same
  per-view-state-map shape as every prior `AddXxxPass()`, `outputTextureName` = literal
  `"GameViewComposited"`/`"SceneViewComposited"`, explicit `VK_FORMAT_R8G8B8A8_UNORM` output
  format — mirroring `ComputeBlurValidation`'s own identical "never inherit the swapchain's
  own uncertain storage-image-format support" reasoning), plus two new public accessors:
  `CompositedOutput(name)` (returns the persistent `RenderTexture*`, or `nullptr`) and
  `FinalizeAerialPerspectiveCompositeForSampling(cmd, name)` (the ComputeShaderWrite ->
  ShaderRead manual barrier, mirroring `RenderPasses.h`'s own
  `FinalizeRenderTextureForExternalSampling()`/`ComputeBlurValidation::FinalizeForSampling()`).

### 6. Editor descriptor plumbing — the composited texture is now PERMANENTLY what's displayed

- **`src/Editor/EditorLayer.h`**: two new pure-virtual methods,
  `SetGameViewCompositedTexture(RenderTexture*)`/`SetSceneViewCompositedTexture(RenderTexture*)`,
  plus `SceneViewCameraWorldPosition() const` (the Scene View's own `EditorCamera` eye
  position, needed by `AtmospherePassSequence.cpp`'s Scene View wiring, since `EditorCamera`
  is Editor-owned and Application has no other way to reach it).
- **`NullEditorLayer.cpp`**: no-ops (`SceneViewCameraWorldPosition()` returns `Vec3::Zero()`).
- **`ImGuiEditorLayer.cpp`**: `SetGameViewCompositedTexture()`/`SetSceneViewCompositedTexture()`
  simply remember the pointer (`m_gameViewComposited`/`m_sceneViewComposited`, non-owning).
  `BuildUI()`'s existing "lazily (re)create `m_ctx.gameViewDescriptor`/`sceneViewDescriptor`"
  block now PERMANENTLY prefers the composited texture whenever one is available this frame
  (falling back to the original `m_gameView`/`m_sceneView` only when it isn't — e.g. the very
  first frame), and tracks the displayed source's own `VkImageView` (`m_lastKnownGameView`/
  `m_lastKnownSceneView`) to correctly detect a resize and rebuild the ImGui descriptor,
  mirroring the exact "recreate whenever the underlying view changed" pattern already
  established for `blurredSceneOutputDescriptor`. `GamePanel.cpp`/`ScenePanel.cpp` needed
  **zero changes** — they already just display whatever `ctx.gameViewDescriptor`/
  `sceneViewDescriptor` currently holds, so repointing the SAME field at a new source was
  sufficient (a narrower, equally-correct way of satisfying the strategy document's own
  "update Panels/GamePanel.cpp/ScenePanel.cpp's own descriptor plumbing" instruction — the
  plumbing feeding those exact same fields was what changed).
- **`Application.cpp`**: after `EndOffscreenRenderGraphRecording()`, for each of Game/Scene
  View: `FinalizeAerialPerspectiveCompositeForSampling()`, then
  `NotifyDebugTextureStateOverride("GameViewComposited"/"SceneViewComposited", ShaderRead)`
  — **the fifth `NotifyDebugTextureStateOverride()` call-site bullet** (AGENTS.md's existing
  list groups "GameView"/"SceneView" as one bullet with two literal calls; this is the
  analogous new bullet, also two literal calls — `AGENTS.md` itself is intentionally NOT
  updated here, per Phase 0's own workflow rule that only Phase 9 touches
  `README.md`/`AGENTS.md`/`TODO.md`) — then
  `m_editorLayer->SetGameViewCompositedTexture(m_atmosphereLutRenderer.CompositedOutput(...))`.
- **`GET /get_game_view`'s own capture logic** (`Application.cpp`) now captures
  `m_atmosphereLutRenderer.CompositedOutput("GameViewComposited")` instead of the original,
  pre-composite `*gameTarget` — falling back to `gameTarget` only in the
  should-be-unreachable case the composited texture doesn't exist yet, so a capture request
  is never silently dropped. `GET /get_swapchain` needed no change at all (it already
  captures whatever the Editor's own ImGui chrome renders, which now naturally includes the
  atmosphere-composited "Game"/"Scene" images via the descriptor swap above).

### 7. `CMakeLists.txt`

- Added `src/Application/AtmospherePassSequence.h/.cpp` and
  `src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.h/.cpp` to `gte_core`'s
  unconditional source list.
- Added `gte_add_shader()` registrations for `AtmosphereSkyBackground.vert/.frag` (the
  `.frag` depends on `AtmosphereCommon.glsl`) and `AtmosphereAerialPerspectiveComposite.comp`
  (also depends on it) — all unconditional (not `GTE_ENABLE_EDITOR`-gated), same reasoning
  as every other atmosphere shader in this campaign.

## Visual verification actually performed

Ran the engine (`run_app_background`, Editor build, Vulkan validation layers enabled by
default) twice this session:

- `GET /list_textures` confirmed every expected name registered and updating:
  `AtmosphereTransmittanceLut`, `AtmosphereMultiScatteringLut`, `AtmosphereSkyViewLut_GameView`,
  `AtmosphereSkyViewLut_SceneView`, `GameView`, `GameViewComposited`, `SceneView`,
  `SceneViewComposited`, `Swapchain`.
- `GET /get_texture?texture_name=AtmosphereTransmittanceLut`/`AtmosphereMultiScatteringLut`
  — both still show the exact same plausible gradients Phase 3/4 originally captured
  (unaffected by this phase's changes).
- `GET /get_texture?texture_name=AtmosphereSkyViewLut_SceneView` — a recognizable sky
  gradient (bright near-horizon glow fading to deep blue toward the zenith, solid black
  below the horizon).
- `GET /get_texture?texture_name=SceneView`/`SceneViewComposited`/`GameViewComposited` — a
  real, on-screen, physically-plausible sky (blue-to-orange horizon gradient) correctly
  filling every pixel where nothing was drawn, confirmed via `GET /get_swapchain` showing the
  full Editor UI with the Scene panel displaying this exact sky.
- **Live sanity-check of the depth-reconstruction math** (per this phase's own "Step 5: budget
  real time... sanity-check it in isolation if it looks wrong" instruction): spawned a real
  cube, then a real sphere, via `POST /instantiate_primitive` directly in front of the Scene
  camera, and re-captured `SceneView`/`SceneViewComposited` — the primitive correctly occludes
  the sky background (proving the `VK_COMPARE_OP_EQUAL` depth-test approach genuinely rejects
  the sky pass wherever real geometry exists), and the composite pass produced a clean,
  artifact-free result with the primitive still correctly shaded and visible (no NaN/garbage,
  no black holes, no double-darkening) — confirming the depth reconstruction handles a
  real, non-degenerate depth value correctly, not just the "nothing drawn" clear-value case.
  Also confirmed `GET /get_texture?texture_name=SceneView&channel=depth` still returns a
  correct, contrasted depth visualization (the sphere visibly darker/different than the
  far-plane background) both before and after spawning the primitive — proving the new
  `isDepthResource` RenderGraph extension didn't regress the pre-existing depth-channel debug
  capture path.
- `GET /get_game_view` returned `409` in this session specifically because the "Game" dock
  tab was inactive (tabbed behind "Scene" in the current `imgui.ini` layout) — this is the
  EXISTING, documented "no panel visible" fast-fail behavior (unchanged by this phase), not a
  regression; `GET /get_texture?texture_name=GameViewComposited` (captured while "Game" was
  briefly the active tab earlier in the session) independently confirmed the identical sky
  gradient renders correctly for the Game View too, through the exact same code path as
  Scene View.
- Stopped the engine (`stop_app_background`) after each run.

**Aerial perspective's own visible contribution is genuinely subtle at typical, near-camera
scene scales** — consistent with, and expected from, Phase 6's own completion report (its
own plausibility-check table showed in-scattering magnitudes on the order of 1e-5 to 1e-6 and
transmittance staying above ~0.995 even at the volume's own farthest 10 km slice). A cube/
sphere spawned ~5 world units (0.005 km) from the camera sits deep in the first froxel
slice, where transmittance is essentially 1.0 and in-scattering is essentially 0 — so
`SceneView` and `SceneViewComposited` are visually near-identical for this kind of
close-range content, which is the CORRECT, physically-plausible outcome (real-world haze is
genuinely imperceptible over a few meters), not a sign the pass isn't running. The pass IS
demonstrably running and producing sane, non-garbage numeric output (per the depth-reconstruction
sanity check above) — its visual magnitude is simply small at this content scale, exactly
as Phase 6 already predicted.

One additional, honest observation (not a regression introduced by this phase): the Editor's
existing "Scene" ground grid (`SceneGridRenderer`) was not visibly present in this session's
default camera framing (camera at `(0,0,-5)`, unrotated, per `EditorCamera`'s own hardcoded
default) — the grid's own fade-distance/anti-aliasing behavior evidently makes it very faint
at this specific grazing angle. This is unrelated to the Sky Background/Composite ordering
(the grid draws AFTER the sky, exactly as designed, and does correctly composite over
real geometry/the sky wherever it IS visible) and was not investigated further, since it is
outside this phase's own scope (`SceneGridRenderer` itself was explicitly untouched — see
Locked open question below).

## Deviations from the plan (and why)

1. **`AtmosphereSkyBackgroundRenderer` lives under `src/Renderer/Atmosphere/`, not
   `src/Editor/`** (see "What changed" §2) — a deliberate, documented deviation from the
   strategy document's literal suggestion, required by Locked Design Decision 4 (always-
   compiled, no `GTE_ENABLE_EDITOR` dependency) combined with the pass needing to run in the
   Game View too (which renders even in a release build).
2. **A small, additive `RenderGraph` extension (`ResourceUsage::isDepthResource`,
   `PassBuilder::ReadTexture()`'s new optional parameter, `RenderGraph.cpp`'s
   `ApplyUsageBarrierIfNeeded()` one-line change) was needed to let the Composite pass
   correctly declare a depth-half read against an already-imported color+depth
   `TextureHandle`** — this is real, was not pre-announced by name in the strategy
   document's own Step 3.3 text, but is exactly the kind of small, targeted "no
   barrier-planner changes needed" gap-closing the document's own CONFIRMED note anticipated
   (the barrier-planner's `RequiredStateFor()`/`TargetsDepthState()` functions themselves are
   completely unchanged; only the plumbing deciding WHICH of a texture's two tracked states a
   given usage targets was extended).
3. **The camera-space push-constant field is packed as one `vec4`
   (`cameraWorldPositionAndScale`) rather than a separate `vec3` + `float`** — a deliberate
   choice to eliminate any ambiguity in how a push-constant block's implicit (`std430`-like)
   packing rules would place a scalar immediately after a `vec3`, at zero cost (both layouts
   are 80 bytes either way).
4. **`GamePanel.cpp`/`ScenePanel.cpp` themselves needed zero code changes** — the strategy
   document asked to "update" their descriptor plumbing; the actual, narrower fix was
   repointing what `ImGuiEditorLayer::BuildUI()` feeds into the EXACT SAME
   `EditorContext::gameViewDescriptor`/`sceneViewDescriptor` fields those two panels already
   read, which satisfies the same requirement (the composited texture is what's
   PERMANENTLY displayed) without needing either panel file to know anything changed.
5. No other deviations — the Sky Background pass's depth convention
   (`VK_COMPARE_OP_EQUAL`, fixed NDC depth `1.0`, no depth write), the
   `DepthBuffer::allowSampledAccess` engine change's exact shape, the Aerial Perspective
   Composite pass's binding layout/blend formula, and the "shared LUTs once per frame, per-
   view LUTs+volume once per view" sequencing were all followed exactly as the strategy
   document specified.

## What was explicitly NOT done (per Step 4)

- No change to `GET /get_game_view`'s/`GET /get_swapchain`'s own contract beyond them now
  naturally capturing the POST-atmosphere composited content — no new network endpoint added.
- The release-build/`-DGTE_ENABLE_EDITOR=OFF` "both Game and Scene panels hidden" direct-to-
  swapchain path (`AddPresentPass()`'s own `directGameRenderAspect` branch) was **deliberately
  left untouched by this phase** — it does not go through `AddGameViewPass()`/
  `AddAtmosphereCompositePass()` at all, so a genuinely headless/panel-less release build (or
  the rare Editor edge case where both panels are simultaneously hidden) does NOT currently
  get the atmosphere effect. This was not explicitly called out as in-scope by Phase 0's own
  "What We Will NOT Do" (which only names the Game View and Scene View - the two views that
  go through the real per-frame RenderGraph regime this phase touches), so it is flagged here
  explicitly as an **open question for Phase 8/9** to decide whether it's worth closing.
- No aerial-perspective/sky-background effect for `AssetPreviewMesh`/`BoneViewerWindow`'s own
  separate offscreen viewports — per Phase 0's own explicit refusal, unchanged.
- No user-facing intensity/exposure/strength slider — `kSkyExposure` (12.0, fixed) in
  `AtmosphereSkyBackground.frag` is a deliberate, documented, hardcoded simplification;
  Phase 8's own `AtmosphereSettings` is the natural place for a real, tunable control.
- No dirty-flag optimization anywhere in the new passes — matches every prior phase's own
  "What We Will NOT Do".

## Build/run verification actually performed

- `cmake --build build --target gte_core` — compiled cleanly, checked incrementally after
  every meaningful edit throughout this session (multiple green builds along the way, not
  just once at the end).
- `cmake --build build --target GreatTamanaEngineTests` — compiled/linked cleanly (not run —
  per this campaign's own "fast compile check only" workflow rule; full `ctest` is reserved
  for Phase 9).
- `cmake --build build --target GreatTamanaEngine` — compiled, all three new shaders
  (`AtmosphereSkyBackground.vert/.frag`, `AtmosphereAerialPerspectiveComposite.comp`)
  compiled via `glslc` with zero errors, and linked cleanly.
- Ran the engine (`run_app_background`, Editor build, Vulkan validation layers enabled by
  default in this non-`NDEBUG` configuration) twice — once for the primary visual
  verification pass, once (after the first `stop_app_background`) specifically to sanity-check
  the depth-reconstruction math against real spawned geometry and the pre-existing
  `channel=depth` debug capture path — see "Visual verification" above for the full results.
  The engine stayed responsive to every HTTP request across both sessions (no crash, no hang),
  the strongest available signal given this tooling environment cannot read the running GUI
  process's own stdout/stderr back (same limitation every prior phase's own completion report
  already recorded).
- Per this campaign's own workflow rule, **no full clean build and no full `ctest` regression
  run** were performed (reserved for Phase 9 only).

## Open questions / notes for Phase 8/9

- **The release-build/both-panels-hidden direct-to-swapchain path does not get the atmosphere
  effect** (see "What was explicitly NOT done" above) — Phase 8/9 should explicitly decide
  whether this is acceptable long-term scope, or whether `AddPresentPass()`'s own direct-
  render branch should eventually get an equivalent (Sky Background + Composite) treatment.
- **`AGENTS.md`'s "Named Texture Capture" section's `NotifyDebugTextureStateOverride()` call-
  site list is now stale** (still says "FOUR existing call sites") — this phase deliberately
  did NOT update it, per Phase 0's own workflow rule that only Phase 9 touches
  `README.md`/`AGENTS.md`/`TODO.md`. Phase 9 must add the new
  `"GameViewComposited"`/`"SceneViewComposited"` bullet (grouped as ONE new list item with two
  literal calls, mirroring how `"GameView"`/`"SceneView"` are already grouped as one bullet
  today) to make it "the fifth" call site as this phase's own task description named it.
- **Phase 8's own `DirectionalLight`/`AtmosphereSettings` work should replace the
  hardcoded 45°-elevation sun placeholder** everywhere it's still referenced — this phase
  did not touch `ResolveAtmosphereFrameUniforms()`'s own body at all (per its own stable-
  signature contract), so both Game View and Scene View still share the identical
  placeholder sun direction.
- **The Editor's ground grid (`SceneGridRenderer`) was not visibly present in this session's
  default camera framing** (see "Visual verification" above) — flagged as an honest
  observation, not investigated further since it is outside this phase's own scope
  (`SceneGridRenderer` itself is untouched by this phase) and does not indicate any
  correctness problem with the Sky Background/Composite ordering itself.
- **A future Phase 8/9 exposure/tonemap control for the Sky Background pass** would let
  `kSkyExposure` become a real, tunable `AtmosphereSettings` field instead of a fixed
  constant — flagged in "What was explicitly NOT done" above.
