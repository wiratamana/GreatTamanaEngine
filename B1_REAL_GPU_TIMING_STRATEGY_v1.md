# B1_REAL_GPU_TIMING_STRATEGY_v1.md

## B.1 — Real GPU Timing for Render Graph Passes

### Where this came from

`RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`, Section B ("Explicitly
open per the campaign's own completion report"), item **B.1**, states this
plainly:

> Real GPU timing is still not wired up for render-graph passes.
> `RenderGraph::LastKnownStatsFor()`'s `timing` field is `Absent` for every
> single pass, forever, as of Phase 8. ... This was named the **single
> highest-priority follow-up** by three separate completion reports in a
> row (Phase 6, 7, 8) and is still untouched.

That same document's Section D ("Prioritized reading... for the GPU-driven-
rendering milestone"), item 5, sharpens WHY this specific gap matters more
than a cosmetic nice-to-have:

> B.1 (GPU timing not wired up) should be picked up alongside this
> milestone, not after it — you cannot honestly evaluate whether GPU-driven
> culling is actually faster without real, per-pass GPU timing for the new
> compute pass AND the indirect draw pass it feeds.

This document is the dedicated implementation strategy for closing that one
gap — nothing more, nothing less. It follows this codebase's own established
"Phase strategy document" shape (see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`
through `RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md`,
`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`): a five-step narrative (Goal /
Situation / Plan / Not-Doing / Their-Role), written against the ACTUAL current
source, not a hypothetical one — every file, class, and method named below was
read directly out of this repository as it exists today.

---

## Step 1: The Goal (Where are we going?)

When this work is done, **every pass executed by `gte::rg::RenderGraph` —
`"GameView"`, `"SceneView"`, `"Present"` today, and any future pass a later
milestone declares (a GPU-driven culling compute pass, an outline-highlight
post-process, ...) — reports genuine, driver-measured GPU execution time**
through `RenderGraph::LastKnownStatsFor(passName).timing`, using the exact
same tri-state contract every other real-data producer in this engine already
honors: `GpuTimingSample::Status::Present` with a real millisecond value when
the pass actually ran and was actually timed this call, `Status::Absent` when
nothing has been recorded yet (a pass that hasn't run, or capture is
disabled), and `Status::Unsupported` on a device/build that can never produce
this measurement — **never a fabricated `0.00 ms`**, mirroring
`AGENTS.md`'s own "Profiling" rule word for word.

Concretely, three externally-observable things change:

1. The Editor's **"Render Graph" panel** (`Panels/RenderGraphPanel.cpp`)
   stops permanently printing `N/A` in its "GPU Time" column for every
   surviving pass, in both the "Offscreen Regime" and "Pipelined Regime"
   sections, and starts printing real numbers (see
   `FormatGpuTiming()`/`BuildPassRow()` in that file — the DISPLAY code
   already fully supports this; it has simply never been fed a non-`Absent`
   `GpuTimingSample` because nothing upstream ever produces one).
2. The Editor's **"Profiler" panel** (`Panels/ProfilerPanel.cpp`) — which
   today reads GPU timing from a completely SEPARATE, effectively orphaned
   system (see Step 2.4 below) — is switched to read from the exact same
   render-graph-owned source of truth the "Render Graph" panel now uses, so
   the two panels can never again silently disagree about "how long did
   GameView actually take on the GPU this frame".
3. **A scaffold now exists that scales to an arbitrary, growing number of
   distinctly-named passes**, not a hardcoded enum of exactly three — this is
   the concrete, load-bearing precondition the GPU-driven-rendering milestone
   (a real compute culling pass feeding a real indirect-draw graphics pass,
   both new pass names the graph has never seen before) needs in order to be
   evaluated honestly rather than on faith.

"Done" is a single, unambiguous acceptance bar: select any real frame of this
engine running with the Editor's "Render Graph" and "Profiler" panels both
open, and every surviving pass in both panels shows a real, non-zero,
plausible millisecond figure that is IDENTICAL between the two panels for the
same pass in the same frame — with nothing anywhere in the pipeline ever
inventing a `0.00 ms` for a pass that didn't actually get measured.

---

## Step 2: The Situation / The Problem (Where are we now?)

### 2.1 The exact dead end, quoted verbatim

`RenderGraph::ExecuteCompiledGraph()` (`RenderGraph.cpp`) already computes a
per-pass name→slot assignment on every single call, for both regimes:

```cpp
// Reserved for Phase 7's real per-pass timestamp query wiring (see
// this class's own "GPU TIMING NOTE") - exercising the name-slot
// assignment/reuse logic on every real Execute() call from day one,
// even though nothing consumes the resulting slot index yet.
(void)timingSlots.AssignOrGetSlot(pass.name);
```

...and then, at the bottom of the very same pass loop, unconditionally
records a permanently-empty `GpuTimingSample`:

```cpp
// Absent GpuTimingSample - see this class's own "GPU TIMING NOTE".
RecordStatsFor(pass.name, PassGpuStats{ passDrawStats, GpuTimingSample{} });
```

`GpuTimingSample{}` default-constructs to `Status::Absent` (see
`GpuTiming.h`). The slot index `AssignOrGetSlot()` just computed is thrown
away with `(void)` on the very same line it's produced. This is not a bug in
the sense of "wrong behavior" — `RenderGraph.h`'s own "GPU TIMING NOTE" is
explicit that this was a deliberate, documented scope decision for Phase 6/7
(don't touch already-shipped `GpuTimingService` before a real consumer
exists) — but it is a deliberately-left-open gap, and closing it is this
document's entire job.

### 2.2 What already works and must not be broken

Three genuinely proven, Tier-1-tested, reusable pieces already exist and
should be mined, not reinvented:

- **`RenderGraphNameSlotTable`** (`RenderGraphNameSlotTable.h`) — pure,
  Vulkan-free name→slot assignment with a fixed per-regime budget
  (`kSynchronousTimingSlotBudget = 16`, `kPipelinedTimingSlotBudget = 8` —
  see `RenderGraph.h`), graceful `kNoNameSlot` degradation on overflow, and
  `AssignedCount()`. It is ALREADY constructed as two independent instances
  (`m_synchronousTimingSlots`, `m_pipelinedTimingSlots`) and exercised on
  every real `Execute()` call, exactly as designed. It currently has **no
  reverse lookup** (slot index → name) — the one small gap in its API this
  work needs to close.
- **`GpuTiming.h`** — pure, Vulkan-header-free math this new work can (and
  should) reuse VERBATIM, with zero changes: `ConvertTimestampDeltaToMilliseconds()`
  (tick-delta → ms, wraparound-safe via `validBits`), `ResolveGpuTimingStatus()`
  (the tri-state priority decision: `Unsupported` always wins over `Absent`
  wins over `Present`), and `InterpretTimestampCapability()`. None of this is
  tied to the fixed `GpuTimingSlot` enum in any way that would prevent reuse
  for a name-keyed pool instead.
- **`VulkanQueryPool`**/**`GpuTimingService`** (`Vulkan/VulkanQueryPool.h/.cpp`,
  `GpuTimingService.h/.cpp`) — a proven, Tier-2-manually-verified, real
  working `vkCmdResetQueryPool`/`vkCmdWriteTimestamp2`/`vkGetQueryPoolResults`
  implementation, currently hardcoded around the fixed `GpuTimingSlot` enum
  (`Offscreen0`/`Offscreen1`/`SwapchainPresent`) and a fixed slot layout. Its
  PATTERN (how it resets, writes, and reads back queries; how it gates
  itself behind `GTE_ENABLE_PROFILER` + `SetCaptureEnabled()`) is exactly
  what this work should imitate — but see 2.4 below for why this work should
  NOT simply extend this class in place.

### 2.3 The two execution regimes need two different readback strategies

`RenderGraph::Execute()` is called exactly twice per real engine frame (see
`RenderGraph.h`'s own top-of-file comment and `ExecuteTimingMode`):

- **`SynchronousImmediateReadback`** — the offscreen Game view + Scene view
  regime. `Renderer::EndOffscreenRenderGraphRecording()` submits and then
  **fully blocks until the GPU finishes**, before `Application::Run()` ever
  reads `LastKnownStatsFor("GameView")`/`"SceneView"`. This means, by
  construction, that ANY query written during this call is guaranteed
  already resolved on the GPU by the time the caller could plausibly want to
  read it back — there is no pipelining to reason about here at all.
- **`PipelinedDeferredReadback`** — the swapchain Present regime. This is
  deliberately NON-blocking (`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own
  "V2 Revision Note 2": merging the two regimes into one submission model
  would force Present to become synchronous too — "a real frame-pacing
  regression"). A query written during frame `F`'s `Execute()` call is only
  SAFE to read back once frame `F`'s own command buffer/fence is known to
  have signaled — which, given this engine's existing multiple-frames-in-
  flight swapchain model, only happens `kGpuTimingFramesInFlight` frames
  later (that constant already exists — see `TESTING.md`'s own reference to
  "the fixed `GpuTimingSlot`/`kGpuTimingSlotCount`/`kGpuTimingFramesInFlight`
  layout" in `GpuTiming.h`).

A single, naive "write timestamps, read them back immediately" strategy is
therefore WRONG for one of these two regimes. Phase 4C/4D already solved
exactly this same two-regime problem once before, for the OLD fixed-enum
system (4C did the easy synchronous half first, 4D did the harder pipelined
half second, using "a per-slot 'has this ever been written' warm-up flag,
not a frame-count heuristic" — see `AGENTS.md`'s "Profiling" section). This
plan deliberately repeats that same two-milestone shape, generalized to a
name-keyed pool instead of a 3-value enum.

### 2.4 A discovery worth confirming before writing a single line of code

Reading `Application.cpp`, `Renderer.cpp`, and `RenderPasses.cpp` closely
together surfaces something worth stating plainly, because it changes how
aggressively this work should touch `GpuTimingService`:

- `Application::Run()` no longer calls `Renderer::RenderOffscreen()` for
  Game/Scene at all — it calls `BeginOffscreenRenderGraphRecording()` →
  `m_renderGraph.Execute(...)` → `EndOffscreenRenderGraphRecording()`
  instead (Phase 7's whole point).
- `Application::Run()` no longer calls `Renderer::Present()` (the legacy,
  `FrameRecorder`-based one) either — it calls `PresentViaRenderGraph()`.
- The only remaining production callers of `Renderer::RenderOffscreen()`
  with a REAL `GpuTimingSlot` argument would have to be
  `AssetPreviewMesh.cpp`/`BoneViewerWindow.cpp` — but per `Renderer.h`'s own
  doc comment on that parameter, both of those callers deliberately pass
  `std::nullopt` ("opt OUT... must never silently share a query slot with...
  'Game View'/'Scene View'").

Taken together, this suggests **`GpuTimingSlot::Offscreen0`/`Offscreen1`/
`SwapchainPresent` may no longer be reachable with a real, non-`nullopt`
value from ANY production call site any more** — i.e. `GpuTimingService`'s
whole reason for existing may already be quietly dead code, orphaned by
Phase 7's own migration, and simply never noticed because it still compiles,
still gets constructed, and still gets `SetGpuTimingCaptureEnabled()` called
on it every frame (`Application::Run()`'s first line of the loop body).

**This must be CONFIRMED, not assumed**, with a full-repository grep for
`GpuTimingSlot::` before any design decision below is finalized (Step 3.1
makes this the literal first action item) — the files attached to this
analysis are a large but not necessarily complete slice of the tree. If
confirmed true, it substantially strengthens the case (Step 3.9) for a
name-keyed system living directly on `RenderGraph` rather than being bolted
onto `GpuTimingService`'s existing, possibly-already-obsolete fixed-enum
shape.

---

## Step 3: The Plan (How will we get there?)

### 3.1 First action: re-verify the assumption in 2.4, and re-read `GpuTiming.h`/`VulkanQueryPool.h` in full

Before any code changes: grep the whole tree for `GpuTimingSlot::` and for
`RenderOffscreen(` / `.Present(` (the legacy, non-render-graph overloads) to
confirm exactly who, if anyone, still supplies a real (non-`nullopt`)
`GpuTimingSlot` today. Also re-read `GpuTiming.h`'s exact current field names
or `kGpuTimingSlotCount`/`kGpuTimingFramesInFlight`/`PresentTimestampSlotBase()`,
and `VulkanQueryPool.h`'s exact constructor signature — this plan describes
their SHAPE from documentation/usage evidence, not their literal source, and
the concrete diff must match reality exactly, not this document's
paraphrase of it.

### 3.2 Give `RenderGraphNameSlotTable` a reverse lookup

One small, pure addition, fully in the spirit of its existing API:

```cpp
// Returns the name previously assigned to `slot` (via AssignOrGetSlot()),
// or nullptr if `slot` is out of range / never assigned. The exact inverse
// of AssignOrGetSlot() - needed by RenderGraph's timing readback code to
// turn "slot 3 in this regime's pool just resolved" back into "that was
// the 'SceneView' pass" without RenderGraph having to keep its own,
// second, parallel name<->slot table.
const char* NameAtSlot(std::int32_t slot) const noexcept
{
    if (slot < 0 || static_cast<std::size_t>(slot) >= m_names.size()) {
        return nullptr;
    }
    return m_names[static_cast<std::size_t>(slot)];
}
```

Add a matching Tier-1 test to whatever existing
`tests/Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp` file already
covers `AssignOrGetSlot()`/`kNoNameSlot` — this is pure, deterministic logic
with zero Vulkan dependency, exactly the kind of change `AGENTS.md`'s
"Testability & Regression Safety" section requires a matching test for in
the same change.

### 3.3 A new, dedicated, generalized timestamp query pool — do NOT extend `GpuTimingService` in place

Per 2.4's finding, the safest and cleanest design is a **brand-new, small
class** — `RenderGraphTimestampPool` (new files,
`src/Renderer/RenderGraph/RenderGraphTimestampPool.h/.cpp`) — that:

- Owns exactly two real `VkQueryPool`s (or two `VulkanQueryPool` instances,
  if that class's constructor can be given an explicit slot-count parameter
  — see 3.1's re-read requirement; if it cannot be generalized cheaply,
  build a second, sibling thin wrapper next to it rather than forcing
  `GpuTimingService`'s own shipped constructor to change shape):
  - **Synchronous pool**: `kSynchronousTimingSlotBudget * 2` queries (one
    begin + one end per name slot), single-buffered — see 3.6 for why no
    frame-in-flight multiplying is needed here.
  - **Pipelined pool**: `kPipelinedTimingSlotBudget * 2 * kGpuTimingFramesInFlight`
    queries — reusing the EXISTING `kGpuTimingFramesInFlight` constant from
    `GpuTiming.h` rather than inventing a second, possibly-drifting
    frames-in-flight assumption elsewhere in the engine.
- Is constructed once, inside `RenderGraph`'s own constructor, using
  `Renderer::GetVulkanContextInfo()` to obtain the `VkDevice` it needs — this
  requires one small, additive change to `Renderer::VulkanContextInfo` (see
  3.4).
- Provides a small, explicit API `RenderGraph.cpp`'s pass loop and finalize
  methods drive directly:
  ```cpp
  void ResetRange(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex);
  void WriteBegin(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t slot);
  void WriteEnd(VkCommandBuffer cmd, bool pipelined, std::uint32_t bufferIndex, std::int32_t slot);
  // Returns raw begin/end tick values via vkGetQueryPoolResults - caller
  // (RenderGraph) is responsible for calling ConvertTimestampDeltaToMilliseconds()
  // and ResolveGpuTimingStatus() itself, exactly as GpuTimingService's own
  // Read* methods already do for the old fixed-enum system.
  struct RawTicks { std::uint64_t begin = 0; std::uint64_t end = 0; };
  RawTicks ReadBack(bool pipelined, std::uint32_t bufferIndex, std::int32_t slot) const;
  ```
- Folds `GTE_ENABLE_PROFILER` into its own capability resolution internally
  (see 3.9), so every call site above can be a plain runtime check with no
  `#ifdef` scattered through `RenderGraph.cpp` — mirroring `GpuTimingService`'s
  own documented precedent exactly ("`GTE_ENABLE_PROFILER` forces the
  effective capability to `unsupported`, so a `GTE_ENABLE_PROFILER=OFF`
  build never creates a `VkQueryPool` at all").

`GpuTimingService`/`VulkanQueryPool`/`GpuTimingSlot` are **not touched** by
this work at all (see Step 4) — `AssetPreviewMesh`/`BoneViewerWindow` keep
working exactly as before, completely unaffected.

### 3.4 Threading device/capability info into `RenderGraph`

`RenderGraph`'s constructor today is `RenderGraph(Renderer& renderer) :
m_resourcePool(renderer) {}` — it never keeps a `Renderer&`/`VkDevice`
itself. Extend `Renderer::VulkanContextInfo` (`Renderer.h`) with one new
field:

```cpp
struct VulkanContextInfo {
    // ...existing fields unchanged...
    GpuTimestampCapability timestampCapability; // from GpuTiming.h - already queried once by VulkanDevice::TimestampCapability()
};
```

populated in `Renderer::GetVulkanContextInfo()` from
`m_device.TimestampCapability()` (the exact same call `GpuTimingService`'s
own construction already makes today — querying it a second, independent
time is cheap and side-effect-free). `RenderGraph`'s constructor then calls
`renderer.GetVulkanContextInfo()` once, and builds its own
`RenderGraphTimestampPool` from the `device`/`timestampCapability` it gets
back — no new, parallel accessor method needed on `Renderer` at all.

### 3.5 Where timestamps actually get written — the pass loop in `RenderGraph::ExecuteCompiledGraph()`

Directly around the existing dynamic-rendering bracket:

```cpp
for (const PassHandle& passHandle : compiled.executionOrder) {
    PassRecord& pass = input.passes[passHandle.index];

    // ...existing barrier application (unchanged)...

    const std::int32_t slot = timingSlots.AssignOrGetSlot(pass.name);

    // ...existing hasColorWrite/hasDepthWrite detection (unchanged)...

    if (slot != kNoNameSlot) {
        m_timestampPool.WriteBegin(cmd, isPipelined, bufferIndex, slot);
    }

    bool didBeginRendering = false;
    if (hasColorWrite) {
        // ...existing vkCmdBeginRendering/.../viewport/scissor (unchanged)...
        didBeginRendering = true;
    }
    if (pass.execute) {
        pass.execute(ctx);
    }
    if (didBeginRendering) {
        vkCmdEndRendering(cmd);
    }

    if (slot != kNoNameSlot) {
        m_timestampPool.WriteEnd(cmd, isPipelined, bufferIndex, slot);
    }

    UpdateDrawStatsFor(pass.name, passDrawStats); // see 3.8 - no longer RecordStatsFor()
}
```

Design decisions made explicit here, matching this codebase's own habit of
documenting every judgment call inline:

- The BEGIN timestamp is written AFTER this pass's own barriers have already
  been recorded, so any GPU stall caused by waiting on THIS pass's own
  dependency transitions is attributed to THIS pass, not misleadingly folded
  into whichever pass happens to run immediately before it.
- A pass with no color write (a future transfer-only/compute-only pass —
  see `RenderGraph.h`'s own existing comment on this case) still gets a
  begin/end pair bracketing `pass.execute(ctx)` directly — timing a pass
  never depends on whether it happens to use the dynamic-rendering bracket.
- A pass whose name overflowed its regime's slot budget (`slot ==
  kNoNameSlot`) simply never gets a timestamp write at all — its
  `GpuTimingSample` stays `Absent` forever, exactly the same graceful
  degradation `RenderGraphNameSlotTable` already documents for this case.

### 3.6 Reading results back — the synchronous regime (simple case)

Because `Renderer::EndOffscreenRenderGraphRecording()` fully fence-waits
before returning, the synchronous pool can be **single-buffered** — reset
once per call, written once per call, and is 100% safe to read back
IMMEDIATELY after that fence wait, THIS SAME FRAME, with zero staleness.
This requires one new public method on `RenderGraph`:

```cpp
// Call exactly once, immediately after Renderer::EndOffscreenRenderGraphRecording()
// returns (i.e. after its fence wait has completed) - reads back every
// timestamp pair written during the immediately-preceding
// SynchronousImmediateReadback Execute() call, converts each to
// milliseconds, and merges the result into m_lastKnownStats via
// UpdateTimingFor() (see 3.8) - never clobbering that same entry's
// drawStats, which was already written synchronously during Execute()
// itself. A no-op (and safe to skip calling) on a frame where the
// offscreen regime didn't run at all this frame (both Game/Scene panels
// hidden) - the caller simply never calls it in that case (see
// Application::Run()'s existing `if (gameTarget != nullptr || sceneTarget
// != nullptr)` guard).
void RenderGraph::FinalizeSynchronousGpuTiming()
{
    for (std::uint32_t s = 0; s < m_synchronousTimingSlots.AssignedCount(); ++s) {
        const char* name = m_synchronousTimingSlots.NameAtSlot(static_cast<std::int32_t>(s));
        if (name == nullptr) continue;
        const auto raw = m_timestampPool.ReadBack(/*pipelined=*/false, /*bufferIndex=*/0, static_cast<std::int32_t>(s));
        const GpuTimingSample sample = ResolveAndConvert(raw); // ConvertTimestampDeltaToMilliseconds() + ResolveGpuTimingStatus()
        UpdateTimingFor(name, sample);
    }
}
```

`Application::Run()` gains exactly one new line, right after the existing
`m_renderer.EndOffscreenRenderGraphRecording();` call and BEFORE the existing
`m_renderGraph.LastKnownStatsFor("GameView")` reads:

```cpp
m_renderer.EndOffscreenRenderGraphRecording();
m_renderGraph.FinalizeSynchronousGpuTiming(); // <-- new
```

The pool's reset happens at the TOP of the NEXT `SynchronousImmediateReadback`
`Execute()` call, right alongside the already-existing `m_resourcePool.BeginFrame();`
call (both are the "first thing this regime's call does each frame" — the
same convention `RenderGraph.h`'s own top comment already documents for
`BeginFrame()`). This is safe because `FinalizeSynchronousGpuTiming()` for
frame `F` always runs, and always completes, strictly before frame `F+1`'s
`Execute()` call even begins recording (this engine is single-threaded — see
`AGENTS.md`'s "Entity-Component-System" section) — there is no possibility of
resetting a query whose result hasn't been consumed yet.

### 3.7 Reading results back — the pipelined regime (the harder case)

`RenderGraph` keeps its own small internal counter,
`std::uint32_t m_pipelinedFrameCounter = 0;`, incremented once per
`PipelinedDeferredReadback` `Execute()` call that actually runs (never
incremented on a frame where `PresentViaRenderGraph()` skipped calling
`Execute()` entirely — minimized window, pending resize, just-recreated
swapchain — since no query was written that "frame" either). This avoids
threading a frame-in-flight index through `Renderer`/`FramePresenter`'s
existing public API at all — `RenderGraph` derives everything it needs from
its own call cadence:

```cpp
const std::uint32_t bufferIndex = m_pipelinedFrameCounter % kGpuTimingFramesInFlight;
```

At the very TOP of `ExecuteCompiledGraph()` when `timingMode ==
PipelinedDeferredReadback`, BEFORE the pass loop runs (and before this
call's own reset of `bufferIndex`'s slice):

```cpp
for (std::uint32_t s = 0; s < m_pipelinedTimingSlots.AssignedCount(); ++s) {
    if (!m_pipelinedHasWritten[s][bufferIndex]) {
        continue; // this exact slice has never been written yet this session - first kGpuTimingFramesInFlight frames, or capture was off
    }
    const char* name = m_pipelinedTimingSlots.NameAtSlot(static_cast<std::int32_t>(s));
    if (name == nullptr) continue;
    const auto raw = m_timestampPool.ReadBack(/*pipelined=*/true, bufferIndex, static_cast<std::int32_t>(s));
    UpdateTimingFor(name, ResolveAndConvert(raw));
}
m_timestampPool.ResetRange(cmd, /*pipelined=*/true, bufferIndex);
```

This is provably safe for the exact same reason Phase 4D's original
single-slot version was safe (see `AGENTS.md`'s "Profiling" section): by the
time `PresentViaRenderGraph()` is about to reuse the command buffer/fence
slot for `bufferIndex`, the engine's OWN existing swapchain frame-in-flight
synchronization has ALREADY guaranteed that slot's previous submission
finished — this read never needs (and must never add) a NEW GPU wait of its
own, mirroring this engine's existing, explicitly-protected rule: "no new
GPU wait was ever added anywhere purely to fetch a timing result sooner."

The `m_pipelinedHasWritten[slotBudget][kGpuTimingFramesInFlight]` flag array
(a plain fixed 2D array, no heap allocation — matching this engine's
"nothing in the per-frame hot path may allocate" convention from `AGENTS.md`'s
"Profiling" section) is what correctly handles the first
`kGpuTimingFramesInFlight` frames of a session (or any gap where capture was
off/the pass didn't exist yet) WITHOUT resorting to a fragile frame-count
heuristic — the exact same documented reasoning Phase 4D used for its own
single warm-up flag, now generalized to `slotBudget × framesInFlight` many
independent flags.

Then, after this readback-and-reset preamble, the pass loop proceeds exactly
as in 3.5, writing fresh begin/end timestamps into `bufferIndex`'s now-clean
slice and setting `m_pipelinedHasWritten[slot][bufferIndex] = true` for every
slot actually written. `m_pipelinedFrameCounter` is incremented once, at the
very end of this `Execute()` call.

### 3.8 Bridging into `PassGpuStats` without clobbering fresh data with stale data — the one genuinely load-bearing correctness point

`RenderGraph::RecordStatsFor()` today OVERWRITES a `NamedStats` entry's
WHOLE `PassGpuStats` (`drawStats` AND `timing` together) on every call. Once
`timing` is populated from a DIFFERENT call site (`FinalizeSynchronousGpuTiming()`/
the pipelined preamble above) than `drawStats` (still written inline, per
pass, during the normal loop), a single combined overwrite would let
whichever call happens to run last silently erase the other's already-correct
data. This is not a new problem this codebase has never seen before —
`Profiling::GpuPassSample` already hit exactly this issue and `AGENTS.md`'s
"Profiling" section documents the fix as a hard rule:

> `GpuPassSample` splits its tri-state into TWO INDEPENDENT fields,
> `timingStatus` and `countStatus` — never reintroduce a single combined
> `status`.

Apply the exact same discipline one level down, inside `RenderGraph`'s own
`NamedStats`/`PassGpuStats` bookkeeping:

```cpp
// Replaces the single RecordStatsFor() call inside the pass loop (3.5) -
// touches ONLY drawStats, never touches timing.
void RenderGraph::UpdateDrawStatsFor(const char* name, const DrawStats& drawStats);

// Called only from FinalizeSynchronousGpuTiming() (3.6) and the pipelined
// preamble (3.7) - touches ONLY timing, never touches drawStats.
void RenderGraph::UpdateTimingFor(const char* name, const GpuTimingSample& timing);
```

Both find-or-create the same `NamedStats` entry `RecordStatsFor()` already
does today (the find-or-create lookup logic itself is unchanged — only what
each caller is allowed to touch changes). `RecordStatsFor()` itself can
either be deleted outright or kept as a thin `private` helper both of the
above call into for the shared find-or-create step — a small refactor, not a
behavior change for `drawStats`.

### 3.9 Respecting the existing two-layer on/off gate

Two independent gates must both be honored, matching `AGENTS.md`'s
"Profiling" section precedent exactly:

- **Compile-time**: `RenderGraphTimestampPool`'s own capability resolution
  folds in `GTE_ENABLE_PROFILER` internally (constructing with an
  already-forced-`Unsupported` capability when that macro is off), so
  `RenderGraph.cpp`'s own call sites never need a single `#ifdef` — every
  `WriteBegin()`/`WriteEnd()`/`ReadBack()` call is naturally a no-op/returns
  a synthetic "unsupported" result under `GTE_ENABLE_PROFILER=OFF`, and a
  `GTE_ENABLE_PROFILER=OFF` build never actually creates either `VkQueryPool`
  at all — this is the exact same trick `GpuTimingService` already uses
  today, reapplied here.
- **Runtime**: `Application::Run()` already calls
  `m_renderer.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());`
  once per frame. Add ONE sibling call at that exact same call site:
  `m_renderGraph.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());`
  — `RenderGraph` is owned directly by `Application` (see `Application.h`),
  so this requires no new plumbing through `Renderer` at all, just a second,
  parallel one-line call next to the existing one.

### 3.10 Consolidating the Editor's two GPU-timing displays into one source of truth

Once 3.5–3.9 land and are verified (Step 3.12's Milestone 3), update
`Panels/ProfilerPanel.cpp`'s GPU Timing line for `GpuPass::GameView`/
`SceneView`/`Present` to read `renderGraph.LastKnownStatsFor("GameView"/
"SceneView"/"Present").timing` (the exact same call
`Application::Run()`/`Panels/RenderGraphPanel.cpp` already make) instead of
`Renderer::LastGpuTiming(GpuTimingSlot::...)`. This requires threading the
already-available `const rg::RenderGraph&` (already passed into
`IEditorLayer::BuildUI(Game&, Renderer&, const rg::RenderGraph&)` today) down
into `ProfilerPanel::Build()`, which currently only takes `EditorContext&`
— a small, mechanical signature change, not a redesign.

### 3.11 Test plan

- **Tier 1 (pure, no VkDevice)**: `RenderGraphNameSlotTable::NameAtSlot()`
  (3.2) gets a direct unit test. `ConvertTimestampDeltaToMilliseconds()`/
  `ResolveGpuTimingStatus()` are reused UNCHANGED from `GpuTiming.h` and need
  no new tests of their own (already covered by
  `tests/Renderer/GpuTimingTests.cpp`). If `RenderGraphTimestampPool` exposes
  any pure index-math helper (e.g. `ComputeQueryIndices(slot) ->
  {beginIndex, endIndex}`, or the pipelined `bufferIndex` computation), pull
  it out as a small free function and test it directly — matching this
  engine's own "no hashing, no cleverness on the hot path" testability
  philosophy.
- **Tier 2 (manual, real device, real validation layers)**: `RenderGraphTimestampPool`
  itself, and every `vkCmd*` call site in `RenderGraph.cpp`'s pass loop, fall
  into this engine's already-accepted "no automated GPU-backed test coverage
  yet" bucket (see `AGENTS.md`'s "Testability & Regression Safety" and
  `TESTING.md`'s own "Tier 2" note) — same bucket
  `VulkanQueryPool`/`GpuTimingService` themselves already live in. Verify with
  a debug build + Vulkan validation layers enabled, watching specifically
  for: a double-write without an intervening reset (a real, loud validation
  error if the buffering math in 3.6/3.7 is wrong), a read of a query that
  was never written (must degrade to `Absent`, never read garbage), and a
  reset issued while a dynamic-rendering scope is still active (must always
  happen strictly before `vkCmdBeginRendering`/after `vkCmdEndRendering`).

### 3.12 Incremental delivery — four milestones, mirroring Phase 4A–4D's own proven cadence

1. **Milestone 1 — synchronous regime only.** Land 3.2–3.6 + 3.9's
   compile-time gate. Verify: the "Render Graph" panel's Offscreen Regime
   section shows real GPU Time numbers for `"GameView"`/`"SceneView"`; the
   Pipelined Regime section (`"Present"`) still correctly shows `N/A`
   (nothing wired up for it yet — an honest, expected intermediate state,
   not a bug).
2. **Milestone 2 — pipelined regime.** Land 3.7 (the harder, frame-in-flight-
   aware half). Verify: `"Present"`'s GPU Time also becomes real; specifically
   verify the first `kGpuTimingFramesInFlight` frames of a session show
   `N/A` for `"Present"` and every frame after that shows a real number,
   proving the warm-up-flag logic is correct and no frame-count heuristic
   crept in.
3. **Milestone 3 — consolidation.** Land 3.10 (Profiler panel switched to the
   same source). Verify: with both "Render Graph" and "Profiler" panels open
   simultaneously, their GPU Time numbers for GameView/SceneView/Present are
   frame-for-frame identical, never merely "close".
4. **Milestone 4 — cleanup decision (not a deletion, a written finding).**
   Re-run 3.1's grep now that the render-graph-based system is proven and
   shipped. If `GpuTimingSlot::Offscreen0/Offscreen1/SwapchainPresent` are
   confirmed to have zero remaining real (non-`nullopt`) production callers,
   write that finding down as a NEW, explicit, separately-scoped future-todo
   entry (do not fold its removal into this work — see Step 4) so a later,
   dedicated cleanup pass can delete `GpuTimingService`'s fixed-enum machinery
   with full confidence and a paper trail explaining why it's safe.

---

## Step 4: What We Will NOT Do (Focus)

- **We will NOT modify, extend, or delete `GpuTimingService`, `VulkanQueryPool`,
  or the `GpuTimingSlot` enum as part of this work.** They stay exactly as
  they are, serving `AssetPreviewMesh`/`BoneViewerWindow`'s own independent,
  render-graph-external previews unaffected. Any consolidation/removal of
  that system is an explicit, separately-approved follow-up (Milestone 4),
  never an unplanned side effect of this one.
- **We will NOT add sub-pass-level (per-draw-call) GPU timing.** This is
  whole-pass begin/end timing only, exactly like the old fixed-enum system
  it replaces — timing an individual `vkCmdDraw`/`vkCmdDrawIndexed` inside a
  pass is a different, much larger feature (would need a query per draw, not
  per pass) and is out of scope here.
- **We will NOT touch multi-color-attachment (MRT) support.** `RenderGraph`'s
  existing single-color-attachment-per-pass MVP scope (see
  `RenderGraphBarrierPlanner.h`'s own documented limitation) is untouched;
  timing wraps whatever attachment shape a pass already has, nothing more.
- **We will NOT add async/multi-queue timing.** Every timestamp write in this
  plan targets the one graphics queue this engine already exclusively uses
  (see `RENDERGRAPH_FUTURE_TODO...md`'s own Section A item 2 — async compute
  is separately scoped, contingent on the GPU-driven-rendering milestone
  landing first).
- **We will NOT make `RenderGraphNameSlotTable`'s budgets dynamic/resizable.**
  A pass name that overflows its regime's fixed budget keeps degrading
  gracefully to `kNoNameSlot`/permanently-`Absent` timing, exactly as today
  — raising the fixed budget constants (`kSynchronousTimingSlotBudget`,
  `kPipelinedTimingSlotBudget`) if it's ever actually hit is a one-line,
  separately-considered change, not something this work needs to solve
  generically up front.
- **We will NOT build any compute-pass-specific timing infrastructure.** No
  compute shader exists in this engine yet (see the FUTURE_TODO document's
  Section C.1) — this work only needs to time whatever a pass's `execute`
  callback records against `cmd`, which is already agnostic to
  graphics-vs-compute; nothing here should be gold-plated in anticipation of
  compute passes specifically.
- **We will NOT treat "no automated Tier-2 GPU test fixture exists" as a
  blocker.** Per `AGENTS.md`'s own explicit rule, the absence of a headless
  `GpuTestFixture` must never slow down or stop feature work under
  `Renderer/Vulkan/`-adjacent code — manual verification with validation
  layers enabled (Step 3.11) is this work's accepted, sufficient bar, exactly
  as it was for Phases 4 and 6.
- **We will NOT retroactively rewrite Section A/C of the future-todo
  document's other items** (barrier batching, resource aliasing, bindless
  descriptors, GPU culling, ...) as part of landing this one. This document
  is scoped to B.1 alone.

---

## Step 5: Their Role (What does this mean for you?)

If you are the engineer picking this up:

1. **Start by confirming, not assuming.** Run the grep described in Step
   3.1 before writing any code. If it turns up a real, non-`nullopt`
   production caller of `GpuTimingSlot` this document didn't account for,
   stop and re-read that call site's own reasoning before proceeding — the
   design in Step 3 assumes it does not exist.
2. **Build in the four milestone order from Step 3.12, not all at once.**
   Milestone 1 (synchronous regime) is strictly easier and de-risks the
   harder pipelined-regime work in Milestone 2 — this mirrors exactly how
   Phase 4 itself was sequenced (4A → 4B → 4C → 4D), and for the same reason:
   each milestone is independently verifiable and independently shippable,
   so a mistake in the harder half never blocks the easier half from
   already being correct and observable.
3. **Every new file/class this plan introduces
   (`RenderGraphTimestampPool`) lives under `src/Renderer/RenderGraph/`**,
   never under `src/Editor/` — this is Renderer/RenderGraph-layer plumbing,
   exactly like `RenderGraphResourcePool`/`RenderGraphBarrierPlanner`
   already are, and exactly like `RenderGraphSnapshot.h`'s own header
   comment explains for why IT lives there instead of under `src/Editor/`
   despite existing purely to feed an Editor panel.
4. **Write a completion report when done**, mirroring every other phase's
   own `*_COMPLETION_REPORT.md` convention in this repository — in
   particular, explicitly state (a) which of the two panels were updated,
   (b) the exact manual verification performed (device/driver, validation
   layer output, number of frames observed with real vs. `N/A` timing), and
   (c) whether Milestone 4's grep confirmed `GpuTimingService`'s fixed-enum
   path is now fully dead code — that finding, either way, is valuable
   information for whoever eventually acts on it.
5. **When this lands, update `RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md`
   itself** — Section B item 1 should move from "still open" to "closed, see
   [this completion report]", and Section D's own prioritized reading should
   be re-checked: item 5 explicitly named this as a precondition worth doing
   "alongside", not after, the GPU-driven-rendering milestone — closing it
   first (or in lockstep) is exactly what that section already recommended.
6. **Nothing about this work should be visible to `Game`/gameplay code at
   all.** Every change described here lives entirely inside
   `Renderer`/`RenderGraph`/`Editor` — `Game.cpp`/`Game.h` and every ECS
   component are completely untouched, matching this engine's own Clean
   Architecture rule (`AGENTS.md`) that only the layers that already know
   about Vulkan/ImGui are allowed to grow new Vulkan/ImGui-adjacent
   capability.
