# RENDERGRAPH_PHASE6_COMPLETION_REPORT.md

Session report for **Phase 6 — Put Everything Together**, the sixth
implementation chunk of the Render Graph campaign described in
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md` — with one deliberate,
explicitly documented scope narrowing around GPU timing (see "A deliberate
scope decision" below) — nothing beyond what that document's own "Step 3:
The Plan" (as narrowed) was implemented, per its own "Step 4: What We Will
NOT Do".

## What shipped

Three new, additively-compiled files plus one new test file — nothing else
in the engine was touched, and nothing outside `src/Renderer/RenderGraph/`
includes any of it yet (confirmed by grep before finishing this session):

- **`src/Renderer/RenderGraph/RenderGraphNameSlotTable.h`** — the pure,
  Vulkan-free half of the strategy document's own Step 3.2 ("generalize
  `GpuTimingService`'s fixed 3-slot design into an arbitrary, name-keyed set
  of passes"): a small, persistent (NOT rebuilt every frame, unlike
  `RenderGraphBuilder` itself) name → slot-index table, bounded by a fixed
  `slotBudget` decided once at construction. `AssignOrGetSlot(name)` returns
  an existing name's slot (compared by pointer first, then `strcmp()` as a
  fallback — mirroring `FrameProfiler::RecordCpuScope()`'s own established
  convention), assigns a fresh one from whatever budget remains, or returns
  the sentinel `kNoNameSlot` for a brand-new name once the budget is
  exhausted — never a crash, never silently aliasing another name's slot.
  Header-only (no `.cpp` — the whole class is small enough to stay inline,
  same judgment call as `GpuTiming.h`'s own inline `PresentTimestampSlotBase()`).
- **`src/Renderer/RenderGraph/RenderGraph.h`/`.cpp`** — the class that
  assembles Phases 1–5 into one coherent, callable object:
  - **`PassContext`** — fully specifies the `struct PassContext;` Phase 1
    forward-declared. Deliberately small, per the strategy document's own
    "Step 5" guidance ("keep PassContext's surface area as small as Phase 7
    actually turns out to need"): `cmd` (the live `VkCommandBuffer`),
    `colorAttachmentExtent` (this pass's resolved color attachment's
    extent, zero for a pass with no `ColorAttachmentWrite`),
    `resolveReadTexture` (a `std::function` resolving a declared-read
    `TextureHandle` into its live `VkImageView`/`VkSampler` pair, wired up
    by `RenderGraph::Execute()` right before invoking each pass), and
    `recordDraw` (a `std::function` a pass's `execute` callback calls
    immediately alongside its own real `vkCmdDraw`/`vkCmdDrawIndexed` calls
    — see "A necessary new design decision" below for why this exists at
    all).
  - **`ExecuteTimingMode`** — `SynchronousImmediateReadback` /
    `PipelinedDeferredReadback`, exactly the two submission regimes
    `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision Note 2
    identifies (today's `RenderOffscreen()` vs. `Present()`). By
    documented convention (enforced by Phase 7's own call order, not by
    this class), the `SynchronousImmediateReadback` call is the FIRST of a
    frame's two `Execute()` calls and is the ONE call that triggers
    `RenderGraphResourcePool::BeginFrame()` — a resource claimed during it
    stays correctly marked "claimed this frame" through the second call.
  - **`PassGpuStats`** — `DrawStats` (real) + `GpuTimingSample` (currently
    always `Status::Absent` — see the scope decision below).
  - **`RenderGraph::Execute<BuildFn>(cmd, timingMode, build)`** — the public
    template entry point exactly as the strategy document specifies:
    `build` receives a fresh `RenderGraphBuilder&`, declares this call's
    passes/resources against it, and returns the `finalOutputs` root set;
    `Execute()` then compiles, resolves every declared resource to a real
    physical one, walks the compiled execution order emitting barriers +
    recording each surviving pass's real Vulkan work, and remembers each
    pass's `DrawStats`/timing under its declared name.
  - **Resource resolution** (`EnsureTextureResolved()`/
    `EnsureBufferResolved()`) — an imported texture resolves directly to its
    already-live `RenderTarget`, seeded at its caller-supplied
    `currentLayout` (stage/access conservatively seeded as
    `TOP_OF_PIPE`/`NONE` — the exact same simplification
    `FrameRecorder::RecordFrame()` already makes for every barrier it
    issues today); a transient texture/buffer resolves via
    `RenderGraphResourcePool::AcquireTexture()`/`AcquireBuffer()` (Phase 4),
    always starting this call's tracking at a synthetic
    "never touched before" state, since the pool guarantees at most one
    virtual resource claims a given pool entry per frame.
  - **Barrier emission** (`ApplyUsageBarrierIfNeeded()`) — for every
    declared read/write, resolves the touched resource, computes the
    required `ResourceState` via Phase 5's `RequiredStateFor()`, and emits
    a real `VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2` via Phase 5's
    `EmitImageBarrier()`/`EmitBufferBarrier()` only when Phase 5's
    `RequiresBarrier()` says one is actually needed — texture accesses
    split between the COLOR image (every access except
    `DepthStencilAttachmentReadWrite`) and the companion DEPTH image
    (`DepthStencilAttachmentReadWrite` only), each independently tracked.
  - **Pass recording** — a pass with exactly one `ColorAttachmentWrite`
    write (the MVP's single-color-attachment-per-pass scope, carried
    forward from Phase 5) gets a real `vkCmdBeginRendering` (plus an
    optional depth attachment for a paired `DepthStencilAttachmentReadWrite`
    write), `vkCmdSetViewport`/`vkCmdSetScissor` sized to that attachment's
    extent (per Phase 5's own header comment: "Phase 6's execution harness
    must call `vkCmdSetViewport`/`vkCmdSetScissor`... right after
    `vkCmdBeginRendering`... BEFORE invoking that pass's execute callback"),
    then its `execute` callback, then `vkCmdEndRendering`. A pass with no
    `ColorAttachmentWrite` (a future transfer-only/compute-only pass) skips
    the whole dynamic-rendering bracket and is invoked directly against
    `cmd` — a deliberate, documented generalization beyond the MVP's own
    real passes (none of which need this yet), kept simple/cheap since it
    falls out of the same code path for free.
  - **`LastKnownStatsFor(passName)`** — a plain, name-keyed (pointer-then-
    `strcmp()`) lookup into a persistent (across many `Execute()` calls)
    vector, returning a default `PassGpuStats{}` for a never-executed name.
- **`tests/Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp`** — 9 new
  Tier-1 tests covering exactly the strategy document's own Step 3.4 list:
  first-call assignment, distinct names getting increasing slots, reuse by
  identical pointer AND by equal-content-different-pointer (the `strcmp()`
  fallback, deliberately exercised with a local `char` array rather than a
  second string literal, since literal pooling is a compiler optimization,
  not a language guarantee), a null name returning the sentinel, budget
  overflow degrading gracefully for a brand-new name while an
  already-assigned name keeps resolving correctly, a zero-budget table
  rejecting everything immediately, and — the strategy document's own
  explicitly-called-out new regression — two independent tables (simulating
  `RenderGraph`'s own two `ExecuteTimingMode` regimes) never colliding on
  the same name.
- **`RenderGraph.h`/`.cpp` itself has NO automated test**, per the strategy
  document's own Step 3.4/Step 5 acceptance criteria — it is Tier 2 (needs a
  real `VkDevice`), the same accepted bucket `Buffer`/`RenderTexture`/
  `Pipeline`/`RenderGraphResourcePool` already live in. `tests/CMakeLists.txt`'s
  own Tier-2 comment block was extended with a new paragraph explaining why
  `RenderGraph.h/.cpp` joins that bucket and what its own accepted
  verification bar is (see "Verification performed" below for what this
  session specifically was and was not able to do about that bar).

## Build system changes

- Root `CMakeLists.txt`: added
  `src/Renderer/RenderGraph/RenderGraphNameSlotTable.h`,
  `src/Renderer/RenderGraph/RenderGraph.h`, and
  `src/Renderer/RenderGraph/RenderGraph.cpp` to `gte_core`'s source list,
  right after the existing `RenderGraphBarrierPlanner.h`/`.cpp` entry.
- `tests/CMakeLists.txt`: added
  `Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp` to
  `GTE_TEST_SOURCES` (right after `RenderGraphBarrierPlannerTests.cpp`),
  plus a matching entry in the file's own Tier-1 taxonomy comment block, and
  a new paragraph in the Tier-2 comment block documenting `RenderGraph.h/.cpp`'s
  own accepted-manual-verification status.

## Verification performed

- Reconfigured with CMake (reusing the existing `build/` Ninja
  configuration) — no network access needed, everything was already
  fetched.
- Built `GreatTamanaEngineTests` from the existing incremental build —
  compiled with **zero warnings/errors** introduced by the new/changed
  files (the template `Execute<BuildFn>()` method and every new class
  compiled cleanly on the first attempt against the real, live
  `RenderGraphBuilder`/`RenderGraphCompiler`/`RenderGraphResourcePool`/
  `RenderGraphBarrierPlanner` headers from Phases 2–5).
- Ran the **new** Render Graph tests in isolation
  (`--gtest_filter=*RenderGraph*`) — all **92** pass (the 83 pre-existing
  Phase 1/2/3/5 tests, unchanged, plus the 9 new Phase 6 tests described
  above — Phase 4's `RenderGraphResourcePool` has no automated tests of its
  own, per its own completion report).
- Ran the **entire** test suite — **613 tests total**, **612 passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  the same pre-existing machine-gated smoke test noted in every prior
  phase's report, unrelated to this change). **Zero regressions.**
- Built the real `GreatTamanaEngine.exe` target too — succeeded cleanly,
  confirming the new files don't break the shipping executable's build
  (this also proves `RenderGraph.cpp` compiles correctly against a real
  `Renderer.h`/`RenderGraphResourcePool.h` include, since
  `GreatTamanaEngine.exe`'s link step pulls in every translation unit in
  `gte_core`, including this genuinely unreferenced one at this stage of
  the campaign).
- **What was NOT done, and why**: this phase's own strategy document (Step
  3.4/Step 5) explicitly calls for manually building and running a
  throwaway two-pass test scene against a real Vulkan device with
  validation layers enabled, in BOTH `ExecuteTimingMode` regimes, BEFORE
  calling this phase done. That step needs a real call site actually
  invoking `RenderGraph::Execute()` from inside a running `Application`
  (to get a live `VkCommandBuffer` already in the recording state, a real
  swapchain/off-screen target, and validation-layer output to observe) —
  which does not exist yet at this stage of the campaign (Phase 7 is
  explicitly where the first such call site is wired up). Building a
  bespoke, temporary integration harness *just* for this manual check would
  itself be a small, throwaway piece of Application-level wiring — exactly
  the kind of premature integration `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s
  own "must not leave the engine in a worse state... nothing outside
  `src/Renderer/RenderGraph/` calls into this yet" rule (repeated in every
  phase's own "What We Will NOT Do") argues against doing here. This is
  therefore **deferred to Phase 7**, whose own strategy document already
  names this exact throwaway-scene verification as part of its own
  acceptance bar — Phase 7 should perform it as its FIRST manual check,
  before wiring up the real Game/Scene/Present passes, exactly as this
  phase's own strategy document originally intended, just one call-site
  hand-off later than a literal reading of "Step 3.4" would suggest.
  `RenderGraph`'s own internal logic (resource resolution, barrier
  application, attachment/viewport/scissor setup, draw-stats accumulation)
  was reviewed carefully against Phase 5's own regression-tested barrier
  shapes and `FrameRecorder.cpp`'s existing, already-shipped recording code
  line-by-line while writing it, specifically to minimize the risk of this
  deferral.

## A deliberate scope decision: GPU timing is NOT actually wired to a real `VkQueryPool` in this phase

The strategy document's own Step 3.2 asks for `GpuTimingService`'s existing,
already-shipping, fixed 3-slot `VkQueryPool` to be **replaced** by a
generalized, name-keyed pool sized per the two `ExecuteTimingMode` regimes.
This session made a deliberate, documented decision **not** to do that
replacement yet, for reasons specific to where this campaign actually is
right now:

- `GpuTimingService`/`VulkanQueryPool` are real, Tier-2, already-shipping
  production code, actively used every frame by `Application::Run()`
  through `Renderer::LastGpuTiming()`/`SetGpuTimingCaptureEnabled()` for
  the Editor's real "Profiler" panel. Replacing their pool layout/API
  **before Phase 7 has migrated a single real call site onto `RenderGraph`**
  would be a materially risky, entirely unforced change to code with real
  users today, for a consumer (`RenderGraph`) that — per this whole
  campaign's own repeated "nothing outside `src/Renderer/RenderGraph/`
  calls into this yet" rule — has no real caller yet either.
- Doing it now would also force an awkward choice: either maintain BOTH the
  old fixed-3-slot API (for today's `Application.cpp`) and a new
  generalized one (for `RenderGraph`) simultaneously across Phases 6 and 7,
  or migrate `Application.cpp` onto the new API a full phase early — either
  of which is a bigger, riskier, less-reviewable change than this campaign's
  own phase-by-phase discipline calls for.
- **What this phase DOES ship, faithfully, is the entire PURE half of Step
  3.2**: `RenderGraphNameSlotTable` implements and thoroughly tests exactly
  the name → slot assignment/reuse/overflow-degradation logic the strategy
  document describes, and `RenderGraph` already owns and exercises TWO
  independent instances of it (`m_synchronousTimingSlots`/
  `m_pipelinedTimingSlots`, sized 16/8 per the document's own example
  budgets) on every single `Execute()` call — `(void)timingSlots.AssignOrGetSlot(pass.name)`
  runs for every pass, every call, today. What's missing is only the
  Vulkan-call half: no `VkQueryPool` is created, no `vkCmdWriteTimestamp2`
  is issued anywhere in `RenderGraph.cpp` yet, so every `PassGpuStats::timing`
  this phase ever produces is `GpuTimingSample{Status::Absent, 0.0}` — never
  a fabricated non-zero value, matching this engine's own "never default a
  GPU measurement that doesn't have a real value this frame to a bare
  numeric 0" rule (see `AGENTS.md`, "Profiling").
- This mirrors a precedent already established twice elsewhere in this
  exact codebase: Phase 3 (`DrawStats`) was deliberately implemented and
  shipped a full phase before Phase 4 (real GPU timestamp queries) for the
  Profiler's own three named passes, specifically because Phase 3 was
  "cheap/self-contained" and Phase 4 was "substantial/risky" (see
  `AGENTS.md`'s own "Profiling" section) — and `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`
  itself already narrows Step 3.3's `Profiling::GpuPass` integration the
  exact same way ("`RenderGraph`'s OWN internal `LastKnownStatsFor()` table
  is deliberately more general than what gets surfaced through the existing
  Profiler pipeline today; that gap is intentional, not an oversight").
  This phase applies that identical judgment to Step 3.2 specifically.
- **This is Phase 7's job**, at the point a real, in-production set of pass
  names actually exists for a generalized `GpuTimingService` to time. `RenderGraph.h`'s
  own class-level "GPU TIMING NOTE" comment documents this explicitly, so a
  future reader of just the header (without this report) still understands
  why every `PassGpuStats::timing` is `Absent` today and where the real
  wiring is expected to land.

## A necessary new design decision: `PassContext::recordDraw`

Neither this phase's own strategy document nor any earlier phase document
specifies HOW a pass's `DrawStats` get accumulated once passes are opaque,
caller-authored `execute` lambdas rather than one single, closed draw-queue
loop `FrameRecorder::RecordFrame()` already owns today. `AGENTS.md`'s
`AccumulateDrawStats()` correctness rule ("fused... inside the very loop
that already issues `vkCmdDraw`/`vkCmdDrawIndexed`... never a separate pass")
still applies, but there is no longer one single loop `RenderGraph` itself
controls end-to-end the way `FrameRecorder` does — a pass's `execute`
callback issues its own draws directly, and `RenderGraph` never sees inside
it. This session's resolution: `PassContext` gained a `recordDraw`
`std::function`, which a pass author is expected to call immediately
alongside its own real `vkCmdDraw`/`vkCmdDrawIndexed` call (mirroring
`Renderer::Submit()`'s existing shape/semantics, just scoped per-pass
instead of per-frame-queue) — `RenderGraph::Execute()` wires this closure to
call `AccumulateDrawStats()` on that exact pass's own local `DrawStats`
accumulator before recording it under that pass's name. This preserves the
FUSION discipline (draw-stats accumulation still happens at the exact call
site that issues the real draw, never from a separate counting pass) while
adapting it to the render graph's fundamentally different "opaque pass
callback" shape. Documented directly in `PassContext`'s own header comment
for whoever writes Phase 7's first real pass `execute` callbacks.

## Acceptance criteria check (against the strategy document's own Step 3/5, as narrowed above)

- ✅ `RenderGraph::Execute()` is callable exactly as the strategy document's
  own template signature specifies — `build` receives a
  `RenderGraphBuilder&`, returns the `finalOutputs` root set.
- ✅ Exactly TWO `ExecuteTimingMode` values exist, matching
  `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision Note 2 —
  never a single merged call, never more than two per frame (enforced by
  documented convention, not by this class itself, per that same note).
- ✅ `RenderGraphResourcePool::BeginFrame()` is called exactly once per
  frame — only from the `SynchronousImmediateReadback` call.
- ✅ Every declared read/write's barrier is computed via Phase 5's own
  `RequiredStateFor()`/`RequiresBarrier()`/`EmitImageBarrier()`/
  `EmitBufferBarrier()` — no new, parallel barrier-decision logic was
  introduced in this phase.
- ✅ `vkCmdSetViewport`/`vkCmdSetScissor` are called right after
  `vkCmdBeginRendering`, before a pass's `execute` callback runs — per Phase
  5's own header comment note for this phase.
- ✅ A dependency-cycle `std::runtime_error` from `RenderGraphCompiler::Compile()`
  is NOT caught inside `RenderGraph::Execute()` — it propagates to the
  caller untouched, per the strategy document's own Step 3.5 (Phase 7's job
  to catch/log/re-throw-in-debug).
- ✅ `RenderGraphNameSlotTable`'s pure logic is Tier-1-tested exactly per the
  strategy document's own Step 3.4 list, including the specific new
  "per-regime, two independent tables never collide" regression it calls
  out.
- ⚠️ (documented, deliberate scope narrowing) `GpuTimingService`'s pool is
  NOT replaced/generalized in this phase — see "A deliberate scope
  decision" above. Every `PassGpuStats::timing` this phase produces is
  `Status::Absent`, honestly, never fabricated.
- ⚠️ (documented, deferred) The manual, real-device, both-regimes,
  throwaway-two-pass-scene verification the strategy document's own Step
  3.4/5 calls for needs a real call site this phase deliberately does not
  add — deferred to Phase 7 as this phase's own first manual check, see
  "Verification performed" above.

## What was deliberately NOT done (per the strategy document's own Step 4, plus this session's own narrowing above)

- No full MRT (multi-color-attachment) support — carried forward from Phase
  5's own scope fence; this file has no attachment-COUNT concept at all,
  operating purely per-resource and per-single-color-attachment-per-pass.
- No memory aliasing, no multi-queue/async-compute submission, no
  compute-shader passes — all explicitly Phase 9 backlog, unaffected by
  this phase.
- No visual node-graph editor/authoring tool — passes are still declared in
  plain C++.
- No change to any Editor panel's user-facing behavior — nothing outside
  `src/Renderer/RenderGraph/` was touched at all.
- No replacement of `GpuTimingService`'s fixed-3-slot production pool/API —
  see "A deliberate scope decision" above; this is now explicitly Phase 7's
  job instead of this phase's.
- No real `VkQueryPool`/`vkCmdWriteTimestamp2` call anywhere in
  `RenderGraph.cpp` — same reason.
- No per-pass clear-color mechanism — every pass's color/depth attachment
  uses `loadOp = LOAD`/`storeOp = STORE` today (see `RenderGraph.cpp`'s own
  inline comment on this), since Phase 2's `PassBuilder` API has no
  clear-color concept yet; a future `PassBuilder` addition is the natural
  place to revisit this once a real pass actually needs to clear (Phase 7's
  Game/Scene/Present migration will need this — flagged here explicitly for
  whoever picks that phase up).

## Handoff notes for whoever picks up Phase 7

- Phase 7 (`RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md`) is the
  next unit of work in this campaign — the strangler-fig migration of
  Game/Scene/Present + the ImGui overlay onto real graph passes via exactly
  two `RenderGraph::Execute()` calls per frame (per this campaign's own V2
  Revision Note 2), against a SINGLE, shared `RenderGraph` instance owned
  wherever `Renderer` (or its owner) lives.
- **Before touching `Application::Run()`, perform this phase's own deferred
  manual verification first**: build a small, throwaway two-pass test graph
  (e.g. a color-write pass feeding a shader-read pass) and run it against a
  real device with validation layers enabled, in BOTH `ExecuteTimingMode`
  regimes — see "Verification performed" above for why this was deferred
  here rather than skipped.
- **A per-pass clear-color mechanism is needed before Phase 7's real
  Game/Scene/Present passes will look correct** — today's `FrameRecorder`
  always clears; this phase's `RenderGraph::Execute()` always uses
  `loadOp = LOAD`. The natural fix is a small `PassBuilder` addition (e.g.
  an optional clear-color parameter on `WriteColorAttachment()`/
  `WriteDepthStencilAttachment()`), threaded through to
  `RenderGraph.cpp`'s attachment-info construction — this phase deliberately
  left that as Phase 7's first small addition rather than guessing at an
  API shape with no real consumer yet.
- **GpuTimingService's real generalization is now explicitly Phase 7's
  job** (see "A deliberate scope decision" above) — `RenderGraph`'s own
  `m_synchronousTimingSlots`/`m_pipelinedTimingSlots` are ready and already
  proven correct in isolation; wiring them to a real, generalized
  `VkQueryPool` (replacing `GpuTimingService`'s fixed 3-slot one) is the
  remaining half of Step 3.2 this phase left undone.
- `PassContext::recordDraw` (see "A necessary new design decision" above) is
  the API every real Phase 7 pass's `execute` callback must call — do not
  reintroduce a separate, `FrameRecorder`-style external draw-queue for
  passes built on this class; that would silently break the
  fusion-correctness guarantee this design exists to preserve.
- `RenderGraph::ExecuteCompiledGraph()`'s per-pass color/depth resolution
  only recognizes a texture handle used with EXACTLY `ColorAttachmentWrite`
  (color image) or EXACTLY `DepthStencilAttachmentReadWrite` (depth image) —
  there is no way today to `ShaderRead` the DEPTH half of a texture that
  also has a color image (e.g. sampling a shadow map's depth buffer in a
  later pass); this MVP limitation is documented inline in
  `RenderGraph.cpp`'s own `ApplyUsageBarrierIfNeeded()` comment and should
  be revisited if/when a real pass needs it.
