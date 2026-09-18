# GreatTamanaEngine

A raw game engine built from scratch.

## Documentation

This README is intentionally thin. Full architecture detail, the complete
project changelog, and contributor conventions all live under
[`docs/`](docs/README.md) — start at **[docs/README.md](docs/README.md)**.

## Goal

The plan is to develop a raw game engine from scratch, with the very foundation
built on **SDL3** (the new generation after SDL2) for window and event handling.

## Architecture

The very first architecture layers the engine like this:

```
SDL -> Application -> Window and Renderer -> Game
```

- **Application** is the only layer that knows about SDL directly. It owns the
  main loop and is responsible for initializing/shutting down SDL.
- **Window** and **Renderer** are custom objects that act as an abstraction
  layer on top of SDL. Other layers (like Game) interact with these custom
  objects instead of touching SDL directly.
- **Game** sits on top of Window and Renderer, and has no direct knowledge of
  SDL either.

At this stage, Window and Renderer will still internally depend on SDL
objects — that's okay for now. The abstraction can be tightened later as the
engine evolves.

Full detail on each of the six subsystems below lives under
[docs/architecture/](docs/architecture/) — see
**[docs/README.md](docs/README.md)** for the full documentation index.

### Event handling

SDL's raw event stream never reaches `Game` (or anything else past
`Application`) directly — every frame, `Application::Run()` polls SDL and
turns each event into the engine's own `gte::Event` vocabulary
(`EventTranslator`), which then feeds both `InputState` (continuous polling)
and `Game::OnEvent()` (discrete reactions).

Full detail: [docs/architecture/event-handling.md](docs/architecture/event-handling.md).

### Math

`src/Math/` (`Vec2`/`Vec3`/`Vec4`/`Mat4`/`Quat`) is a from-scratch math
library — no GLM dependency, the same "own the core data model" philosophy
as the hand-rolled ECS.

Full detail: [docs/architecture/math.md](docs/architecture/math.md).

### Rendering

`Renderer` owns a real Vulkan pipeline built on small RAII wrappers under
`src/Renderer/Vulkan/`, using dynamic rendering (no `VkRenderPass`/
`VkFramebuffer`), with `RenderOffscreen()`/`CreateRenderTexture()` powering
the Editor's "Game"/"Scene" panels and depth-tested, indexed, textured mesh
rendering.

Full detail: [docs/architecture/rendering.md](docs/architecture/rendering.md).

### Entity-Component-System (ECS)

The engine's Scene/World data model lives under `src/ECS/`: `Entity`/
`EntityManager`/`ComponentStorage<T>`/`Registry`, hand-rolled rather than a
third-party library, with `Transform`'s real parent/child hierarchy resolved
by `TransformHierarchy.h` and only `RenderSystem`/`MeshInstantiationSystem`/
`AnimationSystem` allowed to depend on both ECS and `Renderer`.

Full detail: [docs/architecture/ecs.md](docs/architecture/ecs.md).

### Asset Pipeline

`src/Assets/` implements this engine's unified binary asset container format,
`*.gta` ("Great Tamana Asset"), an `AssetDatabase` registry, and import
pipelines for PNG/JPG (→ KTX2 texture), MikuMikuDance `.pmx` (→ Mesh, with
bones/morphs/physics data and Guid-referenced material textures), and `.vmd`
motion (→ Animation).

Full detail: [docs/architecture/asset-pipeline.md](docs/architecture/asset-pipeline.md).

### Editor / Debug UI

An optional in-engine Editor module lives under `src/Editor/`, gated by
`GTE_ENABLE_EDITOR` — Dear ImGui docking, Hierarchy/Inspector/Scene/Game
panels, a transform gizmo, Memory/Profiler/Render Graph/Project panels, and
asset preview/Bone Viewer tooling, all behind the `IEditorLayer` interface so
`Game` never depends on the Editor in either direction.

Full detail: [docs/architecture/editor-debug-ui.md](docs/architecture/editor-debug-ui.md).

## Building

See **[BUILDING.md](BUILDING.md)** for prerequisites and build instructions.

## Testing

See **[TESTING.md](TESTING.md)** for how to build and run the test suite.

## Status

Early foundation stage, but past the basic-scaffolding phase for several
pieces. This section keeps only the most recent entries inline — see
**[docs/CHANGELOG.md](docs/CHANGELOG.md)** for the complete, reverse-
chronological project history from the very first triangle demo onward.

- **A follow-up campaign, `render-pass-3`, gave the engine a generic, opt-in
  `RenderPass` DECLARATION layer sitting strictly above the still-untouched
  `RenderGraphBuilder::AddRenderPass()` chokepoint** (five phases -
  `task_manager/render-pass-3/PHASE0_MASTER_STRATEGY.md`) - a new
  `RenderPipeline`/`RenderPassDesc`/`RenderPassProvider`/`RenderPassBlackboard`
  system (`src/Renderer/RenderGraph/RenderPipeline.h/.cpp`) turns "add a new
  pass" into "register a provider once at startup" instead of a hand-written
  free function called by name from `Application.cpp`'s own ever-growing
  frame-building function, a new opaque-keyed blackboard lets one provider
  (GPU Skinning) hand its output buffers to a completely unrelated provider
  (`"RenderOpaque"`) with zero shared/hand-threaded parameter (the campaign's
  required real proof case), and `Application.cpp`'s previously
  hand-duplicated Game View/Scene View `if` blocks collapsed into ONE generic
  per-view loop that both views now share - Scene View's own Opaque/Sky/
  Transparent draws are therefore real, separate passes today, mirroring Game
  View's own already-shipped shape, instead of one old fused `"SceneView"`
  pass. Every production pass this campaign covers (all six Atmosphere
  passes, Opaque, Sky, Transparent, GPU Skinning, Present, both views) now
  goes through this new system; the Frame Debugger's own replay passes and
  Compute Blur Validation's pass deliberately, permanently stay on the OLD,
  direct `AddRenderPass()` call style forever, since they only feed Frame
  Debugger tooling that itself never moved off the old `ViewScope`/
  `RenderPassCategory`/`RenderPassDrawKind` fields. A small, narrow, related
  fix (PHASE4) replaced the Frame Debugger's literal `"RenderOpaque"`-name
  pivot search with a structural, name-free `RenderPassEvent`-based lookup -
  everything else about the Frame Debugger, including its whole event-tree
  shape, is byte-for-byte unchanged. **This campaign LOUDLY, DELIBERATELY
  deviates from its own original design brief**
  (`task_manager/render-pass-3/GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`), which
  called for the render-graph builder's pass-adding entry point to eventually
  accept only new opaque types with the old `ViewScope`/`RenderPassCategory`/
  `RenderPassDrawKind` enums deleted once migration completed - by explicit
  user decision, that deletion never happens: those enums, and
  `AddRenderPass()`'s own existing overloads, remain permanently in place,
  with the new `RenderPipeline` layer translating into them internally
  instead of replacing them - see
  `task_manager/render-pass-3/CAMPAIGN_COMPLETION_REPORT.md`'s own dedicated
  section enumerating every one of the six Locked Design Decisions that
  reversed or amended the original design doc's stated defaults. Verified
  with a full build, a full `ctest` regression pass (1616 tests, 100%
  passing, one pre-existing environment-gated skip - up from `render-pass-2`'s
  own 1589 baseline), and a live, HTTP-driven smoke test confirming the
  Game-View Frame Debugger tree shape is completely unchanged, `"RenderOpaque"`
  still reports correct per-entity children and pipeline-state data, and the
  Scene View panel (via `SceneViewComposited`) now genuinely renders through
  its own real, separate Opaque/Sky/Transparent passes.
- **A follow-up campaign, `render-pass-2`, fixed a confirmed bug where the Frame
  Debugger's `"DrawSkyBackground"` row rendered flat with no expand arrow,
  looking like a stray, orphaned row belonging to no render pass** (four
  phases - `task_manager/render-pass-2/PHASE0_MASTER_STRATEGY.md`) - the
  confirmed root cause: `BuildRealFrameDebuggerSnapshot()`
  (`src/Editor/FrameDebuggerData.cpp`) built every real pass leaf other than
  `"RenderOpaque"` (every individual Atmosphere `"Compute LUT"` sub-pass, every
  Pre/Post-GameView compute dispatch, and `"DrawSkyBackground"` itself) as a
  single FLAT `FrameDebuggerEventNode` with no child - a genuine structural
  gap, not a display bug. Fixed by a new, purely-descriptive
  `rg::RenderPassDrawKind` enum (`DrawMesh`/`DrawQuad`/`Blit`) threaded through
  `AddRenderPass()`'s new trailing, defaulted parameter (`"DrawSkyBackground"`
  explicitly tagged `DrawQuad` - a real, hand-verified 3-vertex
  full-screen-triangle draw) plus a new shared `WrapPassWithOwnedChildEvent()`
  helper that turns EVERY real pass leaf into a "v PassName" parent owning
  exactly one real, independently-selectable child event row (`"Compute
  Dispatch"` for a Compute-kind pass; `"Draw Mesh"`/`"Draw Quad"`/`"Blit"` for
  a Graphics-kind pass, chosen purely by its own structural
  `RenderPassDrawKind` tag, never a pass-name string match) - `"RenderOpaque"`'s
  own already-correct per-entity-children shape left completely untouched.
  Verified with a full clean build, a full `ctest` regression pass (1589
  tests, 100% passing, one pre-existing environment-gated skip), and a live,
  HTTP-driven, screenshot-verified smoke test confirming `"DrawSkyBackground"`
  now expands to a `"Draw Quad"` child and every `"Compute LUT"` sub-pass now
  expands to a `"Compute Dispatch"` child, with both the pass-level row and
  its new child row independently selectable and showing correct, matching
  Inspector data. See `task_manager/render-pass-2/CAMPAIGN_COMPLETION_REPORT.md`
  for the full four-phase writeup.
- **A prior campaign, `render-pass-1`, replaced this engine's old ad-hoc
  `AddPass()`/`AddComputePass()` free-function sprawl with a single, official
  Render Graph pass-declaration chokepoint, `RenderGraphBuilder::AddRenderPass()`,
  and split the old monolithic `"GameView"` pass into three real, separate
  passes** (seven phases - `task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md`)
  - `"RenderOpaque"`, a brand-new `"DrawSkyBackground"` (previously hand-fused
  into `"GameView"`'s own execute lambda via a hardcoded
  `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()` hack), and a
  permanently-empty `"RenderTransparent"` scaffold for a future transparency
  system - every real pass in the engine (5 Atmosphere LUT passes, the Aerial
  Perspective Composite pass, GPU Skinning, `"Present"`, the Frame Debugger's
  own replay passes, Compute Blur Validation) now declared through this one
  chokepoint, tagged with new `PassKind` (`Graphics`/`Compute`) and
  `RenderPassCategory` (`General`/`AtmosphereLut`/`GpuSkinning`/`Debug`)
  metadata the Frame Debugger's own tree-building logic discovers
  generically, never via a hand-maintained name list. Verified with a full
  clean build, a full `ctest` regression pass (1578 tests, 100% passing, one
  pre-existing environment-gated skip), and a live, HTTP-driven,
  screenshot-verified smoke test confirming the new tree shape and
  `"DrawSkyBackground"`'s own correct, distinct pipeline-state Inspector data.
  See `task_manager/render-pass-1/CAMPAIGN_COMPLETION_REPORT.md` for the full
  seven-phase writeup.
- **A follow-up campaign, `frame-debugger-5`, fixed a confirmed bug where the
  Frame Debugger below only ever showed 3 of this engine's 8+ real
  compute-shader dispatches** (five phases -
  `task_manager/frame-debugger-5/PHASE0_MASTER_STRATEGY.md`) - every Atmosphere
  Transmittance/Multi-Scattering/Sky-View LUT pass, the Aerial Perspective
  Volume pass, the Aerial Perspective Volume Debug-Slice pass, and Compute
  Blur Validation's own pass were completely invisible in the event tree
  regardless of whether they ran that frame, since discovery only ever checked
  a hand-maintained GPU-Skinning name list plus one single hardcoded
  `"AtmosphereAerialPerspectiveCompositePass"` string. Fixed at the root:
  `RenderGraphBuilder::AddComputePass()` - the one real "choke point" every
  compute dispatch in this engine already funnels through - now stamps a real,
  structurally-tracked `PassRecord::isComputePass` flag (surviving into
  `RenderGraphPassSnapshot::isComputePass`/new `readKinds`/`writeKinds`
  per-entry resource-kind tags), so `FrameDebuggerData.cpp` can generically
  discover EVERY real, surviving compute pass and split them into two new
  tree groups positioned around the real `"GameView"` leaf by true execution
  order - `"Compute Dispatches (Pre-GameView)"` (GPU Skinning, every
  Atmosphere LUT pass, the Aerial Perspective Volume pass) and
  `"Compute Dispatches (Post-GameView)"` (the Aerial Perspective Composite
  pass) - REPLACING the old name-list/hardcoded-string special cases entirely,
  a deliberate, user-approved breaking change to the previously-shipped tree
  shape. Selecting any one of these leaves now shows THAT PASS'S OWN real
  output image instead of always the whole Game View: `FrameDebuggerHistory`
  eagerly retains a real GPU copy of every compute pass's own 2D-texture
  write, and a pass whose only visual write is a 3D volume texture (the
  Aerial Perspective froxel volume) gets a real ray-marched thumbnail by
  reusing the already-shipped `VolumeTexturePreviewRenderer`. Verified with a
  full clean build (both `GTE_ENABLE_EDITOR` configs), a full `ctest`
  regression pass, and a live, HTTP-driven, screenshot-verified smoke test
  confirming every atmosphere LUT compute pass now appears as its own
  selectable leaf, each showing its own correct, distinct real output image.
- **The Editor's "Frame Debugger" window is now a genuinely working,
  Unity-Frame-Debugger-style tool for the Game View, closing the whole
  `frame-debugger-2` GUI-only scaffolding's "manual-verification limitation"
  for good** (`frame-debugger-3` campaign, eight phases -
  `task_manager/frame-debugger-3/PHASE0_MASTER_STRATEGY.md`) - checking
  "Enable" now freezes and captures one real rendered frame's worth of real
  Render Graph passes (pass-level granularity - one leaf per relevant real
  pass, a deliberate, permanent divergence from Unity's own per-draw-call
  detail, not a gap), the left-hand tree shows those real passes instead of
  "No frame captured yet.", clicking one shows real shader/blend/Z/stencil/
  texture/vector/matrix data (`FrameDebuggerCapture.h/.cpp`'s new,
  zero-overhead-when-disarmed capture context threaded through
  `Renderer::Submit()`/`RenderSystem::Draw()`) plus a real preview image
  reconstructed as of that exact point in the frame, a new 8-slot Frame
  History ring buffer (`FrameDebuggerHistory.h/.cpp`) lets you step backward/
  forward through past captured frames each with its own retained GPU
  texture copy, and the Channels (All/R/G/B/A)/Levels controls now actually
  affect the preview image via a dedicated compositing shader
  (`FrameDebuggerPreviewProcessing.h/.cpp`/`Shaders/FrameDebuggerPreview.comp`).
  A brand-new `FrameDebuggerCommandBridge` plus eight `/frame_debugger/*` HTTP
  routes (`open`/`enable`/`capture`/`select_event`/`step_history`/
  `set_channel`/`set_levels`/`state`) make the entire feature drivable with no
  mouse/keyboard at all, and a main-viewport-pinning fix guarantees the window
  is visible to `GET /get_swapchain` whenever opened this way. Verified with a
  full clean build (both `GTE_ENABLE_EDITOR=ON` and `=OFF`), a full `ctest`
  regression pass, and - for the first time in this feature's history - a
  genuine, fully-automated, HTTP-driven, screenshot-verified end-to-end smoke
  test of the whole feature (open → enable/auto-capture → select event →
  explicit capture → step history → set channel → set levels, each step
  visually confirmed via `GET /get_swapchain`).
- **A follow-up bug-fix campaign, `frame-debugger-4`, fixed a confirmed bug
  where the Frame Debugger above never actually showed the atmosphere-
  scattering/aerial-perspective effect** (three phases -
  `task_manager/frame-debugger-4/PHASE0_MASTER_STRATEGY.md`) - the retained
  preview was always fed the pre-atmosphere-composite `"GameView"` texture,
  never the real, final `"GameViewComposited"` output the "Game" panel/
  `GET /get_game_view` already show, and the real
  `"AtmosphereAerialPerspectiveCompositePass"` render-graph pass that produces
  that composited image was entirely invisible in the event tree. Fixed by
  having `FrameDebuggerHistory` retain BOTH images per captured frame (the
  true pre-composite copy plus a new true post-composite copy), a new
  composite-aware picking rule that shows the final, fog-inclusive image by
  default (and the true pre-composite reconstruction only when the
  `"GameView"` leaf itself is explicitly selected), and a new, real, selectable
  `"AtmosphereAerialPerspectiveCompositePass"` tree leaf sibling to `"GameView"`
  with real read/write texture names and GPU timing. Verified with a full
  clean build (both `GTE_ENABLE_EDITOR` configs), a full `ctest` regression
  pass, and a live, HTTP-driven, screenshot-verified smoke test directly
  comparing the Frame Debugger's own preview against `GET /get_game_view`
  before and after selecting each leaf.
- **The Editor now has a new "Frame Debugger" window, GUI-only scaffolding
  for a future Unity-Frame-Debugger-style tool** (`frame-debugger-2`
  campaign, seven phases -
  `task_manager/frame-debugger-2/PHASE0_MASTER_STRATEGY.md`) - an on-demand
  floating window opened via a brand-new "Window" top-level menu
  (`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`, backed by the pure
  `src/Editor/FrameDebuggerData.h/.cpp` data model), showing a toolbar
  ("Enable" checkbox, cosmetic disabled "Editor" combo), a disabled frame
  stepper row, a draggable-splitter left-hand event tree, and a right-hand
  inspector pane (RenderTarget/Channels/Levels/preview chrome plus a
  Shader/Pass/Blend/Z-state/Stencil/Preview/ShaderProperties event-details
  section). Checking "Enable" auto-engages the existing Pause/Resume toolbar
  from the `frame-debugger-1` campaign. No real frame/draw-call capture
  logic was wired in anywhere - every value shown is a disabled control or a
  placeholder message ("No frame captured yet." / "No event selected."), by
  design, until a future campaign wires real data into the documented seams.
  Verified with a full clean build (both `GTE_ENABLE_EDITOR=ON` and `=OFF`),
  a full `ctest` regression pass, and a live runtime smoke test.
- **The Editor now has a genuine Unity-style Pause/Step control, backed by
  a brand-new, dedicated, explicit `Time` class** (`frame-debugger-1`
  campaign, five phases -
  `task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md`) - a small
  Pause/Resume + Step toolbar (`src/Editor/PlaybackControls.h/.cpp`) drives
  a new `gte::Time`/`gte::EngineContext` (`src/Core/Time.h/.cpp`,
  `src/Core/EngineContext.h`) that `Application` advances once per frame
  and `Game::Update()` reads to skip Animation/Physics/GPU-skinning work
  entirely on a frozen frame - rendering, the Editor UI, and the
  independently-orbitable Scene-view camera all keep working normally
  while paused, and "Step" advances by exactly one deterministic 1/60s
  tick. Resuming from an arbitrarily long pause is clamped to a single
  ordinary-sized simulation step rather than replaying the entire elapsed
  real-world gap. Verified with a full clean build (both
  `GTE_ENABLE_EDITOR=ON` and `=OFF`), a full `ctest` regression pass, and a
  live runtime smoke test.

- **The Aerial Perspective haze is now actually VISIBLE at this engine's real
  scene scale, its own tuning knobs are live Editor sliders instead of
  hardcoded shader constants, and its froxel volume has a genuinely useful
  live visual + numeric debugging path** (`atmosphere-scattering-2` campaign,
  six phases - `task_manager/atmosphere-scattering-2/PHASE0_MASTER_STRATEGY.md`,
  `AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT.md`). The original
  `atmosphere-scattering-1` pipeline was confirmed logically correct
  end-to-end, but its fixed 10km froxel far-plane and real-Earth-scale
  scattering coefficients made the effect ~2-3 orders of magnitude too faint
  to see at the few-meters-to-few-hundred-meters distances this engine's real
  content actually lives at - a genuine SCALE MISMATCH, not a bug (see
  `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`, kept as permanent historical
  record). `aerialPerspectiveMaxDistanceKm`/`aerialPerspectiveDepthExponent`/
  `aerialPerspectiveSamplesPerSlice`/`aerialPerspectiveScatteringExaggeration`
  are now real `AtmosphereSettings` fields with live sliders in the "Atmosphere"
  panel (Phase 1), the max distance default shrank from 10km to **0.5km** and a
  new scattering-exaggeration multiplier shipped at **30.0x** (Phase 3) -
  applied strictly LOCALLY inside the aerial volume's own shader, never
  touching the shared `AtmosphereMath.h`/`AtmosphereCommon.glsl` oracle the
  Sky-View/Transmittance/Multi-Scattering LUTs also rely on - plus two smaller,
  independently-confirmed composite-pass precision fixes (half-texel Z-bias,
  first-slice fade-in blend, Phase 2). The existing `GET /get_texture`
  volume-preview raymarch (`network-impl-6`) gained a second, atmosphere-aware
  interpretation mode auto-selected by volume name (Phase 4), turning what used
  to render as a flat, uninformative dark box into a legible spatial gradient,
  and a new numeric CPU-readback inspection tool
  (`src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp`, an "Inspect
  Aerial Perspective LUT" button in the "Atmosphere" panel, Phase 5) reports
  live min/max/mean transmittance/in-scattering plus a "likely visible"
  heuristic. Verified with a full clean build, a full `ctest` regression pass,
  and a live runtime smoke test confirming the blueish haze is now clearly,
  smoothly visible on far-distance test geometry relative to near geometry -
  see `AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT.md` for the full
  six-phase writeup and final verification snapshot.
- **The embedded HTTP server's `GET /get_texture`/`GET /list_textures` now
  understand live, GPU-resident 3D (volume) textures, not just 2D ones**
  (`network-impl-6` campaign,
  `task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md`) - the same class
  of capability Unity's Editor gives you when it draws a raymarched
  "smoke cloud"-style preview thumbnail for a `Texture3D` asset in the
  Inspector, except here the client is an LLM/AI agent talking to
  `GET /get_texture` over loopback HTTP, not a human looking at an Editor
  panel. A new, pure, Tier-1-tested data model,
  `gte::rg::RenderGraphDebugVolumeTextureRegistry`
  (`src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h/.cpp`),
  mirrors the existing 2D `RenderGraphDebugTextureRegistry`
  (`network-impl-4` campaign) and is auto-populated by
  `gte::rg::RenderGraph::ExecuteCompiledGraph()` every frame, with zero
  opt-in from whichever pass declared the volume texture (today: the
  Atmosphere feature's own two aerial-perspective froxel volumes - see the
  Atmosphere Scattering entry immediately above - this campaign's own first
  real, verified consumer, but the mechanism works generically for any future
  `VolumeTextureHandle`). A requested `texture_name` that resolves to a
  volume now renders a fresh, on-demand, single-fixed-camera, front-to-back
  alpha-composite raymarch (`gte::VolumeTexturePreviewRenderer`, driven by a
  new compute shader, `Shaders/VolumeTexturePreview.comp`, and its own pure
  CPU camera/ray-box math oracle, `VolumeTexturePreviewMath.h` - the same
  "CPU oracle is right by definition" discipline the Atmosphere Scattering
  campaign's own `AtmosphereMath.h` already established) into a persistent
  256x256 RGBA8 thumbnail, then rejoins the exact same PNG-encode/
  `?format=`/`Accept:` negotiation path every existing 2D capture already
  uses - no new endpoint, no new query parameter, no new cross-thread bridge
  type. `GET /list_textures` entries now also carry a
  `"kind":"texture2d"|"texture3d"` field plus a `"depth"` field (a volume's
  Z/texel-count extent), so an LLM/AI agent caller can discover which
  `texture_name`s are volumes worth requesting with no prior knowledge of
  the engine's internal naming convention. Verified end-to-end against a
  live running engine (`GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
  returning a real, visually plausible non-cubic-box PNG thumbnail;
  `channel=depth` against a volume
  name correctly returning `409`; every pre-existing 2D-texture capture
  request behaving byte-for-byte unchanged) and a full clean build plus full
  `ctest` regression pass. See `AGENTS.md`'s "Named Texture Capture"/
  "Atmosphere Scattering" sections for every load-bearing rule this feature
  depends on, and each phase's own `PHASEn_COMPLETION_REPORT.md` for the
  full six-phase campaign writeup.
- **The Aerial Perspective volume's own HTTP preview thumbnail (above) had its
  camera framing corrected in a follow-up campaign, `atmosphere-scattering-3`**
  (`task_manager/atmosphere-scattering-3/`) - the generic volume-preview
  camera/proxy-box math is correct for an ordinary spatial volume, but it
  flattened this particular LUT's one meaningful (near/far) axis into an
  unreadably thin sliver, since its three axes aren't comparable units; a
  dedicated camera, and then a literal tapering frustum-shaped raymarch proxy
  (auto-selected purely by texture name, same convention as the
  color-interpretation fix above, with zero new HTTP parameter), now make the
  preview actually widen away from the camera with a legible near/far haze
  gradient - verified with a full clean build and full `ctest` regression
  pass. See `task_manager/atmosphere-scattering-3/CAMPAIGN_COMPLETION_REPORT.md`
  for the full five-phase writeup.
- **A follow-up campaign, `atmosphere-scattering-4`, fixed a confirmed bug
  where Aerial Perspective was double-applied to empty sky pixels** (no
  opaque geometry drawn into them that frame) — the Aerial Perspective
  Composite pass used to run its full haze blend unconditionally, re-fogging
  a sky pixel the Sky Background pass had already finished, correctly,
  earlier in the same frame (confirmed by toggling `aerialPerspectiveStrength`
  visibly changing the sky itself, which should never happen). The fix is a
  small early pass-through branch in
  `Shaders/AtmosphereAerialPerspectiveComposite.comp`, mirroring a new,
  dedicated, Tier-1-tested CPU oracle
  (`src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h/.cpp`)
  built and tested BEFORE the shader was touched — real opaque geometry
  (a mesh, the reference grid) still fogs progressively with distance exactly
  as before, completely unaffected. A new permanent Editor diagnostic, the
  "Validate Aerial Perspective Sky Purity" button in the "Atmosphere" panel
  (`src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp`),
  numerically re-confirms every sky pixel's post-composite color exactly
  matches its pre-composite color, on demand — catching a future regression
  of this exact bug class without relying on a human eyeballing a screenshot.
  Verified with a full clean build, a full `ctest` regression pass, and a
  live runtime smoke test. See
  `task_manager/atmosphere-scattering-4/CAMPAIGN_COMPLETION_REPORT.md` for
  the full four-phase writeup.

- **The embedded HTTP server can now bring a specific Editor panel/tab to
  the front on command, and list every known panel name** (`network-impl-7`
  campaign, `task_manager/network-impl-7/PHASE0_MASTER_STRATEGY.md`) -
  `GET /activate_tab?name=<PanelName>` makes that named tab
  (`"Hierarchy"`/`"Inspector"`/`"Scene"`/`"Game"`/`"Memory"`/`"Profiler"`/
  `"Render Graph"`/`"Jobs"`/`"Atmosphere"`/`"Project"`) the active/focused tab
  this same frame, exactly as if a human had clicked it, and
  `GET /list_tabs` reports every currently-known panel name so a caller
  never has to guess or hardcode the engine's internal naming convention.
  Built on a brand-new, dedicated cross-thread bridge,
  `EditorUiCommandBridge` (`src/Application/EditorUiCommandBridge.h/.cpp`),
  mirroring `FrameCaptureBridge`/`EngineCommandBridge`'s own narrow,
  single-purpose bridge precedent, and a shared
  `src/Editor/EditorPanelCatalog.h` panel-name catalog that `DockLayout.cpp`'s
  own default layout now reads from too, so the set of valid tab names can
  never drift out of sync between the two. Verified end-to-end with a real
  running engine (`GET /activate_tab?name=Profiler` followed by
  `GET /get_swapchain` visually confirming the "Profiler" tab genuinely came
  to the front) and a full `ctest` regression pass. See
  `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md` for the full
  five-phase campaign writeup.

See **[docs/CHANGELOG.md](docs/CHANGELOG.md)** for the full project history.

## Roadmap

See **[TODO.md](TODO.md)** for known limitations, deliberately deferred
follow-ups (Editor and Memory Profiler), and longer-term engine roadmap
ideas.
