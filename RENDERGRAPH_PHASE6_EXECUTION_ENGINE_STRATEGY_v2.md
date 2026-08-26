# RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md
### (Part 6 of 9 - see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` for the full campaign)

## V2 Revision Notes (2nd iteration review)

This phase document needed the most substantive rework in this iteration.
Four changes:

1. **Resolved a real contradiction between this document and Phase 7 about
   how many `RenderGraph::Execute()` calls happen per frame, and why it
   matters.** v1's own Step 1/Step 4 text said "one `Execute()` call...
   per logical frame's worth of GPU work (which, post-Phase-7, means once
   per `RenderOffscreen()`-equivalent call PLUS once for the swapchain
   Present-equivalent call...)" - which, read carefully, actually implies
   MULTIPLE calls per frame - but `RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v1.md`'s
   own `Application::Run()` code sample showed exactly ONE `Execute()` call
   wrapping Game view, Scene view, AND Present together, sharing one
   `VkCommandBuffer` and one `outputs` vector. These cannot both be true,
   and the difference is not cosmetic: today's engine has TWO GENUINELY
   DIFFERENT submission/synchronization regimes that share no command
   buffer at all -
   - `FramePresenter::RenderOffscreen()` (Game view, Scene view) uses its
     own dedicated command buffer (`m_offscreenCommandBuffer`) and its own
     dedicated fence, and blocks SYNCHRONOUSLY (`vkWaitForFences(...)`)
     before returning - see `FramePresenter.cpp`'s own comment: "Synchronous
     for now."
   - `FramePresenter::Present()` (the swapchain) is deliberately
     NON-blocking/pipelined across `kFramesInFlight == 2` frames' worth of
     command buffers/fences/semaphores, can independently return
     `std::nullopt` (minimized window, pending resize, just-recreated
     swapchain), and reads back GPU timing from a PAST frame's slot rather
     than the current one (see `GpuTimingService::ReadPresentResultIfAvailable()`,
     which depends on a per-frame-in-flight "has this slot ever been
     written" warm-up flag - there is no equivalent concept on the
     Offscreen side, which is always synchronous and therefore always
     "immediately available").
   Silently merging these into one shared command buffer/one `Execute()`
   call, as Phase 7 v1's sample literally did, would force one of two
   outcomes neither document actually chose: (a) Present becomes
   synchronous too - a real, measurable frame-pacing regression, directly
   contradicting Phase 7's own promise of "zero observable behavior
   difference to a user" - or (b) `RenderGraph::Execute()` secretly splits
   its recorded work across multiple command buffers/submissions
   internally, a substantial, unspecified feature neither v1 document
   described building. **Decision (see Step 3.1 below and Phase 7 v2):
   `RenderGraph::Execute()` is called exactly TWICE per frame** - once for
   the synchronous offscreen regime (a graph containing the Game-view and
   Scene-view passes together, since both already share that regime and
   ordering between them is irrelevant), and once for the async/pipelined
   swapchain regime (a graph containing only the Present pass) - matching
   today's two real regimes exactly, one-for-one. This is a permanent
   design decision for this campaign, not a stepping stone toward merging
   them later (see Phase 0 v2's own "What We Will NOT Do").
2. **`GpuTimingSlot` generalization must explicitly preserve the
   synchronous-offscreen-vs-pipelined-present asymmetry, not flatten it
   away.** v1's Step 3.2 proposed replacing the fixed 8-slot `VkQueryPool`
   layout with "a pool sized `2 * kMaxTrackedPasses`" and a name-keyed
   slot table, without addressing that the CURRENT 8-slot layout is not
   uniform: 4 slots (2 pairs) belong to the two offscreen passes and are
   read back the SAME call, right after a blocking fence wait; the other 4
   slots (2 frame-in-flight pairs) belong to Present and are read back a
   LATER frame, gated by a per-slot "ever written" flag rather than a
   simple "did this run" check. A naive, uniform "N passes, 2*N slots"
   generalization would either lose this distinction (reading Present's
   timing synchronously, which isn't safe without the fence wait Present
   deliberately avoids) or need a per-pass flag for "is this pass
   synchronous or pipelined," which v1 never specified. This is now made
   explicit in Step 3.2 below: `RenderGraph` tags each of its two
   `Execute()` calls (see point 1) with which readback discipline applies,
   and `GpuTimingService`'s generalized slot table carries that same
   distinction per logical pass, exactly mirroring today's split rather
   than inventing a new, unproven unification.
3. **Viewport/scissor state is now explicitly `RenderGraph`'s own
   responsibility, matching Phase 5 v2's note.** Neither v1 document ever
   assigned who calls `vkCmdSetViewport`/`vkCmdSetScissor` under the new
   model - added to Step 3.1 below.
4. **A short robustness note on Phase 3's cycle-detection exception.**
   Since the whole graph is rebuilt/recompiled every single frame (this
   phase's own Step 4, unchanged from v1), a pass-declaration bug that
   introduces a cycle would throw on every subsequent frame, not once.
   Step 3.1 below adds an explicit note that `Application::Run()`'s new
   `Execute()` call sites should not let this become a silent, repeating
   crash loop with no diagnostic - see the added guidance below.

Everything else in this phase is unchanged from v1.

---

## Step 1: The Goal (Where are we going?)

Assemble Phases 1-5 into one coherent, callable class - `RenderGraph` - that
a future caller (Phase 7) can use exactly like this: build a
`RenderGraphBuilder` fresh every frame, declare passes/resources against
it, then call one method that compiles, realizes physical resources,
records every barrier and every pass's real Vulkan work into a supplied
`VkCommandBuffer`, and reports back per-pass `DrawStats` plus per-pass GPU
timing - generalizing `GpuTimingService`'s fixed 3-slot design into an
arbitrary, name-keyed set of passes, WITHOUT losing the real
synchronous-vs-pipelined distinction between today's offscreen and
swapchain-present regimes (see V2 Revision Note 2). By the end of this
phase, `RenderGraph` is fully capable of replacing `FrameRecorder`+the
pass-orchestration half of `FramePresenter`, but **nothing calls it yet** -
that cut-over is Phase 7's job specifically, kept separate so this phase
can be built, compiled, and manually smoke-tested in isolation first.

## Step 2: The Situation / The Problem (Where are we now?)

Every piece this phase needs already exists in isolation (Phases 1-5) but
nothing yet COMBINES them into the one thing an actual frame-recording call
site can hold onto and call once. Today, that combining role is played by
`FrameRecorder`+`FramePresenter` together (`Present()`/`RenderOffscreen()`),
each hand-wired to one fixed target shape, and - crucially - **each wired to
a genuinely different submission/synchronization regime** (see V2 Revision
Note 1's bulleted comparison above). This phase's `RenderGraph` class is the
direct architectural REPLACEMENT for that combined role - once it exists
and is proven correct in isolation, Phase 7 is "just" a rewiring exercise,
not a redesign - but "replacement" must mean replacing BOTH regimes
faithfully, not collapsing them into one that never existed before.

This phase is also where two more of this engine's existing per-frame
systems that are currently STATICALLY tied to exactly three named passes -
`GpuTimingService` (`GpuTimingSlot::Offscreen0/Offscreen1/SwapchainPresent`)
and `DrawStats` accumulation (`Profiling::GpuPass::GameView/SceneView/
Present`) - must be GENERALIZED to work with an arbitrary, frame-to-frame
declared set of passes, since a render graph's whole premise is that the
pass list is not fixed at compile time. `GpuTimingService`'s own EXISTING
asymmetry (offscreen slots read synchronously; Present slots read via a
per-frame-in-flight "ever written" flag on a later frame - see
`GpuTimingService.cpp`'s `ReadOffscreenResultNow()` vs.
`ReadPresentResultIfAvailable()`/`MarkPresentSlotWritten()`) must be
preserved by whatever generalized, name-keyed design replaces it - see Step
3.2 below.

## Step 3: The Plan (How will we get there?)

### 3.1 - New file: `src/Renderer/RenderGraph/RenderGraph.h/.cpp`

```cpp
struct PassContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    // Resolves a declared color/depth WRITE for the currently-executing
    // pass into something vkCmdBeginRendering already bound for it - a
    // pass's execute lambda never manually calls vkCmdBeginRendering
    // itself; RenderGraph does that immediately before invoking execute,
    // and vkCmdEndRendering immediately after it returns (mirrors
    // FrameRecorder::RecordFrame()'s existing begin/end bracketing, now
    // parameterized per pass instead of hardcoded once per RecordFrame()
    // call). RenderGraph ALSO calls vkCmdSetViewport/vkCmdSetScissor,
    // sized to this pass's resolved color attachment's extent, immediately
    // after vkCmdBeginRendering and before invoking execute - mirroring
    // FrameRecorder::RecordFrame()'s own existing behavior exactly (see
    // Phase 5 v2's own note) - so a pass author's execute callback never
    // needs to remember to do this itself.

    // Resolves a declared READ (a texture this pass named via
    // PassBuilder::ReadTexture()) into a sampleable VkImageView/VkSampler
    // pair, or (for a future descriptor-set-bound material texture) a
    // VkDescriptorSet - exact shape finalized against Phase 7's real needs.
    VkImageView ResolveReadTexture(TextureHandle) const;
};

struct PassGpuStats { DrawStats drawStats; GpuTimingSample timing; };

// v2: which readback discipline this Execute() call's passes use for GPU
// timing - see V2 Revision Note 2. Threaded through so RenderGraph's
// generalized, name-keyed timing table never has to guess which of the
// two existing GpuTimingService regimes a given pass belongs to.
enum class ExecuteTimingMode {
    // Fully synchronous - this call's command buffer is guaranteed
    // complete (via a blocking fence wait) before this Execute() call
    // returns, so every pass's GPU timing is safely readable immediately -
    // matches today's RenderOffscreen() regime exactly (Game view, Scene
    // view).
    SynchronousImmediateReadback,
    // Pipelined/frame-in-flight - this call's command buffer may still be
    // executing on the GPU when this Execute() call returns; GPU timing
    // for THIS call's passes is only safely readable on a LATER Execute()
    // call, once that same frame-in-flight slot's own fence has separately
    // proven complete - matches today's Present() regime exactly.
    PipelinedDeferredReadback,
};

class RenderGraph {
public:
    explicit RenderGraph(Renderer& renderer);

    // One call per REGIME per frame (see V2 Revision Note 1 - NOT one call
    // per frame overall): `build` receives a RenderGraphBuilder& to
    // declare this call's passes/resources into (mirrors Phase 2 exactly)
    // and returns the handle(s) that are this call's real, externally-
    // observable outputs (Phase 3's `finalOutputs` root set). `cmd` is a
    // command buffer already appropriate for `timingMode`'s regime -
    // RenderGraph never allocates/submits it itself (see Step 4 below,
    // unchanged from v1).
    template <typename BuildFn>
    void Execute(VkCommandBuffer cmd, ExecuteTimingMode timingMode, BuildFn&& build);

    // Keyed by the SAME string literal `name` passed to AddPass() - see
    // 3.2 below for why this must stay a name-keyed lookup, not an index.
    PassGpuStats LastKnownStatsFor(const char* passName) const;

private:
    RenderGraphResourcePool m_resourcePool; // Phase 4
    // Per-resource ResourceState tracking (Phase 5) persists ACROSS frames
    // for imported resources only (a Game-view RenderTexture's layout at
    // the END of frame N is exactly its layout at the START of frame N+1) -
    // transient/pooled resources always restart at a synthetic UNDEFINED
    // state each frame (Phase 4/5's own reasoning).
};
```

`Execute()`'s single call, per invocation, does exactly these steps, in
order:

1. `m_resourcePool.BeginFrame()` (Phase 4) - **called only on the FIRST of
   this frame's two `Execute()` calls** (the offscreen one, by convention -
   see Phase 7 v2 for the exact call order), so a resource pooled/claimed
   during the offscreen call remains correctly marked "claimed this frame"
   through the Present call too, and `BeginFrame()`'s own "unclaim
   everything" reset genuinely happens exactly once per frame, not once per
   `Execute()` call.
2. Construct a fresh `RenderGraphBuilder`, invoke `build(builder)`, get back
   `CompiledGraphInput` + the caller's declared `finalOutputs`.
3. `RenderGraphCompiler::Compile(input, finalOutputs)` (Phase 3) ->
   `CompiledGraph`. **v2: wrapped in a try/catch for
   `std::runtime_error` (Phase 3's own cycle-detection exception) - see
   Step 3.5 below for why this must not become a silent, every-frame crash
   loop.**
4. For each virtual resource with a non-`{-1,-1}` lifetime, resolve it to a
   physical resource - either the imported `RenderTarget` (Phase 2/4) or a
   freshly-`AcquireTexture()`'d pooled one (Phase 4, using the resource's
   stored name from Phase 2's builder - see Phase 2 v2/Phase 4 v2 for why
   this is now a single, unambiguous flow), and seed its `ResourceState`
   (Phase 5) accordingly.
5. Walk `executionOrder` (Phase 3) one pass at a time: emit every barrier
   this pass's declared reads/writes require (Phase 5), `vkCmdBeginRendering`
   with this pass's single resolved color attachment (plus optional depth -
   see Phase 5 v2, MRT deferred to Phase 9), `vkCmdSetViewport`/
   `vkCmdSetScissor` sized to that attachment's extent (v2 - see Phase 5
   v2's own note and this document's `PassContext` comment above), invoke
   that pass's captured `execute` callback with a `PassContext` wired to
   this pass's resolved attachments, fused `AccumulateDrawStats()` calls
   exactly as `FrameRecorder::RecordFrame()` already does today (this
   fusion rule, AGENTS.md's own correctness requirement, is UNCHANGED by
   this whole campaign - only WHICH loop it lives inside changes),
   `vkCmdEndRendering`.
6. Record this call's per-pass `DrawStats`/GPU-timing results into an
   internal, name-keyed table for `LastKnownStatsFor()` to serve back -
   **timing results are recorded/read back according to `timingMode`, per
   V2 Revision Note 2/Step 3.2 below, not uniformly.**

### 3.2 - Generalizing `GpuTimingService` from 3 fixed slots to N named
passes, WITHOUT losing the synchronous-vs-pipelined split

`GpuTimingService`'s existing fixed 8-slot `VkQueryPool` layout
(`src/Renderer/GpuTimingService.h`'s own class comment table) is REPLACED
by a pool sized `kSynchronousSlotBudget * 2 + kPipelinedSlotBudget *
kGpuTimingFramesInFlight * 2` (two generous, independently-sized fixed
upper bounds - e.g. 16 synchronous-regime passes, 8 pipelined-regime
passes - still a single, never-resized `VkQueryPool`, matching the "fixed
pool, never resized" design principle exactly). A pass's slot is assigned
from whichever of the two sub-ranges matches the `ExecuteTimingMode` of the
`Execute()` call it was declared inside (see 3.1's new
`ExecuteTimingMode` enum) - the FIRST time `RenderGraph::Execute()` ever
sees that exact pass name within that regime (a small, persistent
`std::vector<std::pair<const char*, std::uint32_t>>` name->slot table per
regime, compared by pointer first then `strcmp()` as a fallback -
mirroring `FrameProfiler::RecordCpuScope()`'s own already-established
"string literal, compared by pointer/strcmp" CPU-scope convention exactly,
applied here to GPU passes for the first time) and reused every subsequent
frame for that same name. **v2: a pass's `ExecuteTimingMode` is fixed for
its entire lifetime in this engine - a pass declared inside the offscreen
`Execute()` call is ALWAYS read back synchronously (immediately, in the
SAME call, after this regime's own fence wait - exactly mirroring
`GpuTimingService::ReadOffscreenResultNow()` today), and a pass declared
inside the Present `Execute()` call is ALWAYS read back via the SAME
per-frame-in-flight "ever written" warm-up flag `GpuTimingService::
ReadPresentResultIfAvailable()`/`MarkPresentSlotWritten()` already
implements today - nothing about generalizing the slot COUNT changes
either readback discipline itself.** A frame that declares MORE distinct
pass names (within either regime) than that regime's own budget degrades
gracefully - the newest, unassignable pass name's GPU timing is reported as
`GpuTimingSample::Status::Absent` forever (never a crash, never silently
overwriting another pass's slot) - documented as a known, accepted MVP
limit, revisited only if a real project ever approaches it.

### 3.3 - Generalizing `DrawStats`/`Profiling::GpuPass` similarly

`Profiling::GpuPass` (`src/Profiling/ProfilingTypes.h`) stays a fixed,
`GameView`/`SceneView`/`Present` three-value enum for THIS campaign's MVP -
see Step 4 below for why generalizing the PROFILER's own public/Editor-facing
data model is explicitly out of scope here. `RenderGraph::LastKnownStatsFor()`
instead exposes a per-pass-NAME table internally, and Phase 7's migration
is where the THREE specific, already-existing `Profiling::GpuPass` values
get fed from whichever THREE specific pass names Phase 7's real
`GameViewPass`/`SceneViewPass`/`PresentPass` happen to be called - i.e. the
render graph itself is capable of reporting stats for an arbitrary number
of passes, but only three of them are, for now, ALSO surfaced through the
existing fixed-enum Profiler pipeline. A future Phase (outside this
campaign's 9 phases, noted in Phase 9) can widen `Profiling::GpuPass` itself
once a real feature (e.g. a shadow pass) needs its OWN dedicated Profiler
line.

### 3.4 - Tests

- `tests/Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp` (Tier 1) -
  the pure name->slot assignment/reuse/overflow-degradation logic from 3.2,
  extracted into its own small, Vulkan-free function/class specifically so
  it is testable without a live `VkQueryPool` (mirroring
  `ResolveGpuTimingStatus()`'s own extraction precedent from Phase 4 of the
  GPU-timestamp campaign) - assign, reuse-same-name, overflow returns a
  sentinel "no slot" value, never crashes/throws. **v2, new: a per-regime
  test confirming a pass name assigned a slot in the SYNCHRONOUS regime's
  sub-range and a differently-scoped, same-spelled pass name in the
  PIPELINED regime's sub-range never collide/alias one another - the two
  sub-ranges must be genuinely independent, not merely non-overlapping by
  accident.**
- `RenderGraph` itself (the class combining live Vulkan calls) has NO
  automated test - Tier 2, same accepted bucket as `FramePresenter`/
  `GpuResourceFactory` today. Manual verification is: build a tiny,
  throwaway two-pass graph (a color-write pass feeding a shader-read pass)
  behind a temporary, code-only feature toggle, run it against a real
  device with validation layers on, and confirm zero validation errors AND
  correct visual output, BEFORE Phase 7 ever touches `Application::Run()`.
  **v2: also manually verify BOTH `ExecuteTimingMode` regimes at least
  once each - a synchronous throwaway graph AND a pipelined throwaway graph
  (even if the pipelined one is a trivial single-pass "clear to a color and
  present" test) - so the split isn't only ever exercised, for the first
  time, against the real, already-load-bearing Phase 7 migration.**

### 3.5 - Cycle-detection robustness (new in v2)

Since a `RenderGraph::Execute()` call recompiles its ENTIRE graph from
scratch every single call (this phase's own unchanged Step 4 below), a
pass-declaration bug that introduces a genuine dependency cycle
(`RenderGraphCompiler::Compile()` throwing `std::runtime_error` - see
`RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md`) would otherwise throw
EVERY SINGLE FRAME once introduced, not once - a much worse failure mode
than a one-time crash, and one v1 never addressed. `RenderGraph::Execute()`
itself does not swallow this exception (a cycle is a genuine programming
error that must never be silently "handled" into some arbitrary,
unspecified pass order), but Phase 7's `Application::Run()` call sites
(see Phase 7 v2) are expected to catch it ONCE, at the top of the frame
loop, log it loudly (via whatever this engine's existing fatal-error/
assert convention is), and in a debug build immediately re-throw/abort so
it is impossible to miss during development - never silently skip that
frame's rendering and quietly continue as if nothing happened, which would
turn a loud, obvious bug into a confusing "why is the screen sometimes
blank" report days later.

## Step 4: What We Will NOT Do (Focus)

- We will **not** widen `Profiling::GpuPass` (`ProfilingTypes.h`) to an
  arbitrary/dynamic pass-name set in this phase - that is a genuinely
  separate, Editor/Profiler-panel-facing data-model change with its own
  blast radius (`FrameGraphData.h`, `ProfilerPanelData.h`,
  `Panels/ProfilerPanel.cpp` all currently assume exactly three named
  passes) and is EXPLICITLY out of scope for this whole nine-phase
  campaign - see Phase 9's backlog. `RenderGraph`'s OWN internal
  `LastKnownStatsFor()` table is deliberately more general than what gets
  surfaced through the existing Profiler pipeline today; that gap is
  intentional, not an oversight.
- We will **not** let `RenderGraph::Execute()` own or create the
  `VkCommandBuffer` it records into - it is handed one (from
  `FramePresenter`, post-Phase-7) exactly like `FrameRecorder::RecordFrame()`
  already receives one today; `RenderGraph` never touches command-pool
  allocation, fences, semaphores, or `vkQueueSubmit`/`vkQueuePresentKHR` -
  those stay `FramePresenter`'s job, unchanged, forever.
- We will **not** support recompiling/re-executing a graph MORE than once
  per `Execute()` call, and we will **not** support calling `Execute()`
  re-entrantly/nested. **v2, corrected: exactly TWO `Execute()` calls
  happen per rendered frame (see V2 Revision Note 1) - one per submission
  regime - never one shared call spanning both, and never more than one
  call per regime per frame either.** This is a firmer, more specific
  version of v1's own vaguer "once per logical frame's worth of GPU work"
  wording, which this iteration found to be exactly the ambiguity that let
  Phase 7 v1's code sample drift into a single, merged call.
- We will **not** attempt to make pass EXECUTE callbacks themselves
  Tier-1-testable in general (they issue real `vkCmdDraw`/
  `vkCmdBindPipeline` calls, inherently Tier 2) - only the SCHEDULING/
  BARRIER/SLOT-ASSIGNMENT logic around them is held to the Tier-1 bar.
- We will **not** merge the synchronous-offscreen and pipelined-present
  `ExecuteTimingMode` regimes into one uniform readback discipline "to
  simplify the API" - see V2 Revision Note 2. The asymmetry exists in the
  underlying hardware/API model this engine is built on (a synchronous
  wait vs. a pipelined, frames-in-flight submission are genuinely
  different things with genuinely different performance characteristics),
  and pretending otherwise in the render graph's own abstraction would
  either be a lie (silently blocking somewhere it doesn't today) or a bug
  (reading timing data before it's actually ready).

## Step 5: Their Role (What does this mean for you?)

- If you are implementing this phase, build it with a THROWAWAY two-pass
  test scene FIRST (e.g. render a triangle into an offscreen color target,
  then a second pass that fullscreen-blits/samples it into the swapchain)
  before touching anything Phase 7 will eventually need - proving
  `RenderGraph` works end-to-end on a deliberately trivial, NEW scenario is
  much lower-risk than trying to validate it for the first time against
  this engine's real, already-load-bearing Game/Scene/Present pipeline.
  **Build and manually verify BOTH `ExecuteTimingMode` regimes this way,
  not just one - see Step 3.4's v2 addition.**
- Keep `PassContext`'s surface area as small as Phase 7 actually turns out
  to need, and grow it only when a real pass author hits a wall - it is
  far easier to ADD a method to `PassContext` later than to have
  over-designed it now against imagined future passes that never
  materialize the way you guessed.
- Before writing a single line of this phase's code, re-read this
  document's own V2 Revision Note 1 and Step 3.1's `ExecuteTimingMode`
  enum until the "why two calls, not one" reasoning is completely clear -
  this was the single largest gap this iteration's review found, and it is
  far cheaper to internalize now than to discover midway through
  implementing Phase 7's rewiring that the single-`Execute()`-call model
  doesn't actually fit how `FramePresenter` works.
- Once this phase is manually verified (validation-clean, visually
  correct, on the throwaway two-pass scene, in BOTH regimes), write
  `RENDERGRAPH_PHASE6_COMPLETION_REPORT.md` describing EXACTLY what was
  verified and how - this becomes the evidence Phase 7's own review can
  point back to when arguing "the underlying machinery is already proven,
  this is 'just' rewiring."
