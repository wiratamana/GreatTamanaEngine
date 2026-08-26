# RENDERGRAPH_PHASE7_COMPLETION_REPORT.md

Session report for **Phase 7 — Move the Real Engine to the Render Graph**,
the seventh implementation chunk of the Render Graph campaign described in
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`. Scope was taken from
`RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md`'s own "Step 3: The
Plan", with a handful of deliberate, documented deviations described below
where the v2 document's own code sketch didn't quite match what the real
engine needed (the same kind of "the doc sketch turned out to need a real
addition once actual code was written" finding every earlier phase's own
completion report already logged at least once).

## What shipped

### 1. `src/Application/RenderPasses.h`/`.cpp` (new)

The three thin, Application-layer pass-wrapper functions the strategy
document calls for — `AddGameViewPass()`, `AddSceneViewPass()`,
`AddPresentPass()` — plus one new helper,
`FinalizeRenderTextureForExternalSampling()`, that Step 3.7's own "V2
Revision Note 4" gap analysis made unavoidable (see "Deviation 1" below).
Each pass's `execute` callback is a direct, literal translation of what
`Application::Run()` used to do by hand: it brackets `Game::Render()` with
`Renderer::BeginGraphPassRecording()`/`EndGraphPassRecording()` (see below)
so every `Renderer::Submit()` call `RenderSystem::Draw()` already makes
internally keeps working completely unmodified — **`Game::Render()`,
`RenderSystem::Draw()`, and `Renderer::Submit()` all kept their exact
pre-Phase-7 signatures**, satisfying the strategy document's own hardest
constraint ("Step 4: What We Will NOT Do").

`AddPresentPass()` deliberately deviates from the strategy document's own
code sketch by taking `Game&`/`Renderer&` and an
`std::optional<float> directGameRenderAspect` parameter (see "Deviation 2"
below) — when set, it renders Game directly into the swapchain, in the
SAME pass as the ImGui chrome, rather than as a separate pass.

### 2. Clear-color support added to the render graph (Phase 2/6 growth)

`RenderGraphTypes.h`'s `PassRecord` gained
`colorClearValue`/`depthClearValue` (`std::optional<std::array<float,4>>`/
`std::optional<float>`); `RenderGraphBuilder::PassBuilder::WriteColorAttachment()`/
`WriteDepthStencilAttachment()` gained matching optional parameters
(`std::nullopt` default — preserves Phase 6's original `VK_ATTACHMENT_LOAD_OP_LOAD`-
only behavior exactly); `RenderGraph::ExecuteCompiledGraph()` now reads
these to pick `CLEAR` vs. `LOAD` per attachment. This closes the exact gap
`RENDERGRAPH_PHASE6_COMPLETION_REPORT.md`'s own "Handoff notes" flagged:
"A per-pass clear-color mechanism is needed before Phase 7's real Game/
Scene/Present passes will look correct." Four new Tier-1 tests were added
to `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` covering both
the default-empty and value-supplied cases for color and depth.

### 3. `FrameRecorder::IssueDrawCommand()` (new, static, public)

The exact per-`DrawItem` Vulkan-issuing body `RecordFrame()`'s own draw
loop used to inline (bind pipeline, push constants, optional material
descriptor set, bind vertex/index buffers, `vkCmdDraw`/`vkCmdDrawIndexed`)
was extracted into a public static method, so it can be called from a
SECOND place — `Renderer::Submit()` — without duplicating the logic.
`RecordFrame()`'s own loop now calls this same function; behavior is
byte-for-byte identical to before this refactor (verified by the full test
suite passing unchanged).

### 4. `Renderer::Submit()` — the one surgical redirect

Exactly as `RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md`'s own
Step 3.1 specifies: `Renderer` gained a `m_currentGraphPassCmd`/
`m_currentGraphPassRecordDrawStats` pair, set/cleared by two new public
methods, `BeginGraphPassRecording(cmd, recordDrawStats)`/
`EndGraphPassRecording()`. `Renderer::Submit()` now checks
`m_currentGraphPassCmd` first: if set, it calls
`FrameRecorder::IssueDrawCommand()` DIRECTLY against that command buffer
and calls `recordDrawStats(...)` (fusing draw-stats accumulation to the
same call site that issues the real draw, per this engine's own
`AccumulateDrawStats()` correctness rule — see `AGENTS.md`, "Profiling");
otherwise it falls back to the legacy `m_frameRecorder.Submit()` queue
exactly as before. `Game`/`RenderSystem`/`RenderSystem::Draw()` needed
**zero changes** to keep working through this redirect.

### 5. `Renderer`/`FramePresenter` — the offscreen and present render-graph entry points

- `Renderer::BeginOffscreenRenderGraphRecording()`/
  `EndOffscreenRenderGraphRecording()` (forwarding to new
  `FramePresenter::BeginOffscreenRecording()`/`EndOffscreenRecording()`) —
  wait on/reset the SAME offscreen fence/command buffer
  `RenderOffscreen()` already uses, hand the caller a raw
  `VkCommandBuffer` to record one or more render-graph passes into, then
  end/submit/block synchronously — the multi-pass generalization of
  `RenderOffscreen()`'s own single-`RenderTexture` shape.
- `Renderer::PresentViaRenderGraph()`/`FramePresenter::PresentViaRenderGraph()` —
  the acquire/resize/skip logic is copied verbatim from the old `Present()`
  (same early-return semantics for a minimized window/pending resize/
  just-recreated swapchain), but instead of calling
  `frameRecorder.RecordFrame()` it imports the acquired swapchain image as
  a graph texture (`RenderGraphBuilder::ImportTexture("Swapchain", target,
  VK_IMAGE_LAYOUT_UNDEFINED)`) and calls
  `graph.Execute(cmd, ExecuteTimingMode::PipelinedDeferredReadback, build)`.
  `needsSwapchainDepth` (supplied by the caller) replaces the old
  `FrameRecorder::HasQueuedDraws()`-driven lazy depth-buffer decision,
  since nothing queues into `m_drawQueue` for this path anymore.

### 6. `Application::Run()` — the real two-`Execute()`-calls-per-frame cutover

Replaced the old three `RenderOffscreen()`/`Present()` blocks with exactly
the shape `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision
Note 2 and `RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md`'s
Step 3.2 specify:

- **Call 1 of 2** (`ExecuteTimingMode::SynchronousImmediateReadback`) —
  Game view + Scene view together, via `AddGameViewPass()`/
  `AddSceneViewPass()`, run unconditionally whenever either
  `IEditorLayer::GameViewTarget()`/`SceneViewTarget()` is non-null.
- **Call 2 of 2** (`ExecuteTimingMode::PipelinedDeferredReadback`) —
  Present alone, via `AddPresentPass()`, always runs.

A dependency-cycle `std::runtime_error` from `RenderGraphCompiler::Compile()`
(structurally unreachable through any graph this engine declares — see
`RENDERGRAPH_PHASE3_COMPLETION_REPORT.md` — but genuine defensive code) is
caught around BOTH calls, logged loudly to `stderr`, and additionally
asserted in debug builds — per
`RENDERGRAPH_PHASE6_COMPLETION_REPORT.md`'s own Step 3.5 guidance — rather
than silently swallowed.

`m_renderGraph` (`gte::rg::RenderGraph`) is a new `Application`-owned
member, constructed right after `m_renderer` (needs a live `Renderer&`).

Draw-call/triangle counts for all three named passes are now sourced from
`m_renderGraph.LastKnownStatsFor("GameView"/"SceneView")` and
`Renderer::PresentViaRenderGraph()`'s own return value (which internally
reads `graph.LastKnownStatsFor("Present")`) — these are REAL, non-
regressed numbers (see "Known limitations" below for GPU timing, which is
NOT real yet).

## Deviations from the v2 strategy document's own code sketch

**Deviation 1 — a manual "finalize for external sampling" step was
required, and is new.** Neither `AddGameViewPass()`/`AddSceneViewPass()`
nor `RenderGraph::Execute()` itself ever declares a `ReadTexture()` for the
Game/Scene `RenderTexture` — exactly as
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision Note 4
predicted ("Dear ImGui samples the Game/Scene RenderTexture's entirely on
its own, outside the graph's resource model"). This meant the render
graph itself had NO mechanism to leave a `finalOutputs` texture in
`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` for ImGui to sample afterward —
a pass's own `ColorAttachmentWrite` write leaves it in
`COLOR_ATTACHMENT_OPTIMAL`, and nothing inside the SAME `Execute()` call
ever reads it back out. The fix: a new free function,
`RenderPasses.h`'s `FinalizeRenderTextureForExternalSampling(cmd,
texture)`, called by `Application::Run()` directly against the offscreen
command buffer, AFTER `m_renderGraph.Execute(...)` returns (so the pass's
own `vkCmdBeginRendering`/`vkCmdEndRendering` bracket has already closed)
and BEFORE the command buffer is ended/submitted. It reuses Phase 5's own
pure `RequiredStateFor()`/`EmitImageBarrier()` helpers directly — no new
barrier-decision logic was invented, just a new CALL SITE for the existing
one. The exact same problem exists for the swapchain image's own
`PRESENT_SRC_KHR` transition (also called out by
`RENDERGRAPH_PHASE5_COMPLETION_REPORT.md`'s own "design decision worth
flagging" note) — handled the same way, inside
`FramePresenter::PresentViaRenderGraph()` itself, using the exact fixed
`{PRESENT_SRC_KHR, BOTTOM_OF_PIPE, NONE}` triple that phase's own
regression test already hand-built.

**This is exactly the gap `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own
V2 Revision Note 4 predicted would surface** — a render graph whose three
real passes never exercise a genuine cross-pass `ReadTexture()` — and this
phase's own manual-finalize workaround does NOT close that gap (see "What
was deliberately NOT done" below and Step 3.7's own outline-pass follow-up,
still unimplemented).

**Deviation 2 — `AddPresentPass()` takes `Game&`/`Renderer&` and an
`std::optional<float> directGameRenderAspect`, not just a swapchain handle
+ ImGui callback.** The strategy document's own Step 3.2 code sketch
doesn't show how the release-build ("`gameTarget == nullptr &&
sceneTarget == nullptr`") direct-to-swapchain case is supposed to work
once Game/Scene/Present are three independently-declared passes. Tracing
through the OLD code: in that case, there was only ONE `RecordFrame()`
call for the swapchain (from the old `Present()`), which cleared once,
drew Game's queued items, then drew ImGui (nothing, in a release build) —
all as ONE pass. A naive translation — a separate "GameView-direct" pass
followed by "Present," both writing the swapchain — would have the SECOND
pass's own clear (needed for the ordinary Editor case) silently ERASE the
first pass's just-rendered Game content, since both write the same
`finalOutputs` handle in write-after-write order. The fix: `AddPresentPass()`
does BOTH within ONE pass — clears the swapchain, optionally renders Game
directly into it (only when `directGameRenderAspect.has_value()`, which
also gates whether a depth-attachment write/clear is even declared, since
depth buffers for the swapchain are still lazily provisioned only in this
case — mirroring the old `FrameRecorder::HasQueuedDraws()`-driven decision,
now decided explicitly by the caller via `PresentViaRenderGraph()`'s
`needsSwapchainDepth` parameter), THEN records the ImGui overlay. Verified
by actually building and running a `-DGTE_ENABLE_EDITOR=OFF` configuration
(see "Verification performed" below).

## Known limitations / deliberately deferred work

- **GPU timing (`GpuTimingService`) is NOT wired into the render-graph
  passes in this session — matching Phase 6's own explicit scope
  decision.** `RENDERGRAPH_PHASE6_COMPLETION_REPORT.md`'s "A deliberate
  scope decision" section explicitly named this as Phase 7's job to
  finish; this session did not attempt it, for the same risk-management
  reason Phase 6 gave (a materially larger, riskier change to
  already-shipping, Tier-2 production code, better done as its own
  focused pass). Every `RenderGraph::PassGpuStats::timing` this session
  produces is honestly `GpuTimingSample::Status::Absent` — the Editor's
  "Profiler" panel will show "N/A" for GPU Timing on GameView/SceneView/
  Present from now on, never a fabricated `0.00 ms` (consistent with this
  engine's own "never default a GPU measurement that doesn't have a real
  value this frame to a bare numeric 0" rule — see `AGENTS.md`,
  "Profiling"). Draw-call/triangle counts (the OTHER half of the Profiler
  panel's per-pass data) ARE real and correct. **This is a genuine,
  observable regression versus pre-Phase-7 behavior** (GPU timing used to
  show real milliseconds for these three passes) and should be treated as
  the immediate top priority for whoever picks up the next session on this
  campaign.
- **The old `Renderer::Present()`/`FramePresenter::Present()` (FrameRecorder-
  based) method was NOT deleted.** Nothing in production code calls it
  anymore (confirmed via `findstr` search before and after this session's
  changes — `Application.cpp` was its only caller), but it was deliberately
  left in place rather than removed, to keep this already-large session's
  diff smaller and lower-risk. `RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md`'s
  own Step 3.4/3.5 calls for deleting this as part of THIS phase's
  deliverable — that cleanup is explicitly deferred, not forgotten.
  `Renderer::RenderOffscreen()`/`FramePresenter::RenderOffscreen()` (the
  OTHER old FrameRecorder-based path) is NOT dead code and must NEVER be
  removed — `src/Editor/AssetPreviewMesh.cpp`/`BoneViewerWindow.cpp` both
  still call it directly, independently of Game/Scene/Present, for their
  own Inspector/Bone-Viewer 3D previews (a legitimate, pre-existing
  architectural carve-out — see `AGENTS.md`, "Editor Module Structure").
- **Step 3.7's own cross-pass-read validation (the Scene-view outline-
  highlight post-process) was NOT implemented this session.** Per
  `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision Note 4 and
  this phase's own Step 3.7, this is the step that would prove the whole
  nine-phase campaign's actual value (a real pass reading another pass's
  output through the graph, with a real Phase-5-synthesized barrier in
  between) — as of this session, that capability remains completely
  unexercised in shipped code (see "Deviation 1" above for why even THIS
  phase's own three real passes don't need it). This should be treated as
  the second priority for a future session, alongside real GPU timing
  wiring.
- **Manual visual QA was not possible in this environment** (no
  screenshot/window-capture tool was available for an arbitrary desktop
  application in this session's toolset). What WAS verified: both the
  Editor build (`build/`) and a fresh `-DGTE_ENABLE_EDITOR=OFF` build
  (`build_noeditor/`, configured/built/run/torn-down entirely within this
  session) launch, run for several seconds under `run_app_background`, and
  exit cleanly under `stop_app_background` with no crash — this exercises
  BOTH `AddPresentPass()` branches (`directGameRenderAspect` set and
  unset) and the full offscreen regime end-to-end against a real Vulkan
  device, but does not confirm the RENDERED PIXELS are correct. A human
  should do a real visual pass (the strategy document's own Step 3.6
  checklist: docked layout, split Scene/Game, resize, minimize/restore,
  validation-layer output) before treating this migration as fully proven.

## Build system changes

- Root `CMakeLists.txt`: added `src/Application/RenderPasses.h`/`.cpp` to
  `gte_core`'s source list, right after `MemorySnapshotBuilder.h`.
- No `tests/CMakeLists.txt` changes were needed — the new clear-value
  tests were added to the EXISTING `RenderGraphBuilderTests.cpp` file.

## Verification performed

- Built `GreatTamanaEngineTests` (Debug, Ninja) from the existing `build/`
  tree — compiled with zero warnings/errors introduced by any new/changed
  file.
- Ran the Render Graph test subset in isolation
  (`--gtest_filter=*RenderGraph*`) — all **96** pass (the 92 pre-existing
  Phase 1–6 tests, unchanged, plus 4 new Phase 7 clear-value tests in
  `RenderGraphBuilderTests.cpp`).
- Ran the **entire** test suite — **617 tests total**, **616 passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest`, the same pre-existing
  machine-gated smoke test noted in every prior phase's report, unrelated
  to this change). **Zero regressions.**
- Built the real `GreatTamanaEngine.exe` (Editor build) — succeeded
  cleanly.
- Configured, built, ran, and tore down a completely separate
  `-DGTE_ENABLE_EDITOR=OFF -DGTE_BUILD_TESTS=OFF` build
  (`build_noeditor/`, deleted at the end of this session — not committed)
  — compiled cleanly, and the resulting `GreatTamanaEngine.exe` ran for
  several seconds with no crash, exercising the release-build "direct
  Game render into the swapchain" branch of `AddPresentPass()` end to end.
- Ran the real Editor-build `GreatTamanaEngine.exe` under
  `run_app_background`, confirmed via `tasklist` that it stayed alive
  (~10 seconds, no crash) before stopping it with `stop_app_background` —
  exercising the full two-`Execute()`-calls-per-frame path (offscreen
  Game+Scene regime, then the pipelined Present regime with real ImGui
  chrome) against a live Vulkan device with validation layers enabled
  (this build's default — see `Renderer.cpp`'s `kEnableValidation`).

## Acceptance criteria check (against the strategy document's own Step 3/Step 5)

- ✅ `Game::Render()`/`RenderSystem::Draw()`/`Renderer::Submit()` kept
  their exact pre-Phase-7 public signatures — verified by inspection (no
  edits to `Game.h`/`RenderSystem.h`) and by the full test suite passing
  unchanged (`tests/Game/RenderSystemTests.cpp` untouched).
- ✅ Exactly TWO `RenderGraph::Execute()` calls per frame, matching
  `ExecuteTimingMode::SynchronousImmediateReadback` (offscreen) /
  `PipelinedDeferredReadback` (present) — never merged into one, never
  split into three.
- ✅ `RenderGraphResourcePool::BeginFrame()` is triggered exactly once per
  frame, from the offscreen (first) call only — unchanged from Phase 6's
  own design, `RenderGraph::Execute()`'s own internal logic was not
  touched for this.
- ✅ A dependency-cycle exception from `RenderGraphCompiler::Compile()` is
  caught at the `Application::Run()` call sites (both regimes), logged,
  and asserted in debug builds — never silently swallowed.
- ✅ `-DGTE_ENABLE_EDITOR=OFF` release build: straight-to-swapchain
  rendering still works — verified by an actual clean configure/build/run
  this session (see "Verification performed" above) — the offscreen
  `Execute()` call is skipped entirely in this configuration (`gameTarget
  == nullptr && sceneTarget == nullptr` is always true when
  `NullEditorLayer` is linked in), and `AddPresentPass()`'s
  `directGameRenderAspect` branch is what renders Game directly into the
  swapchain.
- ✅ Draw-call/triangle counts for all three named passes are real,
  non-fabricated, sourced from `RenderGraph::LastKnownStatsFor()`.
- ⚠️ (documented, deliberately deferred) GPU timing for the three named
  passes is honestly `Absent`, not real — see "Known limitations" above.
  This is the ONE genuine, observable regression this phase introduces
  versus pre-Phase-7 behavior, and is flagged as the top follow-up
  priority.
- ⚠️ (documented, deliberately deferred) Step 3.7's cross-pass-read
  validation (the outline-highlight post-process) was not attempted this
  session — see "Known limitations" above.
- ⚠️ (documented, deliberately deferred) The old `Renderer::Present()`/
  `FramePresenter::Present()` scaffold was not deleted this session — see
  "Known limitations" above.

## What was deliberately NOT done (per the strategy document's own Step 4, plus this session's own deferrals above)

- No signature changes to `Game::Render()`, `RenderSystem::Draw()`, or
  `Renderer::Submit()`.
- No new rendering FEATURE was added as part of the core migration (no
  shadow pass, no post-process, no outline highlight) — this was a
  like-for-like translation, full stop, exactly as the strategy document's
  own "Step 4" requires. The one new capability added
  (`WriteColorAttachment()`/`WriteDepthStencilAttachment()`'s optional
  clear parameters) is infrastructure the three real passes needed to
  look correct, not a new user-facing feature.
- No incremental, pass-by-pass migration — Game view, Scene view, AND
  Present were all cut over together, in this one session, exactly as the
  strategy document's own "Step 4" requires ("we will not attempt this
  migration incrementally pass-by-pass").
- No merging of the two `Execute()` calls into one shared command
  buffer/one call.
- No MRT, memory aliasing, multi-queue submission, or compute passes —
  all still explicitly Phase 9 backlog, untouched by this phase.
- `GpuTimingService`'s fixed-3-slot pool was NOT replaced/generalized —
  see "Known limitations" above; this remains open work.

## Handoff notes for whoever picks up the next session

- **Top priority: wire real GPU timing into the render-graph passes.**
  `RenderGraph`'s own `m_synchronousTimingSlots`/`m_pipelinedTimingSlots`
  (`RenderGraphNameSlotTable` instances, Phase 6) are already exercised on
  every `Execute()` call (`(void)timingSlots.AssignOrGetSlot(pass.name)` —
  see `RenderGraph.cpp`) but nothing consumes the resulting slot index yet.
  `GpuTimingService`'s fixed 3-slot `VkQueryPool` (`GpuTimingSlot::
  Offscreen0/Offscreen1/SwapchainPresent`) needs to be generalized into a
  real, per-pass-name-keyed pool (matching the ALREADY-TESTED
  `RenderGraphNameSlotTable` shape) and threaded through
  `RenderGraph::ExecuteCompiledGraph()`'s own per-pass loop — likely
  requiring `vkCmdResetQueryPool`/`vkCmdWriteTimestamp2` calls positioned
  OUTSIDE each pass's own `vkCmdBeginRendering`/`vkCmdEndRendering`
  bracket (see `AGENTS.md`'s own Vulkan-spec constraint on
  `vkCmdResetQueryPool`), which will need careful placement given
  `RenderGraph::Execute()` currently opens/closes that bracket internally,
  per-pass, inside its own loop — this is real, non-trivial surgery, not a
  small addition.
- **Second priority: Step 3.7's cross-pass-read validation** — the
  Scene-view outline-highlight post-process (`TODO.md`, "Editor / Debug
  UI") is still the natural, already-scoped candidate. This is what
  finally proves the render graph's own most novel capability (a real,
  Phase-5-synthesized barrier for a genuine `ReadTexture()` between two
  passes) actually works in shipped code — see
  `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision Note 4 for
  why this matters more than it might look.
- **Delete the old `Renderer::Present()`/`FramePresenter::Present()`
  scaffold** once the two priorities above (and a real manual visual QA
  pass) are complete — confirmed unused by any production call site as of
  this session; `Renderer::RenderOffscreen()`/`FramePresenter::RenderOffscreen()`
  must NOT be touched (still load-bearing for `AssetPreviewMesh`/
  `BoneViewerWindow`).
- `RenderPasses.cpp`'s `kGameClearColor` constant is a HAND-DUPLICATED
  copy of `Game::Render()`'s own hardcoded `renderer.Clear(20, 20, 30,
  255)` call — if that literal ever changes, this constant must be updated
  to match, or Game/Scene/Present will visibly clear to the wrong color
  while `Game::Render()`'s own (now largely vestigial, for the render-
  graph passes) `Clear()` call quietly does nothing. A cleaner long-term
  fix (e.g. exposing the clear color from `Game` itself, or moving pass
  declaration to happen AFTER `Game::Render()`'s own state is known) was
  considered out of scope for this session — flagged here for whoever
  next touches this seam.
- A real, human-driven visual QA pass (the strategy document's own Step
  3.6 checklist — default docked layout, Scene+Game split, resize,
  minimize/restore, validation-layer output, "Memory"/"Profiler" panel
  sanity) has NOT been performed and should happen before this migration
  is considered fully trustworthy — this session's own verification was
  necessarily limited to "does it build, does it pass its test suite, does
  it run without crashing" (see "Known limitations" above).
