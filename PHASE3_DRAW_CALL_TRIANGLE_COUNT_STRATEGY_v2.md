# Phase 3 Strategy — Draw-Call and Triangle Counts (v2)

Status: PROPOSAL / PLANNING DOCUMENT — no implementation yet.

## Changelog from v1

v1 was reviewed before implementation, per this project's own "read it
again before writing code" discipline. Four changes came out of that
review, in order of importance:

1. **The counting design is now FUSED into the existing recording loop,
   not a separate pre-pass.** v1's Step 3.1/3.2 built a whole
   `std::vector<CountableDrawItem>` and called `CountDrawStats()` over it
   as an independent loop, run before the real per-item recording loop
   that decides `vkCmdDraw` vs. `vkCmdDrawIndexed`. That is two
   iterations over `m_drawQueue` per call, plus an extra heap allocation,
   and — more importantly — a structural correctness hazard: if a future
   edit ever adds a skip/validity branch to the real recording loop (e.g.
   "don't draw if a resolved handle is stale this frame"), the separate
   counting loop would silently keep counting an item that was never
   actually submitted to the GPU, unless someone remembered to mirror the
   new condition in both places by hand. This is exactly the "same logic
   hand-duplicated in two places" anti-pattern this codebase has
   deliberately refactored *away* from elsewhere (`MeshVertexPacking.h`,
   `MeshMaterialPartitioner.h`, `AnimationPoseEvaluator`'s own
   ordering-critical single-call-site rule in `AGENTS.md`) — v2 does not
   introduce a fresh instance of it. See Step 3.1/3.2 below.
2. **`TESTING.md` is now an explicit deliverable**, both for the new
   `tests/Renderer/DrawStatsTests.cpp` and to close a pre-existing gap:
   `TESTING.md` as it stands today does not mention `tests/Profiling/*`
   at all, even though `AGENTS.md` and this very document both reference
   that suite. See Step 3.8.
3. **Every exact source line number cited from v1 is now explicitly
   flagged as "re-verify at implementation time," not a frozen fact** —
   this is already a second iteration of this very document, so numbers
   quoted from a single read-through should never be trusted blindly by
   whoever implements this next. See Step 2's new opening note.
4. **Two small documented assumptions added**: draws are always
   `instanceCount == 1` (no instancing exists anywhere in the engine
   today), and the cost of counting when `GTE_ENABLE_PROFILER=OFF` is a
   deliberate, stated decision rather than something that just never came
   up. See Step 3.1 and Step 4.

Nothing else about v1's reasoning changed — the `timingStatus`/
`countStatus` split (Step 2.4) in particular was already correct and is
carried over unchanged, since it remains the single most important
correction this phase makes to shared, cross-phase infrastructure.

**Scope of this one document**: `PROFILER_STRATEGY_v2.md`'s own **Phase
3** — "Draw-call and triangle counts" — immediately following the
already-shipped Phase 2 ("Frame time graph data"). Nothing below revisits
Phase 0/1/2, and nothing below reaches ahead into Phase 4 (GPU timestamp
queries), Phase 5 (GPU memory history), Phase 6 (benchmark mode), or
Phase 7 (the Editor "Profiler" panel) beyond what's needed to keep this
phase's output consumable by them later with zero rework.

--------------------------------------------------------------------------
## Step 1: The Goal (Where are we going?)
--------------------------------------------------------------------------

### 1.1 What "done" looks like, concretely

When this phase is complete, every frame the engine renders will carry a
**real, measured** answer to exactly two questions, broken down **per
named GPU pass** (`GameView`, `SceneView`, `Present` — the same three
passes Phase 1's CPU scope timers already distinguish, see
`ProfilingTypes.h`'s `GpuPass` enum):

- **"How many `vkCmdDraw`/`vkCmdDrawIndexed` calls did this pass actually
  submit this frame?"**
- **"How many triangles did those calls actually draw, in total?"**

...sitting inside `FrameProfiler`'s per-frame ring buffer
(`FrameSample::gpuPasses[pass]`) the instant this phase lands — reachable
by a unit test asserting exact values, a throwaway diagnostic reading
`FrameProfiler::Instance().LastCompletedFrame()`, and transparently, with
zero further work, by `Profiling::FrameGraphData.h`'s already-shipped
`FrameGraphPoint::gpuPasses`.

No Editor panel is built in this phase (Phase 7). No GPU timestamp query
is added (Phase 4). This phase is **purely a CPU-side counting exercise**
over data the engine already fully possesses every frame.

### 1.2 Why this phase, why now, why this order

`PROFILER_IMPLEMENTATION_STATUS_v3.md` puts Phase 3 first, above Phase
5/6/7, with the justification: **"Self-contained, low-risk, cheap."**
Having re-read the current `FrameRecorder`/`FramePresenter`/`Renderer`
source, that verdict holds — **with one qualification this document
exists to surface**: the "low-risk, cheap" part is true of the counting
logic itself; a genuine, small design correction to `ProfilingTypes.h`
(Step 2.4/3.6) needs to happen *at the same time*, or this phase would
ship a subtly wrong number into a shared data structure two other phases
already build on.

### 1.3 Concrete deliverables

1. A new, tiny, always-compiled, **Vulkan-free**, Tier-1-testable pure
   accumulator that turns "one queued draw's shape" into an incremental
   `{drawCallCount, triangleCount}` contribution — called from **inside**
   the existing per-item recording loop, never from a separate pass over
   the same list (see Step 3.1/3.2 — this is the main change from v1).
2. `FrameRecorder::RecordFrame()` accumulating that value once per item,
   in the exact same loop iteration/branch that already issues
   `vkCmdDraw`/`vkCmdDrawIndexed` — **zero change to what gets recorded or
   how**, and now also **zero possibility of the count ever disagreeing
   with what was actually recorded**, since there is only one loop.
3. That accumulated total threaded back up through `FramePresenter`/
   `Renderer` to `Application::Run()` — the one place that already knows,
   by literal construction, which of the three named `GpuPass` values a
   given `RenderOffscreen()`/`Present()` call corresponds to.
4. A **corrected** `GpuPassSample` data model (`ProfilingTypes.h`) that can
   represent "counts are real, timing is not yet measured" without lying
   about the timing half.
5. Every "pass didn't run this frame" case continuing to report
   **absent**, never a fabricated zero.
6. Full Tier-1 test coverage for the new accumulator/data model, plus
   updates to every existing Phase 0–2 test that touches the data model
   being corrected (a known, bounded, fully enumerated list — Step 3.6).
7. `AGENTS.md`'s "Profiling" section, **and `TESTING.md`**, gaining
   documentation of this phase's new file/tests (v1 only updated
   `AGENTS.md` — see Step 3.8).

--------------------------------------------------------------------------
## Step 2: The Situation / The Problem (Where are we now?)
--------------------------------------------------------------------------

> **Re-verification note (new in v2):** every exact line number cited
> below is quoted from a single read-through of the current source tree,
> immediately before this document was written. This is already the
> second iteration of this document — treat every cited line number as a
> **pointer to go re-read**, not a guaranteed-still-accurate fact, before
> writing the corresponding code. The *shape* of each finding (what
> exists, what's missing) is far more likely to still be true than the
> exact line it lives on.

### 2.1 What already exists that we get to reuse (the good news)

- **The raw numbers are already sitting in exactly one place, in exactly
  the shape we need.** `FrameRecorder`'s private `DrawItem` struct
  (`FrameRecorder.h`) already carries `vertexCount`, `indexBuffer`
  (`VK_NULL_HANDLE` when the `Mesh` has none — see `Mesh::HasIndexBuffer()`),
  and `indexCount` for every queued draw, and
  `FrameRecorder::RecordFrame()`'s existing per-draw loop already iterates
  every one of them once, in order, to decide `vkCmdDraw` vs.
  `vkCmdDrawIndexed`. **This phase adds no new iteration and no new list**
  — it adds one small accumulation step directly inside the loop that
  already exists, at the exact point that branch is already being
  evaluated (see Step 3.1/3.2 for why this replaces v1's separate-pass
  design).
- **Every `Pipeline` this engine builds is unconditionally
  `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`** (`Pipeline.cpp`,
  `inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;`). So
  "triangle count" is always exactly `(indexed ? indexCount : vertexCount)
  / 3` — a fixed, universally-correct formula today, not an approximation.
  **Also worth stating explicitly (new in v2): every draw this engine
  issues today has `instanceCount == 1`** (no instancing exists anywhere
  in `FrameRecorder`/`Pipeline` yet) — the triangle-count formula above
  implicitly assumes this. Both assumptions get their own doc comment in
  the new code (Step 3.1) so a future change (a non-triangle-list
  pipeline variant, or real instancing) is an obvious, easy-to-spot
  one-line fix rather than a silently wrong count.
- **`FrameRecorder::RecordFrame()`'s queue-clearing contract already
  guarantees no double-counting.** `m_drawQueue` is cleared at
  `BeginFrame()` and again immediately after being recorded. Since
  `Game::Render()` is called once per visible target immediately before
  that target's own `RenderOffscreen()`/`Present()` call consumes the
  queue, a count taken **inside** `RecordFrame()`'s own existing loop, as
  each item is actually recorded, sees exactly — and only — what this one
  specific call is about to submit. No cross-contamination between the
  Game view's count and the Scene view's count is possible by
  construction.
- **`Application::Run()` already knows, by literal, hardcoded call-site
  position, which named `GpuPass` a given recording corresponds to** — the
  three `GTE_PROFILE_SCOPE(...)` call sites in `Application.cpp` are
  already individually associated with `"Renderer::RenderOffscreen(GameView)"`,
  `"Renderer::RenderOffscreen(SceneView)"`, and `"Renderer::Present"`.
  Phase 3 only needs those same three call sites to *also* report
  draw/triangle counts, tagged with the matching `GpuPass` enumerator.
- **The "pass didn't run this frame" tri-state already exists and already
  defaults correctly with zero extra code.** `FrameProfiler::BeginFrame()`
  resets the in-progress sample via `m_current = FrameSample{}`, and
  `FrameSample::gpuPasses`'s default-constructed entries are already
  `GpuSampleStatus::Absent`. As long as Phase 3's new reporting call is
  made only on the branch where a pass actually ran (mirroring
  `Application.cpp`'s own existing `if (gameTarget != nullptr)`/
  `if (sceneTarget != nullptr)` structure), a hidden Game/Scene panel's
  pass is automatically left at `Absent` for that frame.
- **`Renderer::RenderOffscreen()`'s public signature can gain a return
  value with zero source-level impact on its two other call sites**
  (`src/Editor/AssetPreviewMesh.cpp`, `src/Editor/BoneViewerWindow.cpp` —
  both already call it as a bare statement, discarding whatever it
  returns). Confirmed directly via a repository-wide search for
  `RenderOffscreen(` before writing this document — re-run that search at
  implementation time to make sure no new call site was added since.
- **Testing tier discipline already tells us exactly where the line falls
  here.** `FrameRecorder`/`FramePresenter`/`Renderer` are already, and
  remain, Tier 2. The new pure accumulator this phase introduces is
  squarely Tier 1 — plain integers/booleans in, plain integers out.

### 2.2 What is genuinely missing today (the actual gap)

- **Nothing anywhere sums per-draw vertex/index counts into a per-frame
  total.** `FrameRecorder::RecordFrame()`'s loop uses `item.vertexCount`/
  `item.indexCount` purely to issue the correct `vkCmdDraw`/
  `vkCmdDrawIndexed` call — the moment a draw is recorded, those numbers
  are discarded today.
- **`FrameProfiler::SetGpuPassSample()` has zero production callers as of
  right now** — every match is inside `tests/Profiling/FrameProfilerTests.cpp`/
  `FrameGraphDataTests.cpp`, constructing synthetic samples by hand. This
  is the first phase to ever call this API from real engine code.
- **`Renderer::Present()`/`RenderOffscreen()` currently return `void`** —
  no channel exists today for `FrameRecorder::RecordFrame()`'s per-call
  knowledge to reach back up to `Application::Run()`, the one place that
  knows how to label it.
- **`FramePresenter::Present()` can genuinely skip recording a frame
  entirely, three separate ways, and today's `void` return type makes that
  invisible to the caller** (re-verify against the actual current file at
  implementation time — see this section's opening note):
  - the window is minimized (pending width/height are non-positive) —
    nothing recorded this call;
  - a resize is still pending after being requested — same;
  - the swapchain needed rebuilding (`VK_ERROR_OUT_OF_DATE_KHR`) — same.
  Only past all three does `frameRecorder.RecordFrame(...)` actually run.
  **This distinction matters**: a minimized-window frame must report
  `Present`'s `GpuPass` as **absent** (the pass truly did not run), not as
  "ran with zero draws" — conflating them would misrepresent a minimized
  window as an unusually cheap, fully-executed present.
  `RenderOffscreen()`, by contrast, has no early-return branch at all — it
  always calls `frameRecorder.RecordFrame(...)` unconditionally. This
  asymmetry must be reflected in the two functions' respective new return
  types (Step 3.3), not papered over for superficial symmetry.
- **`GpuPassSample`'s current shape cannot represent what Phase 3 actually
  needs to report, without also lying about something else** — see 2.4.

### 2.3 The `recordExtra` scope boundary (a real, pre-existing limitation to document, not fix)

`AssetPreviewMesh.cpp`/`BoneViewerWindow.cpp`'s own preview meshes are
drawn via a `recordExtra` callback passed into `Renderer::RenderOffscreen()`,
never via `Renderer::Submit()`/`FrameRecorder::Submit()`. Their draws never
enter `m_drawQueue` at all — issued directly against the `VkCommandBuffer`
handed to `recordExtra`, entirely outside `FrameRecorder::RecordFrame()`'s
own counted loop.

**Consequence, stated explicitly**: this phase's counter will never see or
count Dear ImGui's own overlay geometry, the Inspector's live mesh
preview, or the Bone Viewer's preview + bone-gizmo lines. This is
consistent with, not a violation of, this phase's own goal — it's about
the engine's own scene geometry, the thing that actually ships in a
release build, not Editor-only debug chrome that doesn't exist at all once
`GTE_ENABLE_EDITOR=OFF`. Mirrors Phase 4's own already-accepted scope
refusal for secondary ImGui viewport windows. State this plainly in code
comments so nobody "fixes" this later by teaching `FrameRecorder` to peek
inside an opaque `std::function`.

### 2.4 The one real design gap this document exists to catch: `GpuPassSample`'s single combined status cannot represent "counts are real, timing is not"

`ProfilingTypes.h`'s `GpuPassSample`, as it exists today:

```cpp
struct GpuPassSample {
    GpuSampleStatus status = GpuSampleStatus::Absent;
    double milliseconds = 0.0;
    std::uint32_t drawCallCount = 0;
    std::uint32_t triangleCount = 0;
};
```

...has exactly one `status` field governing all three of
`milliseconds`/`drawCallCount`/`triangleCount` simultaneously — a
reasonable shape at the time, under the unstated assumption that timing
and counts would become real together. **That assumption is false as soon
as Phase 3 is implemented before Phase 4** — which is exactly the
recommended order, for good, independent reasons (Phase 3 is cheap/
self-contained; Phase 4 is the most substantial/risky remaining phase).

The moment Phase 3's new call site does the natural-looking thing —
tagging a real count as `status = Present` — it has just told every
consumer of this struct, **including Phase 2's own already-shipped
`ComputeGpuMillisecondsRange()`**, that this pass's GPU timing this frame
was a real, measured `0.0` milliseconds. It was not measured at all —
Phase 4 doesn't exist yet. `ComputeGpuMillisecondsRange()` branches
exclusively on `status` (deliberately never on whether `milliseconds`
"looks like zero," per its own tests) — from its point of view,
`status == Present` *is* the promise that `milliseconds` is trustworthy.
Phase 3, implemented naively, would make that promise falsely, for a field
this phase was never asked to touch.

**The fix**: split `GpuPassSample`'s single `status` into two independent
tri-states — one governing timing, one governing counts:

```cpp
struct GpuPassSample {
    // Governs `milliseconds` only. Stays GpuSampleStatus::Absent until a
    // future Phase 4 (GPU timestamp queries) actually measures a real
    // value for this pass this frame - Phase 3 (draw-call/triangle
    // counts) never touches this field.
    GpuSampleStatus timingStatus = GpuSampleStatus::Absent;
    double milliseconds = 0.0; // Only meaningful when timingStatus == Present.

    // Governs drawCallCount/triangleCount only - Phase 3's own concern,
    // entirely independent of GPU timing. A pure CPU-side count of what
    // was queued via Submit()/FrameRecorder::Submit() this frame.
    GpuSampleStatus countStatus = GpuSampleStatus::Absent;
    std::uint32_t drawCallCount = 0; // Only meaningful when countStatus == Present.
    std::uint32_t triangleCount = 0; // Only meaningful when countStatus == Present.
};
```

**Why this is safe to do now**: `SetGpuPassSample()` has zero production
callers today (2.2) — every call site is a test constructing synthetic
data. There is no real, running behavior this correction could regress.
This is the cheapest possible moment this fix could ever be made.

--------------------------------------------------------------------------
## Step 3: The Plan (How will we get there?)
--------------------------------------------------------------------------

### 3.1 New, Tier-1, Vulkan-free file: `src/Renderer/DrawStats.h` + `DrawStats.cpp`

**Changed from v1**: the primary production-facing entry point is now a
tiny, `noexcept`, allocation-free **per-item accumulator**, designed to be
called from *inside* `FrameRecorder::RecordFrame()`'s existing loop rather
than over a separately-built list. A span-based batch wrapper is kept
alongside it purely as a convenient, symmetrical way to write table-driven
unit tests — it is not the production call path.

```cpp
#pragma once

#include <cstdint>
#include <span>

namespace gte {

// One recorded pass's aggregate draw-call/triangle totals - see
// FrameRecorder::RecordFrame()'s new return value, and AGENTS.md's
// "Profiling" section.
struct DrawStats {
    std::uint32_t drawCallCount = 0;
    std::uint32_t triangleCount = 0;
};

// Accumulates ONE queued draw's contribution into `stats` - pure,
// allocation-free, no Vulkan dependency. `hasIndexBuffer`/`vertexCount`/
// `indexCount` mirror FrameRecorder::DrawItem's own fields exactly (see
// FrameRecorder.cpp's RecordFrame(): vkCmdDrawIndexed() vs. vkCmdDraw()).
//
// DELIBERATELY meant to be called from INSIDE FrameRecorder::RecordFrame()'s
// existing per-item loop, at the exact same branch that already decides
// vkCmdDraw vs. vkCmdDrawIndexed - never from a separate pass over a
// separately-built list. This is a correctness decision, not a style
// preference: a separate counting pass over the same queue would be a
// second, independent place that has to keep agreeing with whatever the
// real recording loop actually does (including any future skip/validity
// branch added there) - fusing the two into one loop makes divergence
// between "what was counted" and "what was actually drawn" structurally
// impossible instead of something that has to be remembered.
//
// One triangle per 3 indices (when hasIndexBuffer) or 3 vertices
// (otherwise) - mirroring FrameRecorder::RecordFrame()'s own branch
// exactly. Dividing by 3 is always EXACT for this engine today: every
// Pipeline is unconditionally built with VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
// (see Pipeline.cpp), and every draw today has instanceCount == 1 (no
// instancing exists anywhere in this engine yet). A future non-triangle-
// list pipeline variant or real instancing support would need this
// formula (and this comment) revisited - see AGENTS.md's "Profiling"
// section. A malformed count not evenly divisible by 3 (never produced by
// any real importer today) simply truncates via integer division,
// mirroring what the GPU itself would do with a truncated final
// primitive - never a crash.
inline void AccumulateDrawStats(DrawStats& stats, bool hasIndexBuffer,
    std::uint32_t vertexCount, std::uint32_t indexCount) noexcept
{
    ++stats.drawCallCount;
    const std::uint32_t primitiveVertexCount = hasIndexBuffer ? indexCount : vertexCount;
    stats.triangleCount += primitiveVertexCount / 3;
}

// One queued draw's pure, countable shape - used ONLY by the test-facing
// batch wrapper below (CountDrawStats()), never by production code, which
// calls AccumulateDrawStats() directly inline instead (see its own
// comment above for why).
struct CountableDrawItem {
    bool hasIndexBuffer = false;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
};

// Test-facing convenience wrapper: applies AccumulateDrawStats() once per
// item, in order, over an already-built list. Exists so
// tests/Renderer/DrawStatsTests.cpp can write plain, table-driven
// "items in, DrawStats out" cases without needing a live FrameRecorder -
// see this file's own header comment and AGENTS.md's "Profiling" section.
// Never allocates on the caller's behalf beyond what `items` itself
// already occupies; safe to call every frame if a future caller ever
// prefers this shape over the inline accumulator.
DrawStats CountDrawStats(std::span<const CountableDrawItem> items) noexcept;

} // namespace gte
```

`.cpp`:

```cpp
#include "DrawStats.h"

namespace gte {

DrawStats CountDrawStats(std::span<const CountableDrawItem> items) noexcept
{
    DrawStats stats;
    for (const CountableDrawItem& item : items) {
        AccumulateDrawStats(stats, item.hasIndexBuffer, item.vertexCount, item.indexCount);
    }
    return stats;
}

} // namespace gte
```

Added to `gte_core`'s unconditional `add_library()` file list in the root
`CMakeLists.txt` (same tier as `Vertex.h`/`ResourcePool.h` — no
`GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROFILER` gate at all).

**On `GTE_ENABLE_PROFILER=OFF` cost (new in v2, stated explicitly rather
than left silent)**: `AccumulateDrawStats()` is a couple of integer ops
per queued draw — cheap enough that it is deliberately **not** gated
behind `#if GTE_ENABLE_PROFILER`, unlike `ScopeTimer`'s per-scope clock
read (which the "genuinely zero cost when off" rule in `AGENTS.md`
specifically targets). This is a considered decision, not an oversight:
`m_drawQueue` itself is already iterated unconditionally every frame
regardless of `GTE_ENABLE_PROFILER` to issue the real draw calls, and this
phase's accumulation rides along on that same, already-necessary
iteration at effectively no extra measurable cost — there is no separate
pass to skip. If a future profiling session ever finds this measurable
(it is not expected to), gating the one `AccumulateDrawStats()` call
inline behind `#if GTE_ENABLE_PROFILER` is a trivial, fully-contained
follow-up.

### 3.2 `FrameRecorder::RecordFrame()` gains a return value, fused into the existing loop

`FrameRecorder.h`'s declaration changes from:

```cpp
void RecordFrame(VkCommandBuffer cmd, const RenderTarget& target, VkFormat expectedFormat,
    VkFormat expectedDepthFormat, VkImageLayout finalLayout, const std::function<void(VkCommandBuffer)>& recordExtra);
```

to:

```cpp
DrawStats RecordFrame(VkCommandBuffer cmd, const RenderTarget& target, VkFormat expectedFormat,
    VkFormat expectedDepthFormat, VkImageLayout finalLayout, const std::function<void(VkCommandBuffer)>& recordExtra);
```

(`#include "DrawStats.h"` added alongside `FrameRecorder.h`'s existing
includes.)

**Changed from v1**: no intermediate `std::vector<CountableDrawItem>` is
built at all. A single `DrawStats drawStats;` is declared before the
existing per-item loop, and `AccumulateDrawStats(...)` is called once
per item **inside that same existing loop**, immediately alongside the
branch that already decides `vkCmdDrawIndexed` vs. `vkCmdDraw` — e.g.:

```cpp
DrawStats drawStats;
for (const DrawItem& item : m_drawQueue) {
    // ... existing pipeline bind / push-constant / descriptor-set code,
    // unchanged ...
    if (item.indexBuffer != VK_NULL_HANDLE) {
        vkCmdDrawIndexed(cmd, item.indexCount, 1, 0, 0, 0);
    } else {
        vkCmdDraw(cmd, item.vertexCount, 1, 0, 0);
    }
    AccumulateDrawStats(drawStats, item.indexBuffer != VK_NULL_HANDLE, item.vertexCount, item.indexCount);
}
```

**Whoever implements this must re-read the CURRENT body of this loop
first** (per Step 2's opening note) and confirm there is no skip/`continue`/
early-exit condition between "this item was popped off `m_drawQueue`" and
"this item's `vkCmdDraw*` was actually issued." If such a condition exists
today (none was found at the time of writing) or is added later,
`AccumulateDrawStats(...)` must be called **only** on the path that
actually issues the draw — never before a skip check, and never
unconditionally at the top of the loop body — so the count can never
overstate what the GPU actually received. This is precisely the
correctness property the fused design (vs. v1's separate pre-pass) exists
to make easy to get right and keep right.

The function's final line becomes `return drawStats;` — the queue is
still cleared exactly where it already is today, right after the draw
loop; this change alters nothing about *when* or *whether*
`m_drawQueue.clear()` runs.

### 3.3 `FramePresenter`: two different return shapes for two functions with different early-return behavior — deliberate, not an inconsistency

(Unchanged from v1's reasoning — restated here for completeness.)

`FramePresenter.h`:

```cpp
// See Renderer::Present(). Returns std::nullopt on a call that recorded
// NOTHING this time (a minimized window, a still-pending resize, or a
// just-recreated swapchain) - std::nullopt is the correct, honest signal
// that the "Present" GpuPass genuinely did not run this frame, distinct
// from "ran and recorded zero queued draws" (a real DrawStats{0, 0}).
std::optional<DrawStats> Present(FrameRecorder& frameRecorder, const std::function<void(VkCommandBuffer)>& recordExtra);

// See Renderer::RenderOffscreen(). Unlike Present() above, this function
// has no early-return path today - re-verify this is still true at
// implementation time (see Step 2's opening note) - so it always has a
// real DrawStats to return, never std::nullopt.
DrawStats RenderOffscreen(FrameRecorder& frameRecorder, RenderTexture& target,
    const std::function<void(VkCommandBuffer)>& recordExtra);
```

`FramePresenter.cpp`'s early-return points inside `Present()` each become
`return std::nullopt;`; its successful-completion path captures
`frameRecorder.RecordFrame(...)`'s result and returns it.
`RenderOffscreen()`'s call is returned directly, unwrapped.

### 3.4 `Renderer`: the same shape, forwarded one layer up

```cpp
std::optional<DrawStats> Present(const std::function<void(VkCommandBuffer)>& recordExtra = {});
DrawStats RenderOffscreen(RenderTexture& target, const std::function<void(VkCommandBuffer)>& recordExtra = {});
```

Two one-line forwarding bodies, no logic of `Renderer`'s own to add.
Confirmed non-breaking for `AssetPreviewMesh.cpp`/`BoneViewerWindow.cpp`
(bare-statement callers, discard the return value) and for
`Application.cpp` (the only place needing new code) — **re-confirm via a
fresh repository-wide search for `RenderOffscreen(`/`Present(` at
implementation time**, per Step 2's opening note.

### 3.5 `Application::Run()`: the one place that actually reports counts to the Profiler

Minimum-diff addition alongside the existing three `GTE_PROFILE_SCOPE(...)`
call sites:

```cpp
if (gameTarget != nullptr) {
    // ... unchanged Game-view setup ...
    GTE_PROFILE_SCOPE("Renderer::RenderOffscreen(GameView)");
    const DrawStats gameViewStats = m_renderer.RenderOffscreen(*gameTarget);
    Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(
        Profiling::GpuPass::GameView, Profiling::GpuSampleStatus::Present,
        gameViewStats.drawCallCount, gameViewStats.triangleCount);
}
if (sceneTarget != nullptr) {
    // ... unchanged Scene-view setup, same pattern with GpuPass::SceneView ...
}
{
    GTE_PROFILE_SCOPE("Renderer::Present");
    const std::optional<DrawStats> presentStats =
        m_renderer.Present([this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); });
    if (presentStats.has_value()) {
        Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(
            Profiling::GpuPass::Present, Profiling::GpuSampleStatus::Present,
            presentStats->drawCallCount, presentStats->triangleCount);
    }
    // else: Present() recorded nothing this frame - GpuPass::Present's
    // countStatus correctly stays at its default GpuSampleStatus::Absent,
    // with no extra code needed.
}
```

**Deliberately NOT wrapped in `#if GTE_ENABLE_PROFILER`** — matches the
existing, already-verified precedent in this same file:
`Profiling::FrameProfiler::Instance().BeginFrame()`/`EndFrame()` are not
guarded this way today; only `GTE_PROFILE_SCOPE(...)`'s own macro body is
compile-time-gated. Re-verify this precedent still holds at implementation
time before copying it.

### 3.6 `ProfilingTypes.h`/`FrameProfiler.h/.cpp`: the `timingStatus`/`countStatus` split, and its exact migration list

`ProfilingTypes.h`'s `GpuPassSample` changes to the shape shown in Step
2.4. `FrameProfiler::SetGpuPassSample()` is **replaced** (zero production
callers to preserve compatibility for) by two focused setters:

```cpp
void SetGpuPassTiming(GpuPass pass, GpuSampleStatus status, double milliseconds = 0.0) noexcept;
void SetGpuPassDrawStats(GpuPass pass, GpuSampleStatus status,
    std::uint32_t drawCallCount = 0, std::uint32_t triangleCount = 0) noexcept;
```

Both mirror `SetGpuPassSample()`'s existing body (same
`m_captureEnabled`/`m_frameInProgress` early-return, same unconditional
bounds check, active in Debug and Release), writing to one sub-set of
`GpuPassSample`'s fields each.

`FrameGraphData.cpp`'s `ComputeGpuMillisecondsRange()` changes its one
branch from `sample.status` to `sample.timingStatus` — a pure rename, zero
behavioral change.

**Exact existing test call sites that must be migrated in the same
change** (re-run this search at implementation time — treat this as a
checklist, not a guarantee it is still complete):

- `tests/Profiling/FrameProfilerTests.cpp` — the default-value loop (now
  two assertions per entry, `timingStatus` and `countStatus`), the
  combined-sample test (split into a timing call + a draw-stats call, plus
  a new assertion that setting one never touches the other), and the
  outside-frame-bracket no-op test (duplicated into a timing variant and a
  draw-stats variant).
- `tests/Profiling/FrameGraphDataTests.cpp` — every `SetGpuPassSample(...)`
  call site rewritten to call `SetGpuPassTiming`/`SetGpuPassDrawStats`
  individually depending on which half that specific test actually cares
  about.
- Every reference to `GpuPassSample::status` anywhere in either file
  replaced by `.timingStatus` or `.countStatus`.

**A new, MUST-HAVE regression test** (this is the one test that would have
caught Step 2.4's exact defect had it existed before this phase started):

```cpp
TEST_F(FrameProfilerTest, DrawStatsAloneDoNotImplyRealTimingData)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.BeginFrame();
    profiler.SetGpuPassDrawStats(GpuPass::GameView, GpuSampleStatus::Present, 12, 400);
    profiler.EndFrame();

    const FrameSample& frame = profiler.HistoryAt(0);
    const GpuPassSample& gameView = frame.gpuPasses[static_cast<std::size_t>(GpuPass::GameView)];

    EXPECT_EQ(gameView.countStatus, GpuSampleStatus::Present);
    EXPECT_EQ(gameView.drawCallCount, 12u);
    EXPECT_EQ(gameView.triangleCount, 400u);
    EXPECT_EQ(gameView.timingStatus, GpuSampleStatus::Absent);
}
```

...plus one new test in `FrameGraphDataTests.cpp` proving
`ComputeGpuMillisecondsRange()` correctly reports `hasData == false` for a
pass whose only data this session is a Phase-3-style draw-stats call.

### 3.7 New Tier-1 test file: `tests/Renderer/DrawStatsTests.cpp`

Mirrors `tests/Game/MeshMaterialPartitionerTests.cpp`'s own style.
**Changed from v1**: add cases for both entry points (the batch
`CountDrawStats()` wrapper AND the inline `AccumulateDrawStats()`
accumulator directly, since production code now calls the latter):

| Case | Input | Expected output |
|---|---|---|
| Empty queue | `{}` | `{0, 0}` |
| One non-indexed draw | `{false, 9, 0}` | `{1, 3}` |
| One indexed draw | `{true, 0, 300}` | `{1, 100}` |
| Multiple mixed draws | one non-indexed + one indexed | drawCallCount = 2; triangleCount = sum of each |
| Count not evenly divisible by 3 | `{true, 0, 10}` | truncates (`10 / 3 == 3`), proven exactly |
| Zero-vertex/zero-index degenerate draw | `{false, 0, 0}` | `drawCallCount` still +1; `triangleCount` +0 |
| Repeated `AccumulateDrawStats()` calls into the SAME `DrawStats` across several "items" | 3 sequential calls with hand-chosen counts | totals match calling `CountDrawStats()` once over the equivalent list — proves the two entry points agree, since production uses the inline form and tests primarily use the batch form |

Added to `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES` list.

### 3.8 Documentation updates (`AGENTS.md` AND `TESTING.md` — new in v2)

- **`AGENTS.md`**'s existing "Profiling" section gains bullets covering:
  `src/Renderer/DrawStats.h/.cpp` and why `AccumulateDrawStats()` is called
  inline from within `FrameRecorder::RecordFrame()`'s existing loop rather
  than over a separately-built list (the correctness argument from Step
  3.1); the `timingStatus`/`countStatus` split and the rule it enforces
  ("a producer of draw-call/triangle-count data must never also imply
  real GPU timing data exists, and vice versa"); and the
  `instanceCount == 1`/triangle-list-only assumptions baked into the
  triangle-count formula.
- **`TESTING.md` (new in v2 — v1 did not update this file at all)** gains
  a bullet for `Renderer/DrawStatsTests.cpp` in its own bullet-list style,
  immediately alongside the existing `Renderer/VertexTests.cpp`/
  `ResourcePoolTests.cpp` entries. While auditing this, also check whether
  `tests/Profiling/*` (a pre-existing suite from Phase 0-2, currently
  undocumented in `TESTING.md`) should get its own bullets added in the
  same pass — closing that gap is a small, low-risk addition to make while
  already editing this file's test list, even though it predates this
  phase.

### 3.9 Manual/Tier-2 verification

Same acceptance already established for this boundary: a full build, a
full `ctest` run, and a manual sanity check — spawn a known number of
primitive entities via "Hierarchy → Create 3D Object," read
`Profiling::FrameProfiler::Instance().LastCompletedFrame().gpuPasses[...]`
via a throwaway diagnostic (removed before this phase is done), and
confirm `GameView`'s reported `drawCallCount` matches the number of
`MeshRenderer` entities actually visible, and that hiding the "Scene"
panel makes `SceneView`'s `countStatus` read back as `Absent`, not a
fabricated `Present` with `{0, 0}`. **Additionally (new in v2)**: minimize
the window (or otherwise force `FramePresenter::Present()`'s
minimized-window branch) and confirm `GpuPass::Present`'s `countStatus`
also reads back `Absent` for those frames — this exercises the
`std::optional`/early-return path from Step 3.3, which is easy to write
but easy to forget to actually test manually.

--------------------------------------------------------------------------
## Step 4: What We Will NOT Do (Focus)
--------------------------------------------------------------------------

- No GPU timestamp queries, `VkQueryPool`, or `vkCmdWriteTimestamp2`
  anywhere in this phase — `timingStatus`/`milliseconds` are touched only
  to the extent of being correctly left alone by this phase's new call
  sites.
- No Editor "Profiler" panel, no ImGui code anywhere in this phase.
- No per-draw-call GPU timing, and no per-material/per-mesh breakdown of
  the counts — one `drawCallCount`/`triangleCount` pair per named
  `GpuPass`, matching `PROFILER_STRATEGY_v2.md`'s own feature table.
- No attempt to count geometry drawn via a `recordExtra` callback (Dear
  ImGui's overlay, `AssetPreviewMesh`/`BoneViewerWindow`'s previews) — see
  Step 2.3.
- No topology-awareness or instancing support beyond the two explicitly
  documented assumptions (triangle-list only, `instanceCount == 1`) this
  engine's `Pipeline`/`FrameRecorder` already always use today.
- No change to `Game`, `RenderSystem`, `MeshInstantiationSystem`,
  `AnimationSystem`, or ECS in any way.
- No new CMake option.
- No attempt to preserve `FrameProfiler::SetGpuPassSample()`'s old,
  combined signature alongside the new split.
- **No `#if GTE_ENABLE_PROFILER` gate around `AccumulateDrawStats()`'s
  call site inside `FrameRecorder::RecordFrame()`** (new in v2, stated
  explicitly as a considered decision — see Step 3.1's own cost
  discussion — not something to "fix" later without first actually
  measuring a real cost).

--------------------------------------------------------------------------
## Step 5: Their Role (What does this mean for you?)
--------------------------------------------------------------------------

### 5.1 Order of operations

1. Re-read the CURRENT `FrameRecorder.cpp`/`FramePresenter.cpp`/
   `Application.cpp` in full before writing anything — every line number
   in this document is a pointer to go verify, not a frozen fact (Step 2's
   opening note). In particular, confirm `FrameRecorder::RecordFrame()`'s
   per-item loop still has no skip/`continue` condition between popping an
   item and issuing its `vkCmdDraw*` call — if one now exists,
   `AccumulateDrawStats(...)` must be placed on the path that actually
   draws, never before that check.
2. `src/Renderer/DrawStats.h/.cpp` (Step 3.1) + its own tests (Step 3.7) —
   the one piece with zero dependents yet.
3. `ProfilingTypes.h`'s `timingStatus`/`countStatus` split (Step 2.4/3.6) +
   `FrameProfiler.h/.cpp`'s two new setters + `FrameGraphData.cpp`'s
   one-line rename + the full enumerated existing-test migration — done
   and green (full `ctest` run) BEFORE touching
   `FrameRecorder`/`FramePresenter`/`Renderer`.
4. `FrameRecorder::RecordFrame()`'s fused accumulation + return value
   (Step 3.2) → `FramePresenter`'s two differently-shaped return values
   (Step 3.3) → `Renderer`'s forwarding (Step 3.4) →
   `Application::Run()`'s new reporting calls (Step 3.5).
5. `AGENTS.md` + `TESTING.md` (Step 3.8) and manual verification (Step 3.9)
   last.

### 5.2 Non-negotiable checklist (copy into the PR/commit description)

- [ ] `AccumulateDrawStats()`/`CountDrawStats()` have full Tier-1 coverage
      added in the SAME change that introduces them.
- [ ] Confirmed (by re-reading the actual, current loop body) that
      `AccumulateDrawStats(...)` is called on the exact same
      code path that issues the real `vkCmdDraw`/`vkCmdDrawIndexed` — not
      before a skip check, not from a separate pass over a separately
      built list.
- [ ] Every existing test enumerated in Step 3.6 is migrated — run a
      repository-wide search for `SetGpuPassSample(` and bare `.status`
      (scoped to `GpuPassSample` usages) after the change and confirm zero
      remaining references to the old, combined API/field name.
- [ ] The `DrawStatsAloneDoNotImplyRealTimingData`-style regression test
      (Step 3.6) exists and is green.
- [ ] `FramePresenter::Present()`'s early-return points each now return
      `std::nullopt`, verified by re-reading the actual current diff — not
      assumed from this document.
- [ ] `AssetPreviewMesh.cpp`/`BoneViewerWindow.cpp` confirmed to still
      compile unchanged.
- [ ] `Application.cpp`'s new `SetGpuPassDrawStats(...)` calls are NOT
      wrapped in `#if GTE_ENABLE_PROFILER`, matching the verified
      `BeginFrame()`/`EndFrame()` precedent in the same file.
  - [ ] The `Present` pass's `SetGpuPassDrawStats(...)` call is gated on
      `presentStats.has_value()`, never called unconditionally — verified
      manually with the window minimized (Step 3.9).
- [ ] Full build + full `ctest` run — every pre-existing test still
      passes, plus every new one from Steps 3.6/3.7, with an honest,
      freshly-recounted total.
- [ ] `AGENTS.md` **and `TESTING.md`** are both updated in the same
      change (Step 3.8) — not as a follow-up.
- [ ] After landing: update (or fold into)
      `PROFILER_IMPLEMENTATION_STATUS_v3.md`, recording the exact final
      test count and confirming no regression.

### 5.3 What this phase deliberately leaves for you to decide later

- Whether a future Phase 7 panel wants its own
  `ComputeGpuDrawCallCountRange()`/`ComputeGpuTriangleCountRange()`
  siblings to `ComputeGpuMillisecondsRange()` in `FrameGraphData.h` —
  deliberately out of scope for this phase; the raw per-frame data is
  already sitting in `FrameGraphPoint::gpuPasses[...].drawCallCount`/
  `triangleCount` today.
- Whether `AccumulateDrawStats()`'s call site inside
  `FrameRecorder::RecordFrame()` is ever worth gating behind
  `#if GTE_ENABLE_PROFILER` — deliberately left as "not yet, revisit only
  if actually measured as a problem" (Step 3.1).

Once this phase lands, `PROFILER_STRATEGY_v2.md`'s Step 1.2 feature table
row 5 ("Draw-call and triangle counts") moves from aspirational to real.
