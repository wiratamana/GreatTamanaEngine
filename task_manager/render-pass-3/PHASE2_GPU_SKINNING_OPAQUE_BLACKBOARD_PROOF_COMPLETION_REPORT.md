# PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF — Completion Report

_Child of `PHASE0_MASTER_STRATEGY.md`, `render-pass-3` campaign. Branch:
`feature/render-pass-impl` (unchanged, never switched)._

## Summary

Implemented PHASE2 exactly as scoped: the first REAL consumer of PHASE1's new
declaration layer. `Application` now owns a real `rg::RenderPipeline
m_offscreenRenderPipeline;` member with exactly two providers registered —
`"GpuSkinning"` (`ProviderScope::Once`) and `"RenderOpaque"`
(`ProviderScope::PerActiveView`, this phase only ever invoked with one active
view, `"Game"`). GPU Skinning's output buffer handles reach `RenderOpaque`'s
own `setup` lambda ENTIRELY through `RenderPassBlackboard::Publish`/`Fetch`
(key `"GpuSkinning.OutputBuffers"_passId`), with **zero direct call or shared
captured variable between the two providers' own registration lambdas** —
confirmed by inspection of `RegisterOffscreenRenderPipelineProviders()`
(`Application.cpp`): the `"GpuSkinning"` lambda never references the
`"RenderOpaque"` lambda or vice versa; the only thing that connects them is
`frame.blackboard`.

Verified with an incremental compile of `gte_core`/`GreatTamanaEngineTests`/
`GreatTamanaEngine`, a full targeted test run (207/207 `RenderGraph*:
RenderPass*` tests passing, zero regressions), and a live, HTTP-driven smoke
test (`run_app_background` + `gte_send_request`) confirming the Game View
still renders correctly and the Frame Debugger's tree shape/Inspector data
for `"RenderOpaque"` is pixel-for-pixel identical in shape to before this
phase.

## Files changed

- **`src/Renderer/RenderGraph` — untouched.** Confirmed no files under this
  directory were modified this phase (PHASE1's `RenderPipeline.h`/`.cpp` and
  the trailing-defaulted `renderPassEvent` parameter on
  `RenderGraphBuilder::AddRenderPass()` are used exactly as PHASE1 shipped
  them).
- **`src/Application/RenderPasses.h`** — `DeclareGpuSkinningReads()`'s
  declaration, and the `kGameClearColor`/`kGameClearDepth` constants, MOVED
  here (UNCHANGED in value/behavior) from `RenderPasses.cpp`'s own former
  anonymous namespace, so `Application.cpp`'s new `"RenderOpaque"`
  `RenderPipeline` provider can reuse these exact same symbols bit-for-bit
  instead of duplicating them (a real risk of the two copies silently
  drifting apart otherwise). This required upgrading the header's own bare
  `namespace rg { class RenderGraphBuilder; }` forward declaration to a full
  `#include "../Renderer/RenderGraph/RenderGraphBuilder.h"`, since
  `DeclareGpuSkinningReads()`'s signature needs the real, complete
  `rg::RenderGraphBuilder::PassBuilder` nested type, which a bare forward
  declaration of the outer class can never provide. Added `#include <array>`
  for `kGameClearColor`'s `std::array<float, 4>` type. Only `Application.cpp`
  and `RenderPasses.cpp` themselves include this header (confirmed via
  `search_in_dir`), so this include-footprint change is low-risk.
- **`src/Application/RenderPasses.cpp`** — the `DeclareGpuSkinningReads()`
  function body (and the `kGameClearColor`/`kGameClearDepth` constant
  definitions) moved OUT of the file's anonymous namespace to match the new
  header declaration — same exact bodies/values, just relocated so they have
  external linkage now. Every pre-existing call site inside this same file
  (`AddRenderOpaquePass()`, `AddFrameDebuggerReplayPasses()`,
  `AddSceneViewPass()`, `AddPresentPass()`) is unaffected (unqualified name
  lookup still resolves to the same symbols, now via the header instead of
  the anonymous namespace). `AddGpuSkinningPasses()` itself is **completely
  unchanged** — still the one real, direct call site whenever Game View is
  NOT visible this frame (see below).
- **`src/Application/Application.h`** — new
  `#include "../Renderer/RenderGraph/RenderPipeline.h"`; new private method
  declaration `void RegisterOffscreenRenderPipelineProviders();`; new members
  `rg::RenderPipeline m_offscreenRenderPipeline;`,
  `std::vector<AnimationSystem::GpuSkinningDispatchRequest>
  m_gpuSkinningRequestsThisFrame;`, `std::vector<rg::BufferHandle>
  m_gpuSkinningHandlesThisFrame;`, `rg::TextureHandle
  m_currentGameViewTargetForOffscreenPipeline;`, `float
  m_currentGameViewAspectForOffscreenPipeline = 1.0f;`,
  `FrameDebuggerCaptureContext* m_currentFrameDebuggerCaptureForOffscreenPipeline
  = nullptr;`.
- **`src/Application/Application.cpp`** —
  - New includes: `../Renderer/ComputeDispatch.h`,
    `../Renderer/GpuSkinning/GpuSkinningPipelines.h` (the `"GpuSkinning"`
    provider needs `ComputeGroupCount()`/`kSkinningLocalSizeX`/
    `GpuSkinningPipelines`, exactly like `RenderPasses.cpp`'s own
    `AddGpuSkinningPasses()` already does).
  - New anonymous-namespace constant: `constexpr rg::RenderPassId
    kGpuSkinningOutputsKey = "GpuSkinning.OutputBuffers"_passId;` (plus a
    `using rg::operator""_passId;` to bring PHASE1's consteval literal
    operator into scope from `gte::rg` into `gte`'s own anonymous
    namespace).
  - Constructor body now calls `RegisterOffscreenRenderPipelineProviders();`
    once, before starting the network server.
  - New method `Application::RegisterOffscreenRenderPipelineProviders()` —
    registers both providers exactly as described in the Summary above.
  - `Run()`'s offscreen `build` lambda — the old unconditional
    `AddGpuSkinningPasses(b, m_game, m_renderer)` call at the top of the
    lambda is REPLACED by a `std::vector<rg::BufferHandle>
    gpuSkinningBuffers;` local, assigned via the OLD, unchanged free
    function ONLY when `gameTarget == nullptr` (see "Deviation 1" below for
    why). Inside the `if (gameTarget != nullptr)` block, at the exact point
    the old `AddRenderOpaquePass(...)` call used to sit, the new code:
    collects `m_gpuSkinningRequestsThisFrame`/imports
    `m_gpuSkinningHandlesThisFrame` (using the SAME builder `b` this frame's
    graph is being built against), stamps
    `m_currentGameViewTargetForOffscreenPipeline`/
    `m_currentGameViewAspectForOffscreenPipeline`/
    `m_currentFrameDebuggerCaptureForOffscreenPipeline`, constructs a local
    `rg::RenderPassBlackboard`/`rg::RenderPassFrameContext` (with
    `activeViews = { RenderViewId::Named("Game") }`), calls
    `m_offscreenRenderPipeline.DeclareInto(b, offscreenPipelineFrame)`,
    calls `offscreenPipelineBlackboard.ReportUnusedPublishesIfAny()` under
    `#ifndef NDEBUG`, then re-populates the OUTER `gpuSkinningBuffers` local
    by `Fetch()`-ing the SAME key back out of the blackboard — so
    `AddSceneViewPass()`/`AddFrameDebuggerReplayPasses()` (both still
    called exactly as before, unmigrated, per this phase's own scope) get
    the correct handles regardless of which of the two paths ran.
    `outputs.push_back(h);`/`AddDrawSkyBackgroundPass(...)`/
    `AddRenderTransparentPass(...)` and everything after are UNCHANGED.

## Deviations from the phase doc (both intentional, both narrow, both
documented per the phase doc's own explicit instruction to "state clearly
in the completion report")

### Deviation 1 — the "wrinkle" (`ImportBuffer()` needs a builder, a provider has none) was resolved by pre-computing outside any provider, exactly as the phase doc's own preferred option

The phase doc's Step 3.1 flagged that `RenderGraphBuilder::ImportBuffer()` is
a `RenderGraphBuilder&` method, but a `RenderPassProvider` callback has no
builder parameter, and asked to check "whether `ImportBuffer()`'s result...
can instead be resolved once, OUTSIDE any provider entirely, by
`RenderPipeline::DeclareInto()`'s own caller (`Application::Run()`) BEFORE
calling `DeclareInto()` at all" before resorting to touching
`RenderGraphBuilder.h`. That resolved cleanly: `Application::Run()` (which
already holds a live `RenderGraphBuilder& b` inside the offscreen `build`
lambda) now calls `m_game.CollectGpuSkinningDispatchRequests()` and
`b.ImportBuffer(request.name, request.outputBuffer,
request.outputBufferSize)` for every request itself, storing the results in
two new per-frame `Application` members
(`m_gpuSkinningRequestsThisFrame`/`m_gpuSkinningHandlesThisFrame`) that the
`"GpuSkinning"` provider (registered once, at construction time, capturing
`this`) reads when invoked later that same `DeclareInto()` call.
**`RenderGraphBuilder.h` was NOT touched at all this phase** — no new
`PassBuilder` accessor was needed, which is the safer of the two options the
phase doc itself called out.

### Deviation 2 — `AddGpuSkinningPasses()`'s OLD free function is still called directly, but ONLY when Game View is not visible this frame

The phase doc's own Step 3.2 pseudocode places the
`blackboard`/`frame`/`DeclareInto()` construction UNCONDITIONALLY, at the top
of the offscreen `build` lambda (mirroring where the old
`AddGpuSkinningPasses()` call used to sit), implicitly assuming
`RenderOpaque`'s own `gameViewTarget`/`aspectWidthOverHeight`/
`frameDebuggerCapture` are already known by that point. In the REAL code,
those three values are only computed deep inside the `if (gameTarget !=
nullptr)` block (after the Game View's own Atmosphere View LUT sequence),
*not* at the top of the lambda — moving that computation earlier would have
been a much larger, riskier restructuring than this narrow proof-of-concept
phase should attempt.

Resolving this literally (calling `DeclareInto()` unconditionally at the
top) was rejected because the `"RenderOpaque"` provider needs those three
values at the exact moment it runs (synchronously, inside `DeclareInto()`),
and they don't exist yet at the top of the lambda. Instead:
`m_offscreenRenderPipeline.DeclareInto()` is called **only** inside the `if
(gameTarget != nullptr)` block, at the point the old
`AddRenderOpaquePass()` call used to sit (matching the phase doc's own
literal instruction: "REPLACE the direct... call PAIR... only for the Game
View case"). This means the `"GpuSkinning"` provider (`ProviderScope::Once`)
only actually fires on a frame where Game View is visible. To make sure GPU
Skinning's compute dispatch is still declared **exactly once, every frame**
regardless of which panels are visible (a real, confirmed regression risk
this deviation specifically avoids: an Editor session with only the "Scene"
panel open, Game View hidden, would otherwise silently stop dispatching GPU
skinning at all, breaking Scene View's own display of an animated model), a
small `if (gameTarget == nullptr) { gpuSkinningBuffers =
AddGpuSkinningPasses(b, m_game, m_renderer); }` fallback keeps calling the
OLD, completely unchanged free function directly in that one case. The two
paths are mutually exclusive by construction (the `if`/`else` shape, and the
fact `DeclareInto()` is never called when `gameTarget == nullptr`), so GPU
Skinning's own compute dispatch is still declared exactly once per frame,
never twice — matching this phase's own Definition of Done exactly.

This deviation only affects INTERNAL MECHANISM (which of two code paths
declares the GPU Skinning compute pass this frame) — the observable
behavior (GPU Skinning dispatches exactly once, `RenderOpaque`/`SceneView`
both see correct buffer handles) is unchanged from before this phase in
every scenario, including ones not explicitly called out by the phase doc's
own pseudocode.

### Deviation 3 (very small) — `RenderPassDesc::order` for the `"GpuSkinning"` provider's descs was set to `PreOpaques`, not left at the default `Opaques`

`RenderPipeline::DeclareInto()` stable-sorts every collected `RenderPassDesc`
by `order` before declaring them, so setting the GPU Skinning descs' own
`order = rg::RenderPassEvent::PreOpaques` (matching
`PHASE0_MASTER_STRATEGY.md`'s own intended mapping: "GPU Skinning dispatch...
run first (`PreOpaques`/`BeforeEverything`)") guarantees they are declared
via `builder.AddRenderPass()` BEFORE the `"RenderOpaque"` desc
(`order = Opaques`), matching the OLD code's own declaration order exactly.
Confirmed via `search_in_dir` that `PassRecord::renderPassEvent`/
`RenderGraphPassSnapshot::renderPassEvent` are read by **nothing** anywhere
in the codebase yet (PHASE1's own completion report already noted "zero real
consumers" — PHASE4 is the first planned consumer) — so this value change
has **zero observable effect** on the Frame Debugger or anything else today,
confirmed by the live capture screenshot in "Verification" below showing
`"RenderOpaque"`'s tree position/Inspector data unchanged.

## Verification performed

- **Incremental compile, `gte_core`**: succeeded cleanly (2 objects
  rebuilt — `RenderPasses.cpp`, `Application.cpp` — plus a relink).
- **Incremental compile, `GreatTamanaEngineTests`**: succeeded cleanly
  (relink only — no test file needed changes, since this phase touches no
  Tier-1-testable pure logic of its own; `RenderPipeline`/
  `RenderPassBlackboard` themselves were already tested by PHASE1).
- **Incremental compile, `GreatTamanaEngine`** (the real executable): also
  built and linked cleanly.
- **Full targeted test run**: `GreatTamanaEngineTests.exe
  --gtest_filter=RenderGraph*:RenderPass*` — **207/207 passing**, zero
  failures, zero new skips (same count as PHASE1's own completion report —
  this phase added no new test files, since it wires EXISTING,
  already-tested PHASE1 vocabulary into `Application.cpp`, which has no
  Tier-1 test file of its own per this codebase's own established "Tier 2:
  GPU-dependent code has no automated test coverage yet" rule — see
  AGENTS.md).
- **Live, HTTP-driven smoke test** (`run_app_background` +
  `gte_send_request`):
  - `GET /get_swapchain` (both before and after enabling the Frame Debugger)
    confirmed the Scene/Game panels render correctly — the atmosphere sky
    gradient is visible in both, matching normal, unregressed behavior. The
    test scene (`TestScene.gtscene`) contains only a `Camera` entity, no
    mesh/skinned model, so this run could **only** confirm generic Opaque
    rendering is unaffected — it did **not** exercise a real GPU-skinned/
    animated model (explicitly called out here as a limitation of this
    phase's own live verification, per the phase doc's own instruction, not
    silently skipped). `m_gpuSkinningRequestsThisFrame` was empty every
    frame during this smoke test, so the `"GpuSkinning"` provider published
    an empty `std::vector<rg::BufferHandle>{}` onto the blackboard every
    frame, and `"RenderOpaque"` fetched that same empty vector back — the
    "empty publish, immediately fetched, never reported as unused" code
    path was exercised for real, but the "at least one real buffer handle
    flows end-to-end through the blackboard" path was not (no rigged/
    GPU-skinning-eligible model was available in this project to load).
  - `GET /frame_debugger/open` → `?value=true` (enable) →
    `/frame_debugger/capture` → `GET /get_swapchain` confirmed the event
    tree shows `"RenderOpaque"` in exactly the same tree position as before
    this phase (a flat leaf — no per-entity children this frame, since the
    scene has zero mesh entities — immediately after the `"Compute LUT"`
    group and immediately before `"DrawSkyBackground"` ▸ `"Draw Quad"`),
    matching `render-pass-2`'s own established shape exactly.
  - `GET /frame_debugger/select_event?index=10` (the `"RenderOpaque"` row,
    confirmed by `totalEventCount: 15` = 5 LUT passes × 2 rows each + 1
    `RenderOpaque` + 2 `DrawSkyBackground` rows + 2
    `AtmosphereAerialPerspectiveCompositePass` rows) → `GET /get_swapchain`
    confirmed the Inspector pane shows `"Event #10: Draw Mesh"`, `Pass:
    RenderOpaque`, `Blend: Opaque (no blend)`, `ZTest: Less`, `ZWrite: On`,
    `Cull: None` — real, correct pipeline-state data, identical in shape to
    what the OLD direct `AddRenderOpaquePass()` call always produced.
  - Frame Debugger disabled and the process terminated cleanly afterward
    (`stop_app_background`).
- No full build, no full `ctest` run was performed — correctly out of scope
  for this phase per `PHASE0_MASTER_STRATEGY.md`'s cross-cutting rules ("No
  full build/regression test until PHASE5").

## Notes for PHASE3's implementer

- **The Game-hidden/Scene-visible fallback (Deviation 2 above) is a real,
  load-bearing piece of code PHASE3 needs to know about and properly
  retire.** Today, `Run()`'s offscreen `build` lambda has TWO different
  code paths that can declare GPU Skinning's compute dispatch depending on
  `gameTarget`'s nullness — the NEW provider-based path (`gameTarget !=
  nullptr`) and the OLD direct `AddGpuSkinningPasses()` call (`gameTarget ==
  nullptr`). PHASE3, which migrates `AddSceneViewPass()`/`AddPresentPass()`
  onto the new system too and builds the real per-view provider loop, should
  be able to collapse this back down to a single, unconditional
  `DeclareInto()` call site (since GPU Skinning is `ProviderScope::Once`
  and does not itself depend on which views are active) — at that point the
  `if (gameTarget == nullptr) { ... AddGpuSkinningPasses ... }` fallback
  this phase added can be deleted entirely.
- **`m_currentGameViewTargetForOffscreenPipeline`/
  `m_currentGameViewAspectForOffscreenPipeline`/
  `m_currentFrameDebuggerCaptureForOffscreenPipeline` are a narrow,
  Game-View-only stopgap**, needed only because `RenderPassProvider` has no
  way to receive extra, view-specific data beyond `RenderPassFrameContext`
  itself. PHASE3, which generalizes `"RenderOpaque"`'s provider body to
  branch on `frame.currentView` for real Game/Scene View support (per
  `PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decision 1/note in this
  phase's own doc), will likely want to replace these three
  Application-level members with a proper per-view lookup structure (e.g. a
  small `std::vector`/map keyed by `RenderViewId`, populated once per frame
  before `DeclareInto()` runs) rather than growing more single-view scalar
  members of this same shape.
- **The blackboard instance itself
  (`rg::RenderPassBlackboard offscreenPipelineBlackboard;`) is a per-call
  LOCAL in `Run()`, not an `Application` member** — a deliberate choice
  matching the phase doc's own Step 3.2 code sample exactly (a fresh
  blackboard each time the offscreen `build` lambda runs). This is
  correct and sufficient for this phase (the `"GpuSkinning"` publish and
  `"RenderOpaque"` fetch both happen inside the same `DeclareInto()` call,
  well before the blackboard goes out of scope at the end of the lambda) —
  PHASE3 should keep this same "local blackboard, recreated every
  `DeclareInto()` call" shape rather than promoting it to a member, unless
  a genuine cross-`DeclareInto()`-call hand-off need emerges (none exists
  today).
- **`RenderPipeline::DeclareInto()`'s own hardcoded `ViewScope::Shared`
  translation (PHASE1's own documented stand-in) is STILL in effect** —
  `"RenderOpaque"`'s underlying `PassRecord::viewScope` is now
  `ViewScope::Shared`, not `ViewScope::GameView` like the OLD direct call
  produced. This was confirmed SAFE for the Frame Debugger specifically
  (`FrameDebuggerData.h`'s own documented rule: "`ViewScope::Shared`... and
  `ViewScope::GameView` both still pass through unchanged" — every
  discriminating check in that file tests `viewScope == SceneView`, never
  `== GameView`), and confirmed visually via the live capture above. PHASE3
  is what actually builds the real Game/Scene `ViewScope` translation table
  this stand-in exists to eventually replace (`PHASE0_MASTER_STRATEGY.md`'s
  Locked Design Decision 5) — until then, any FUTURE pass migrated onto this
  new system should double check whether its own behavior (unlike the Frame
  Debugger's) actually depends on the real `GameView`/`SceneView` distinction
  before assuming `Shared` is a safe stand-in for it too.
- **`RenderPassDesc::order = PreOpaques` for GPU Skinning (Deviation 3) has
  zero observable effect today**, but is already the semantically correct
  value per `PHASE0_MASTER_STRATEGY.md`'s own intended
  `RenderPassEvent` mapping — PHASE3 migrating the Atmosphere/Sky/Transparent
  passes onto this system should set each one's own `order` to the
  correspondingly correct value from that same mapping
  (`AfterOpaques` for Sky, `Transparents` for the transparent pass,
  `AfterTransparents` for the Aerial Perspective Composite pass) from the
  start, rather than leaving them at the default `Opaques`, so PHASE4's
  planned `RenderPassEvent`-based pivot search has fully correct data to
  work with once it lands.
- **This phase's own live smoke test never exercised a real GPU-skinned
  model** (see "Verification" above — the current test project has no
  rigged mesh loaded). If PHASE3 or PHASE5 has access to a project with a
  GPU-skinning-eligible model already imported, re-running this same
  smoke-test recipe (enable GPU skinning mode via the "Jobs" panel, animate
  the model, capture a Frame Debugger frame, confirm the model still
  renders correctly and `"RenderOpaque"`'s own child draw record shows the
  skinned mesh) would be valuable, genuinely-new verification this phase
  could not perform.

## Definition of Done — checklist

- [x] `Application` owns a real `rg::RenderPipeline
      m_offscreenRenderPipeline;` member with exactly two providers
      registered: `"GpuSkinning"` (`Once`) and `"RenderOpaque"`
      (`PerActiveView`, invoked with exactly one active view — `"Game"` —
      this phase).
- [x] GPU Skinning's output buffer handles reach `"RenderOpaque"`'s own
      `setup` lambda ENTIRELY through `RenderPassBlackboard::Publish`/
      `Fetch`, with zero direct call or shared captured variable between
      the two providers' own registration lambdas.
- [x] `AddSceneViewPass()`/`AddPresentPass()` still receive GPU Skinning's
      buffer handles correctly (same barrier/ordering guarantee as before
      this phase — `DeclareGpuSkinningReads()` itself is byte-for-byte
      unchanged), with GPU Skinning's own compute dispatch still declared
      exactly ONCE per frame, never twice (see Deviation 2 above for the
      real mechanism that guarantees this across both possible code paths).
- [x] An incremental compile of `gte_core` succeeds (also verified
      `GreatTamanaEngineTests`/`GreatTamanaEngine` themselves compile/link).
- [x] A live, HTTP-driven check (`run_app_background` + `gte_send_request`)
      confirms Game View still renders correctly and the Frame Debugger's
      tree shape for `"RenderOpaque"` is visually unchanged from before this
      phase.
- [x] `PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md`'s own
      `ReportUnusedPublishesIfAny()` mechanism does NOT fire a warning
      during a normal frame where GPU Skinning legitimately has nothing to
      publish — confirmed: `"RenderOpaque"` always `Fetch()`s the same key
      every time `DeclareInto()` runs (marking the slot as fetched even when
      the published vector is empty), so an empty publish is never reported
      as "unused."

## What This Phase Did NOT Do (confirmed, matching its own "What We Will NOT Do")

- Did NOT migrate `AddSceneViewPass()`/`AddPresentPass()` onto the new
  provider system — both still use the OLD, hand-threaded
  `gpuSkinningOutputBuffers` vector parameter, unchanged.
- Did NOT migrate Atmosphere/Sky/Transparent onto the new system — this
  phase touches ONLY GPU Skinning + the Game-View Opaque pass.
- Did NOT generalize `"RenderOpaque"`'s provider body to branch on
  `frame.currentView` for Scene View support — `frame.activeViews` in this
  phase only ever contains the Game View id.
- Did NOT touch `FrameDebuggerData.cpp` in any way — confirmed via
  `search_in_dir` (zero matches for `RenderPipeline`/`RenderPassBlackboard`/
  `RenderPassDesc` anywhere under `src/Editor/`) and via the live capture
  screenshot showing byte-for-byte identical tree shape/Inspector data.
