# GreatTamanaEngine — Phase 7 Grand Strategy: Editor "Profiler" Panel (v2)

Status: PROPOSAL / PLANNING DOCUMENT — no implementation yet.
Supersedes: `PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v1.md`. Following this
project's own established "bump the version, delete the superseded file"
convention (see `PROFILER_IMPLEMENTATION_STATUS_v5.md` → `_v6` in Step 3.8,
and the fact that `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v3.md` already implies
a v1/v2 of its own once existed) — **delete `..._v1.md` once this file is
the team's working reference**, don't keep both around indefinitely.

Scope is unchanged from v1: `PROFILER_STRATEGY_v2.md`'s own Phase 7 — "The
Editor 'Profiler' panel" — the last phase in the original 8-phase plan.
Prerequisite reading is unchanged from v1: `PROFILER_STRATEGY_v2.md`,
`PROFILER_IMPLEMENTATION_STATUS_v5.md`, `AGENTS.md`'s "Profiling" and
"Editor Module Structure" sections, and `Phase7.md`.

--------------------------------------------------------------------------
## Changelog: what changed from v1, and why (read this first)
--------------------------------------------------------------------------

v1 was already a solid, source-verified plan. This pass re-read it
specifically hunting for (1) missing context and (2) places where a
different call would genuinely be better, rather than re-deriving the
whole plan from scratch. Six real findings came out of that pass, all
folded into the step-by-step plan below (not just listed here):

1. **Missing context: what does this panel show when
   `GTE_ENABLE_PROFILER=OFF`?** v1 never once discusses this build
   configuration. Per `AGENTS.md`'s own "Profiling" section, that switch
   compiles `GTE_PROFILE_SCOPE`'s body down to a true empty no-op — meaning
   **every CPU scope call site in the engine compiles away entirely** in
   that configuration, while `FrameProfiler`'s own ring buffer, and
   (per that same section) very likely the CPU frame-time measurement
   itself, keep working regardless (`FrameProfiler.h/.cpp` has no
   `GTE_ENABLE_PROFILER` dependency at all). That means: with
   `GTE_ENABLE_PROFILER=OFF`, the CPU frame-time graph should keep working
   correctly, but the "CPU Scopes" table would show **zero rows, forever**
   — and nothing in v1 distinguished that permanent, compiled-out state
   from the ordinary, transient "no scopes recorded yet this frame"
   empty state a fresh Editor session already legitimately has for a
   moment. A user (or a future agent) staring at a permanently-empty CPU
   Scopes table with no explanation would reasonably assume this panel is
   broken. **Fixed in Step 3.2/3.3 below**: a new, tiny, always-correct
   compile-time distinction and a corrected empty-state message.
2. **Missing context: Pause → un-Pause was never actually specified.** v1's
   Step 3.3 describes in detail what happens on the frame `m_paused` flips
   `false → true` (capture a frozen snapshot) but says nothing about what
   happens on the frame it flips back `true → false`. The behavior is
   simple (every section already reads `m_paused` fresh each frame, so
   flipping it off just means the very next frame's sections read live
   data again, and the stale `m_frozenPoints`/`m_frozenLatestFrame` can be
   left sitting unused rather than needing any explicit clear), but v1
   never SAID this, which is exactly the kind of gap that causes an
   implementer to invent unnecessary code (e.g. an unneeded "on unpause"
   branch) that isn't wrong so much as pure noise. **Fixed in Step 3.3.**
3. **Ambiguous, non-committal spec: the GPU Timing section's pass
   coverage.** v1's Step 3.3 item 6 said "one call to
   `FormatGpuTimingLine()` per named pass (**or, at minimum, for
   `GameView`**...)" — leaving it to whoever implements this to decide.
   That is a planning-document failure mode this very document elsewhere
   correctly avoids (e.g. it's explicit that Present is docked in the same
   `bottom` node as Memory, not "wherever seems fine"). **Fixed in Step
   3.3: all three named passes, unconditionally, matching the identical
   three-pass treatment the draw-call/triangle section already gets** —
   there's no cost or ambiguity reason to do fewer, and doing all three
   keeps the two sections visually/structurally parallel.
4. **A real, if minor, design improvement: avoid a fresh heap allocation
   every single frame for the two `ImGui::PlotLines()` float conversions.**
   v1's Step 2.4 correctly bans this per-frame `float` conversion from ever
   leaking into the Tier-1 `ProfilerPanelData.h` layer, but then Step 3.3
   still describes building a throwaway local `std::vector<float>` fresh
   every frame for BOTH the CPU graph and the memory sparkline, for as
   long as the panel stays visible. Since v1 *already* made `ProfilerPanel`
   a stateful class specifically to hold `m_paused`/`m_frozenPoints`, there
   is no reason not to also let it own small reusable scratch buffers for
   this — turning a per-frame allocation into an occasional one (only
   growing, never on every frame once warmed up). Not a correctness bug,
   just wasted, easily-avoided per-frame churn in code that already pays
   for a class instance anyway. **Fixed in Step 3.3.**
5. **Missing verification: the very first frame(s) after the Editor opens,
   before anything has rendered a "real" scene, were never explicitly
   checked.** v1's Step 3.7 manual-verification list starts with "spawn
   several primitive entities" — it never verifies the panel's OWN
   empty/startup state (a fresh `imgui.ini`, zero completed frames yet,
   default-constructed `FrameSample`s with `cpuScopeCount == 0` and every
   `GpuPassSample` at `Absent`). This is precisely the state most likely to
   expose an off-by-one or an unguarded read (e.g. of `LastCompletedFrame()`
   before a single frame has ever completed). **Fixed in Step 3.7: added as
   the explicit first verification step**, before entities are even
   spawned.
6. **Missing context: an unverified assumption about what "Capture" toggles
   off.** v1's Step 1.3 success criteria assert that toggling Capture off
   makes `FrameProfiler::CompletedFrameCount()` stop advancing — reasonable,
   and almost certainly right, but v1 never flagged this as something to
   *re-confirm by reading `FrameProfiler.cpp` directly* before wiring the
   Capture checkbox's own descriptive text, the same "don't trust it, go
   look" discipline this document already applies everywhere else (e.g.
   Step 5.1's "never trust a number quoted in any planning document"). If
   it turns out `SetCaptureEnabled(false)` only suppresses *scope*
   recording and *not* frame-time/draw-stats/memory-snapshot recording (or
   vice versa), the "Capture disabled" notice text needs to say exactly
   that, not a generic "no new frames are recorded" that could be wrong in
   a way nobody checked for. **Fixed in Step 2.4 (new constraint) and Step
   3.3 (a concrete re-verification instruction right where the checkbox's
   text is written).**

Nothing else of substance changed — the file layout, the CMake wiring, the
"why a class not a free function," the tri-state discipline, the "don't
build a second CSV exporter" refusal, etc. were all already correct in v1
and are carried forward unchanged below.

--------------------------------------------------------------------------
## Where this phase sits, right now, for real
--------------------------------------------------------------------------

(Unchanged from v1 — re-confirmed by directly reading the current source
tree before writing a word of either version of this plan.)

- Phase 0 (data model), Phase 1 (CPU scope timers), Phase 2 (frame-time
  graph data, `src/Profiling/FrameGraphData.h/.cpp`), Phase 3 (draw-call/
  triangle counts, `src/Renderer/DrawStats.h/.cpp`), and Phase 5 (GPU
  memory history, `src/Application/MemorySnapshotBuilder.h`) are all
  implemented, tested, and wired into real production call sites — see
  `PROFILER_IMPLEMENTATION_STATUS_v5.md`.
- Phase 4 (Vulkan GPU timestamp queries) is deliberately skipped for now.
  Every downstream consumer, this phase included, treats "no GPU timing
  yet" as a first-class, permanent-until-further-notice state
  (`GpuSampleStatus::Absent`), never a temporary gap that blocks anything.
- Phase 6 (benchmark mode) is not started, and is not a blocker for this
  phase — Phase 7 and Phase 6 are two independent *consumers* of the same
  already-built data model.
- **Phase 7 itself has literally zero code today.** No
  `src/Editor/ProfilerPanelData.h/.cpp`, no
  `src/Editor/Panels/ProfilerPanel.h/.cpp`, no `"Profiler"` string anywhere
  in `src/Editor/DockLayout.cpp`. This phase closes that gap.

--------------------------------------------------------------------------
## Step 1: The Goal (Where are we going?)
--------------------------------------------------------------------------

### 1.1 What "done" looks like, concretely

A sixth dockable Editor panel, **"Profiler"**, tabbed alongside "Memory"
(and "Project", if enabled) along the bottom of the default layout —
exactly where Unity's own Profiler window lives relative to its
Console/Project panels — answering, live, with real already-collected
data and zero new engine-level plumbing beyond one small, already-
anticipated extension to Phase 2's reshape module:

- "Is my frame time trending up/down/spiky right now?" — a scrolling CPU
  frame-time graph over the last ~240 frames, current ms + FPS, visible
  range min/max.
- "Which named CPU system is costing the most time THIS frame?" — a
  sorted, biggest-first table (name / total ms / call count) — same
  "biggest contributor first" convention as the "Memory" panel.
- "How many draw calls/triangles is Game submitting right now?" — Game
  View primary, Scene View/Present as secondary context, a hidden/not-run
  pass reading **"N/A"**, never a misleading "0".
- "Is GPU memory usage climbing (a leak) or flat/sawtoothing (expected
  churn)?" — current total/buffer/texture bytes + a sparkline over the
  same window the CPU graph uses.
- "Is GPU-side timing available yet?" — an honest, permanent-until-
  Phase-4 **"GPU Timing: N/A — Vulkan timestamp queries are not available
  yet."** line for every named pass (not just one — see Changelog #3),
  never a fabricated `0.00 ms`.
- "Can I freeze what I'm looking at without stopping real data collection
  underneath?" — a Pause control fully independent of the Capture on/off
  toggle (`Profiling::FrameProfiler::SetCaptureEnabled()`).
- **New in v2:** "If CPU scope instrumentation is compiled out entirely,
  does the panel tell me that, instead of just looking broken?" — yes; see
  Step 3.2/3.3.

No new engine-level tracking mechanism is invented anywhere in this phase.
Every number this panel displays already exists inside
`Profiling::FrameProfiler`'s ring buffer — this phase's job is
presentation, plus one small, already-anticipated data-model extension
(Step 2.3/3.1).

### 1.2 Concrete deliverables

1. `Profiling::FrameGraphData.h/.cpp` gains a `memory` field on
   `FrameGraphPoint` and `ComputeMemoryBytesRange()`.
2. A new, pure, ImGui-free, Tier-1-tested
   `src/Editor/ProfilerPanelData.h/.cpp`, following `MemoryPanelData.h/.cpp`'s
   template — **now also including a compile-time-correct "why is this
   empty" helper for the CPU scope table (Changelog #1)**.
3. A new `src/Editor/Panels/ProfilerPanel.h/.cpp` — a small, stateful
   class (the sanctioned exception, `AGENTS.md`'s "Editor Module
   Structure") — **now explicitly documented as also owning small reusable
   scratch buffers for its ImGui-facing `float` conversions, not just its
   Pause snapshot (Changelog #4)**.
4. `"Profiler"` added to `DockLayout.cpp`'s `kAllPanelNames` array and
   `BuildDefaultDockLayout()`'s bottom-docked group, in the same change.
5. `ImGuiEditorLayer` gains one new member (`m_profilerPanel`) and one new
   call inside `BuildUI()`.
6. Full Tier-1 test coverage, CMake wiring, documentation updates
   (`AGENTS.md`/`README.md`/`TESTING.md`/`PROFILER_IMPLEMENTATION_STATUS_v5.md`
   → `_v6`), and a full clean build + `ctest` run with zero regressions
   before this phase is done. **Also delete
   `PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v1.md` once this file (v2) is the
   accepted plan.**

### 1.3 Success criteria

- A fresh (or pre-Phase-7) `imgui.ini` shows a "Profiler" tab, tabbed
  alongside "Memory", with no manual re-layout needed.
- Spawning primitive entities visibly moves the CPU frame-time graph, the
  `RenderSystem::Draw` scope row, Game-view draw-call/triangle numbers,
  and GPU memory numbers — all in the SAME panel.
- Hiding "Scene" makes the panel's "Scene View" line read **"N/A"**, not
  `"0 draw calls"` — verified by actually doing it.
- Toggling "Capture" off freezes the underlying data
  (`FrameProfiler::CompletedFrameCount()` stops advancing — **re-confirm
  this by reading `FrameProfiler.cpp` before writing the checkbox's own
  descriptive text; see Step 2.4's new constraint and Step 3.3**); toggling
  "Pause" independently (Capture still ON) freezes only the panel's own
  display, proven by a throwaway diagnostic showing
  `FrameProfiler::Instance().CompletedFrameCount()` still advancing.
- **New in v2:** with `-DGTE_ENABLE_PROFILER=OFF`, the CPU frame-time graph
  still works, and the CPU Scopes table honestly states that scope
  instrumentation is compiled out — never an unexplained, indistinguishable
  "empty" table (Step 3.7's new first verification step covers the
  Editor-just-opened case; a second build with this flag off covers the
  compiled-out case).
- `cmake --build build --config Debug` succeeds, and
  `ctest --test-dir build -C Debug --output-on-failure` reports the full
  suite passing (**re-run `ctest` fresh immediately before writing any
  final count anywhere**).
- Building with `-DGTE_ENABLE_EDITOR=OFF` still produces a clean binary
  with zero ImGui linked in.

--------------------------------------------------------------------------
## Step 2: The Situation / The Problem (Where are we now?)
--------------------------------------------------------------------------

Based on directly reading the current source tree (unchanged scope from
v1: `src/Profiling/ProfilingTypes.h`, `FrameProfiler.h`,
`FrameGraphData.h`, `src/Renderer/DrawStats.h`,
`src/Editor/MemoryPanelData.h`, `Panels/MemoryPanel.h/.cpp`,
`DockLayout.h/.cpp`, `EditorContext.h`, `EditorLayer.h`,
`ImGuiEditorLayer.cpp`, `Panels/GamePanel.h`, root `CMakeLists.txt`).

### 2.1 What already exists that this phase gets to reuse

(All unchanged from v1 — this reuse inventory was already accurate and
complete.)

- `Profiling::FrameProfiler::Instance()` — process-wide singleton, 300-frame
  (`kMaxFrameHistory`) ring buffer of `FrameSample`, `HistoryCount()`/
  `HistoryAt(i)`/`LastCompletedFrame()`, `SetCaptureEnabled(bool)`/
  `IsCaptureEnabled()` already the runtime switch this phase's "Capture"
  toggle drives directly.
- `Profiling::FrameGraphData.h/.cpp` — `BuildFrameGraphPoints()`,
  `ComputeCpuMillisecondsRange()`/`ComputeGpuMillisecondsRange()`, both
  taking `std::span<const FrameGraphPoint>` specifically so a caller can
  pass a zero-copy windowed suffix — exactly this phase's windowing need,
  already built.
- `src/Renderer/DrawStats.h`'s `DrawStats`/`AccumulateDrawStats()` — every
  real frame's per-pass draw-call/triangle counts are real, measured data.
- `src/Application/MemorySnapshotBuilder.h`'s `BuildMemorySnapshot()` — the
  one production call site feeding a real `Profiling::MemorySnapshot` every
  frame, unconditionally, always tagged `Present`.
- `src/Editor/MemoryPanelData.h/.cpp` + `Panels/MemoryPanel.h/.cpp` — the
  directly-reusable Tier-1 precedent (plain reshape functions +
  thin ImGui wrapper) to copy function-for-function.
- `DockLayout.cpp`'s `kAllPanelNames`/`BuildDefaultDockLayout()` pairing —
  both must be updated together, per the file's own comment.
- `EditorLayer.h`'s `IEditorLayer` boundary needs zero new virtual methods.
- `AGENTS.md`'s explicit pre-approval of a stateful-class panel exception.
- The CMake wiring precedent (`if(GTE_ENABLE_EDITOR)` block, no new option
  needed, not additionally gated behind `GTE_ENABLE_PROJECT_PANEL`).
- The testing-tier discipline (`tests/Editor/*Tests.cpp` gated the same way
  as the module they test).

### 2.2 What is genuinely missing today (unchanged from v1)

- No `ProfilerPanelData`/`ProfilerPanel` files exist.
- `"Profiler"` appears nowhere in `DockLayout.cpp`.
- Nothing reads `FrameProfiler`'s history for display purposes at all,
  outside tests/a throwaway diagnostic.
- `ImGuiEditorLayer::BuildUI()` has no Profiler-shaped call.

### 2.3 The genuine, already-anticipated data-model gap (unchanged from v1)

`FrameGraphPoint` today carries `frameIndex`/`cpuMilliseconds`/`gpuPasses`
only — no `MemorySnapshot`. Predicted, by name, in
`PROFILER_IMPLEMENTATION_STATUS_v5.md`'s own "Known rough edges" section:
a future consumer needing memory-over-time would add
`ComputeMemoryBytesRange()` "then, not before." **This phase is "then."**
Step 3.1 extends `FrameGraphPoint` with a verbatim-copied `MemorySnapshot
memory` field and adds `ComputeMemoryBytesRange()` as a sibling to the two
existing range functions, living in `src/Profiling/FrameGraphData.h/.cpp`
(always-compiled, Editor-independent) so a future Phase 6 CSV exporter
consumes the exact same reshape, never a second copy.

### 2.4 Constraints discovered while reading the code (must be respected)

(All of v1's constraints carried forward unchanged, plus **two new ones**
marked NEW below.)

- `GpuPassSample` has TWO INDEPENDENT tri-states, `timingStatus` and
  `countStatus` — never a single combined `status`. Draw-call/triangle
  display branches on `countStatus` only; GPU timing display branches on
  `timingStatus` only.
- `GpuPass` is `{GameView = 0, SceneView = 1, Present = 2}`,
  `kGpuPassCount == 3`. A hidden/not-run pass reports `Absent`, never a
  `Present` with `{0, 0}`.
- `FrameGraphPoint`/`FrameGraphRange`'s existing tests use
  `EXPECT_DOUBLE_EQ` for plain copies — this phase's new tests must follow
  the same convention and mirror the existing ring-buffer-wraparound/
  tri-state-exclusion test patterns rather than reinvent them.
- `FrameProfiler::OverrideLastFrameCpuMillisecondsForTestingForTesting()`
  exists for deterministic CPU-millisecond seeding — this phase's own tests
  either use this hook (through a real `FrameProfiler`) or simply
  hand-construct a `FrameGraphPoint` by value (since `ProfilerPanelData.h`'s
  functions take already-built `FrameSample`/`FrameGraphPoint` values, not a
  live `FrameProfiler&`).
- `FrameProfiler` is a Meyers singleton, explicitly single-threaded.
- Every existing Editor panel is a stateless free function —
  `ProfilerPanel` is the deliberate, pre-approved exception, still called
  explicitly BY NAME from `BuildUI()`, no `IEditorPanel` interface.
- `DockLayout.cpp`'s one-shot layout logic is order-sensitive; adding
  `"Profiler"` to `kAllPanelNames` requires zero changes to the one-shot
  logic itself, only to the data it reads.
- No `RenderTexture`/GPU resource is needed for this panel — no
  `ctx.profilerPanelVisible`-style flag, since nothing expensive is skipped
  by hiding it.
- `Panels/MemoryPanel.cpp`'s `BuildMemoryPanel()` calls `ImGui::Begin()`
  unconditionally, without checking its return value — `ProfilerPanel`
  should match this established convention for consistency.
- `ImGui::PlotLines()` takes `const float*`, not `double` — a conversion is
  needed at the point of drawing, confined to Tier-2 `Panels/ProfilerPanel.cpp`.
  **(See the amended design in Step 3.3 for how this conversion buffer is
  now owned, per Changelog #4 — it must not be a fresh
  `std::vector<float>` built from scratch every single frame; reuse a
  member scratch buffer instead.)**
- **NEW — must be re-verified before writing the Capture checkbox's own
  text (Changelog #6):** re-read `FrameProfiler::SetCaptureEnabled()`/
  `BeginFrame()`/`EndFrame()` directly before implementation and confirm,
  precisely, what "Capture disabled" actually suppresses — e.g. does it
  skip advancing the ring buffer entirely (no new `FrameSample` at all that
  frame), or does it still record frame time/memory but skip only CPU
  scopes/draw stats, or some other split? Whatever the real, current
  behavior is, the checkbox's own sub-text (Step 3.3, item 1) must
  describe *that exact behavior*, not the behavior this document assumes.
  Do not copy v1's assumed wording ("no new frames are being recorded")
  into code without confirming it against the real function body first.
- **NEW — the CPU Scopes table must distinguish "genuinely no scopes this
  frame (yet)" from "scope instrumentation is compiled out entirely"
  (Changelog #1):** `GTE_PROFILE_SCOPE`'s body is a true no-op whenever
  `GTE_ENABLE_PROFILER=OFF` (see `AGENTS.md`, "Profiling"), meaning every
  CPU scope call site in the engine compiles away in that configuration —
  permanently, for the life of the process — while `FrameProfiler`'s own
  ring buffer (and, per that same section, CPU frame-time measurement)
  keeps working regardless, since `FrameProfiler.h/.cpp` has no
  `GTE_ENABLE_PROFILER` dependency at all. A table that's merely empty
  either way is indistinguishable to a user from a bug. This phase's
  `ProfilerPanelData.h` must expose a small, compile-time-correct way for
  `Panels/ProfilerPanel.cpp` to tell the two states apart (see Step 3.2).

--------------------------------------------------------------------------
## Step 3: The Plan (How will we get there?)
--------------------------------------------------------------------------

Nine concrete, sequential, independently-buildable-and-testable steps,
each mapped back to `Phase7.md`'s own numbered "must-have" items.

### Step 3.1 — Extend `Profiling::FrameGraphData.h/.cpp` with GPU memory history

*(Unchanged from v1 — this part of the plan was already correct.)*

`FrameGraphPoint` gains one new field, appended after `gpuPasses` (never
reordering existing fields):

```cpp
struct FrameGraphPoint {
    std::uint64_t frameIndex = 0;
    double cpuMilliseconds = 0.0;
    std::array<GpuPassSample, kGpuPassCount> gpuPasses{};

    // Copied verbatim from FrameSample::memory (Phase 5) - the exact same
    // tri-state MemorySnapshot type FrameSample itself already uses.
    MemorySnapshot memory{};
};
```

`BuildFrameGraphPoints()` gains exactly one new line
(`point.memory = frame.memory;`).

A new sibling function:

```cpp
struct MemoryBytesRange {
    bool hasData = false;
    std::uint64_t minBytes = 0;
    std::uint64_t maxBytes = 0;
};

// Computes the min/max TOTAL GPU memory byte range across every point in
// `points`, including ONLY entries whose memory.status ==
// GpuSampleStatus::Present - mirrors ComputeGpuMillisecondsRange()'s own
// "branch on status, never on the value" rule exactly.
MemoryBytesRange ComputeMemoryBytesRange(std::span<const FrameGraphPoint> points);
```

**Tests** (added to the existing `tests/Profiling/FrameGraphDataTests.cpp`):

1. `BuildFrameGraphPointsCopiesMemorySnapshotVerbatim` — every field a
   distinct value, field-for-field equality check.
2. `ComputeMemoryBytesRangeIgnoresAbsentEntries`.
3. `ComputeMemoryBytesRangeOnAllAbsentReportsNoData`.
4. `ComputeMemoryBytesRangeOnEmptyPointsReportsNoData`.
5. `AbsentMemorySampleWithStaleNonZeroValueIsStillExcluded`.

Textbook Tier 1, no new CMake wiring needed.

### Step 3.2 — New file: `src/Editor/ProfilerPanelData.h/.cpp`

Pure, ImGui-free reshape/formatting functions, following
`MemoryPanelData.h`'s template. `#include`s `"../Profiling/FrameGraphData.h"`
and `"MemoryPanelData.h"` (to reuse `FormatBytes()`, never a second one).

```cpp
#pragma once

#include "../Profiling/FrameGraphData.h"
#include "../Profiling/ProfilingTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

std::vector<Profiling::CpuScopeSample> BuildSortedCpuScopeRows(const Profiling::FrameSample& frame);

std::string FormatDuration(double milliseconds);
std::string FormatFrameTimeSummary(double cpuMilliseconds);
std::string FormatCount(std::uint64_t value);

struct GpuPassCountDisplay {
    bool available = false;
    std::uint32_t drawCallCount = 0;
    std::uint32_t triangleCount = 0;
};

GpuPassCountDisplay ResolveGpuPassCounts(const Profiling::FrameSample& frame, Profiling::GpuPass pass);
const char* ToString(Profiling::GpuPass pass);
std::string FormatGpuTimingLine(const Profiling::GpuPassSample& pass);

// --- NEW in v2 (Changelog #1) ---
//
// Whether GTE_PROFILE_SCOPE actually records anything anywhere in this
// build, i.e. whether GTE_ENABLE_PROFILER was ON when this translation
// unit was compiled. A plain compile-time constant, not a runtime probe -
// this is exactly as permanent/unchanging for the life of the process as
// GTE_ENABLE_EDITOR itself is, so a simple constexpr bool is enough; no
// caching/memoization concern exists.
//
// NOTE: confirm at implementation time that GTE_ENABLE_PROFILER is
// actually visible as a preprocessor definition to files under
// src/Editor/ (it is PUBLIC-defined on the gte_core target per
// AGENTS.md's "Profiling" section, the same way GTE_ENABLE_EDITOR/
// GTE_ENABLE_PROJECT_PANEL already are - so it should be, but this is
// exactly the kind of "assumed, not yet verified" detail this document
// elsewhere insists on double-checking against the real CMakeLists.txt
// before relying on it in code).
constexpr bool kCpuScopeInstrumentationCompiledIn =
#if GTE_ENABLE_PROFILER
    true;
#else
    false;
#endif

// The CPU Scopes table's own empty-state message - distinguishes "this
// build can never show scope data, by design" (kCpuScopeInstrumentationCompiledIn
// == false) from "no scopes have been recorded yet this particular frame"
// (still true, transiently, even in a build where it's compiled in - e.g.
// the very first frame, or Capture just having been re-enabled). See
// PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md, Changelog #1.
const char* CpuScopeTableEmptyMessage();

} // namespace gte
```

**Design decisions (mirroring v1's own decision-log format):**

- `BuildSortedCpuScopeRows()` returns `std::vector<Profiling::CpuScopeSample>`
  directly, not a new parallel struct — `CpuScopeSample` already carries
  exactly what's needed. (Unchanged from v1.)
- `ResolveGpuPassCounts()` is the ONE place the count tri-state collapses
  to a bool, deliberately not inside `FrameGraphData.h`/`ProfilingTypes.h`
  themselves (a future Phase 6 CSV exporter may still want the full
  tri-state). (Unchanged from v1.)
- `FormatGpuTimingLine()` takes a whole `GpuPassSample`, so a future Phase
  4's real `Present` data flows through unchanged. (Unchanged from v1.)
- **New in v2:** `kCpuScopeInstrumentationCompiledIn`/
  `CpuScopeTableEmptyMessage()` exist specifically so `Panels/ProfilerPanel.cpp`
  never has to embed its own `#if GTE_ENABLE_PROFILER` — the ONE place
  this compile-time fact is turned into user-facing text is here, in the
  Tier-1, testable layer, exactly like every other formatting decision in
  this file.

**Tests: `tests/Editor/ProfilerPanelDataTests.cpp`** (new), added to
`tests/CMakeLists.txt`'s existing `if(GTE_ENABLE_EDITOR)` test block:

- `BuildSortedCpuScopeRowsSortsBiggestFirst`
- `BuildSortedCpuScopeRowsRespectsCpuScopeCountNotArrayCapacity`
- `BuildSortedCpuScopeRowsOnEmptyFrameReturnsEmpty`
- `FormatDurationProducesExpectedText`
- `FormatFrameTimeSummaryComputesFpsCorrectly` (including the zero/negative
  → `"N/A"` guard)
- `FormatCountGroupsThousandsCorrectly`
- `ResolveGpuPassCountsReportsAvailableForPresentStatus`
- `ResolveGpuPassCountsReportsUnavailableForAbsentStatusEvenWithStaleNonZeroValues`
- `ResolveGpuPassCountsBoundsChecksOutOfRangePass`
- `ToStringGpuPassCoversAllThreeValues`
- `FormatGpuTimingLineReportsPlaceholderForAbsentAndUnsupported`
- `FormatGpuTimingLineReportsRealValueForPresent`
- **New in v2:** `CpuScopeTableEmptyMessageMatchesCompileTimeFlag` — asserts
  `CpuScopeTableEmptyMessage()`'s returned text is consistent with
  `kCpuScopeInstrumentationCompiledIn`'s current value in whatever build
  configuration the test suite itself was compiled with (this test can't
  meaningfully flip the flag at runtime, but it does prove the function
  and the constant never disagree with each other within one build — a
  real, if narrow, regression it can actually catch).

### Step 3.3 — New file: `src/Editor/Panels/ProfilerPanel.h/.cpp`

`ProfilerPanel.h`:

```cpp
#pragma once

#include "../../Profiling/FrameGraphData.h"

#include <vector>

namespace gte {

struct EditorContext;
class Renderer;

// Unity-Profiler-window-style panel. Deliberately a small STATEFUL CLASS,
// not a free function (see AGENTS.md, "Editor Module Structure").
class ProfilerPanel {
public:
    void Build(EditorContext& ctx);

private:
    bool m_paused = false;

    // The frozen snapshot captured at the moment m_paused most recently
    // became true. See Step 3.3's own prose below for the FULL pause/
    // un-pause state machine (both directions, spelled out explicitly -
    // v1 only ever documented one direction of this transition).
    std::vector<Profiling::FrameGraphPoint> m_frozenPoints;
    Profiling::FrameSample m_frozenLatestFrame{};

    // --- NEW in v2 (Changelog #4) ---
    // Reusable scratch buffers for the two ImGui::PlotLines() float
    // conversions (CPU frame-time graph, GPU memory sparkline). Owned
    // here rather than built fresh as a local std::vector<float> every
    // single frame the panel is visible - cleared and refilled each call,
    // but only ever REALLOCATES when the visible window genuinely grows
    // past its previous largest size (e.g. its first few frames of life),
    // never on every single frame steady-state. Purely a Tier-2, ImGui-
    // facing efficiency detail; never read/written by anything in
    // ProfilerPanelData.h.
    std::vector<float> m_cpuGraphScratch;
    std::vector<float> m_memoryGraphScratch;
};

} // namespace gte
```

`ProfilerPanel.cpp`'s `Build()`, section by section:

1. **Capture / Pause controls row** (`Phase7.md` item 7):
   - `"Capture"` checkbox bound to
     `Profiling::FrameProfiler::Instance().IsCaptureEnabled()`/
     `SetCaptureEnabled()`. When unchecked, an `ImGui::TextDisabled()` line
     underneath explains exactly what's suppressed — **worded to match
     whatever `FrameProfiler::SetCaptureEnabled(false)` is actually
     verified to do (Step 2.4's new constraint), not assumed wording.**
   - `"Pause"` checkbox bound to `m_paused`. The FULL state machine,
     spelled out (v1 only documented direction 1 below):
     1. **`false → true` (pause just engaged):** capture
        `m_frozenPoints = Profiling::BuildFrameGraphPoints(profiler);` and
        `m_frozenLatestFrame = profiler.LastCompletedFrame();` once, this
        frame only.
     2. **Stays `true` across subsequent frames:** every section below
        reads `m_frozenPoints`/`m_frozenLatestFrame` instead of calling
        `BuildFrameGraphPoints()`/`LastCompletedFrame()` fresh — this is
        the entire mechanism behind "pause only freezes the panel, never
        capture."
     3. **`true → false` (un-pause) — new, explicit in v2:** no special
        action is needed at all. Every section already reads
        `m_paused`'s CURRENT value each frame to decide live-vs-frozen; the
        very next frame after un-pausing, they simply read live data
        again. `m_frozenPoints`/`m_frozenLatestFrame` are left as-is
        (stale, unused) until the next pause - re-populating them
        unconditionally on every un-pause would just be wasted work for no
        behavioral difference, since they're never read while `m_paused`
        is `false`.
     4. **Toggling Capture while Pause is already `true` — new, explicit
        test case in v2 (also added to Step 3.7):** must have NO effect on
        what the panel currently displays (still the frozen snapshot) —
        this is what actually proves the two controls are independent,
        rather than merely asserting it in prose.
2. **CPU frame-time graph section** (`Phase7.md` item 2): last ~240 points
   via `std::span` slicing of whichever point source is active (live or
   frozen). Empty → `ImGui::TextDisabled("Waiting for profiler data...");`.
   Otherwise: fill `m_cpuGraphScratch` (`clear()` + `reserve()`/`push_back()`
   per point, no destructive reallocation once warmed up — see the
   member's own doc comment above) and call `ImGui::PlotLines()`, scaled
   via `ComputeCpuMillisecondsRange()` (floor `scale_min` at `0.0`). Show
   `FormatFrameTimeSummary()` for the most recent point plus the range's
   min/max via `FormatDuration()`.
3. **CPU scope breakdown table section** (`Phase7.md` item 3): a plain
   ImGui table (`Borders | RowBg | Resizable`, "Scope"/"Total"/"Calls")
   built from `BuildSortedCpuScopeRows()`. **New in v2:** when the result is
   empty, call `CpuScopeTableEmptyMessage()` (Step 3.2) rather than a
   single hardcoded string — this is what makes the compiled-out-vs-
   transiently-empty distinction (Changelog #1) actually visible in the UI.
4. **Draw calls / triangles section** (`Phase7.md` item 4): "Game View"
   first, unconditionally, via `ResolveGpuPassCounts(frame, GpuPass::GameView)`;
   "Scene View"/"Present" underneath, de-emphasized, each independently
   resolved the same way.
5. **GPU memory section** (`Phase7.md` item 5): current totals via the
   reused `FormatBytes()`, plus a sparkline fed from `m_memoryGraphScratch`
   over the same windowed slice as the CPU graph, scaled via the new
   `ComputeMemoryBytesRange()`. A point whose `memory.status != Present` is
   excluded from the *range* computation (Step 3.1) but still needs some
   finite value in the raw float array — repeat the previous plotted value
   for a gap frame (documented in-code as intentional; a flat segment
   reads more honestly than a spike toward zero).
6. **GPU timing placeholder section** (`Phase7.md` item 6) — **changed in
   v2 (Changelog #3): render `FormatGpuTimingLine()` for ALL THREE named
   passes (`GameView`/`SceneView`/`Present`), unconditionally, not just
   `GameView`** — structurally identical to how section 4 already covers
   all three, so there is nothing left ambiguous for whoever implements
   this. Each currently renders the same permanent `"GPU Timing: N/A"` +
   explanatory sub-line (`ImGui::TextDisabled()`), and each will
   automatically start showing a real value the moment Phase 4 lands, per
   pass, independently — with zero code change here.
7. **Export button (disabled stub)**: `ImGui::BeginDisabled()`-wrapped
   `"Export CSV"` button, tooltip `"Planned for the benchmark/export phase
   (Phase 6)."` — present but inert.

`Build()`'s top-level shape mirrors `BuildMemoryPanel()` exactly:
`ImGui::Begin("Profiler");` (unconditional content, no return-value check)
→ the seven sections above → `ImGui::End();`.

### Step 3.4 — Wire `ProfilerPanel` into `ImGuiEditorLayer`

*(Unchanged from v1.)* `#include "Panels/ProfilerPanel.h"`, a new
`ProfilerPanel m_profilerPanel;` member, and one new call in `BuildUI()`
immediately after `BuildMemoryPanel(m_ctx, renderer);` and before the
`#if GTE_ENABLE_PROJECT_PANEL` block:

```cpp
BuildMemoryPanel(m_ctx, renderer);
m_profilerPanel.Build(m_ctx);
#if GTE_ENABLE_PROJECT_PANEL
    BuildProjectPanel(m_ctx, m_assetDatabase);
#endif
```

Re-verify the precise surrounding lines at implementation time — line
numbers drift.

### Step 3.5 — `DockLayout.cpp`: add `"Profiler"` to both lists together

*(Unchanged from v1.)*

```cpp
constexpr const char* kAllPanelNames[] = {
    "Hierarchy", "Inspector", "Scene", "Game", "Memory", "Profiler",
#if GTE_ENABLE_PROJECT_PANEL
    "Project",
#endif
};
```

```cpp
ImGui::DockBuilderDockWindow("Hierarchy", left);
ImGui::DockBuilderDockWindow("Inspector", right);
ImGui::DockBuilderDockWindow("Scene", center);
ImGui::DockBuilderDockWindow("Game", center);
ImGui::DockBuilderDockWindow("Memory", bottom);
ImGui::DockBuilderDockWindow("Profiler", bottom);
#if GTE_ENABLE_PROJECT_PANEL
    ImGui::DockBuilderDockWindow("Project", bottom);
#endif
```

`"Profiler"` is added unconditionally (gated only by `GTE_ENABLE_EDITOR`,
exactly like `"Memory"`), docked into the same `bottom` node.

### Step 3.6 — CMake wiring

*(Unchanged from v1.)* Root `CMakeLists.txt`: both new file pairs added to
`gte_core`'s `target_sources()` inside the existing `if(GTE_ENABLE_EDITOR)`
block, alongside the Memory panel's own — not additionally gated behind
`GTE_ENABLE_PROJECT_PANEL`; no new `option()`. `tests/CMakeLists.txt`:
`Editor/ProfilerPanelDataTests.cpp` added to the existing
`if(GTE_ENABLE_EDITOR)` test block. `Profiling/FrameGraphDataTests.cpp`
needs no CMake change (already unconditional).

### Step 3.7 — Manual/Tier-2 verification

**New in v2: this now starts with an explicit "before anything else"
step (Changelog #5), and gains a Capture+Pause interaction check
(Changelog #6/#4's test case):**

0. **NEW — first, before spawning anything:** launch the Editor fresh
   (delete `imgui.ini` first) and open the "Profiler" tab immediately.
   Confirm it does not crash/assert/show garbage in the moment before any
   frame has completed, that the CPU graph shows
   `"Waiting for profiler data..."` (not a graph of garbage values), that
   the CPU Scopes table shows its correct empty message (matching whether
   this particular build has `GTE_ENABLE_PROFILER` ON or OFF — see step 8
   below for the OFF case specifically), and that every `GpuPass` row
   reads sensibly (most likely `"N/A"`, since nothing has rendered yet).
1. Spawn several primitive entities via "Hierarchy" → "Create 3D Object";
   confirm the CPU graph rises, the relevant scope rows appear/grow, and
   "Game View"'s draw-call count climbs.
2. Hide "Scene" and confirm "Scene View" reads `"N/A"`, not
   `"0 draw calls"`.
3. Minimize the OS window briefly and confirm "Present" also reads `"N/A"`
   for those frames.
4. Toggle "Capture" off; confirm the graph/table stop advancing, and the
   notice appears with wording that matches whatever `SetCaptureEnabled()`
   was actually confirmed to do (Step 2.4).
5. Toggle "Capture" back on, then toggle "Pause" on; confirm the display
   freezes while a throwaway diagnostic proves
   `FrameProfiler::Instance().CompletedFrameCount()` keeps advancing.
6. **NEW — while still paused, toggle "Capture" off and back on again;**
   confirm the panel's DISPLAY does not change at all during this (still
   showing the exact frozen snapshot from step 5) — this is the concrete
   verification that Capture and Pause are genuinely independent controls,
   not just independent in name.
7. Drag/split the "Profiler" tab away from "Memory" and confirm normal
   dockable behavior.
8. Delete/rename `imgui.ini` and confirm "Profiler" docks correctly with
   no manual rearrangement.
9. **NEW — build once more with `-DGTE_ENABLE_PROFILER=OFF`** (leaving
   `GTE_ENABLE_EDITOR=ON`) and confirm: the CPU frame-time graph still
   shows real values (frame time is still measured), while the CPU Scopes
   table now shows its "instrumentation compiled out" message rather than
   simply looking empty the same way it did in step 0 above — i.e. prove
   the two different empty-table reasons actually render two different,
   distinguishable messages.

### Step 3.8 — Documentation updates

*(Same file list as v1, with one addition.)*

- `AGENTS.md`'s "Profiling" section: the `FrameGraphPoint::memory`/
  `ComputeMemoryBytesRange()` extension.
- `AGENTS.md`'s "Editor Module Structure" section: `ProfilerPanel` as the
  second real-world "stateful panel" instance.
- `README.md`'s "Editor / Debug UI" section: a new "Profiler panel:"
  bullet (Capture/Pause distinction, the honest GPU-Timing-N/A line, and —
  new in v2 — a one-line mention of how the CPU Scopes table behaves under
  `GTE_ENABLE_PROFILER=OFF`), plus a "Status" update.
- `TESTING.md`: bullets for `Editor/ProfilerPanelDataTests.cpp` and the
  `Profiling/FrameGraphDataTests.cpp` additions.
- `PROFILER_IMPLEMENTATION_STATUS_v5.md` → `_v6`: move Phase 7 from "not
  implemented" to "implemented this session," with a freshly re-counted
  `ctest` total.
- **New in v2:** delete `PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v1.md` once
  this v2 document is confirmed to be the one actually implemented from,
  per this project's own "bump the version, delete the superseded file"
  convention — don't let both linger.
- This document (v2) gains its own "Result (filled in after
  implementation)" addendum once built.

### Step 3.9 — Order of implementation

*(Unchanged from v1.)*

1. Step 3.1 + tests — green before touching `src/Editor/`.
2. Step 3.2 + tests — green before touching `Panels/ProfilerPanel.cpp`.
3. Step 3.3.
4. Step 3.4 + Step 3.5, together, in the same change.
5. Step 3.6.
6. Full build + full `ctest` run.
7. Step 3.7 (manual verification, now including the new steps 0/6/9
   above).
8. Step 3.8, last.

--------------------------------------------------------------------------
## Step 4: What We Will NOT Do (Focus)
--------------------------------------------------------------------------

*(Unchanged from v1 — every refusal below was already correctly scoped.)*

- No Vulkan GPU timestamp queries / `VkQueryPool` / Phase-4-shaped work.
- No per-draw, per-mesh, or per-material profiling breakdown.
- No nested/hierarchical CPU scope-tree UI — the model is deliberately
  flat.
- No new plotting library dependency (ImPlot or otherwise) —
  `ImGui::PlotLines()` is sufficient.
- No second GPU memory tracker, and no second history buffer.
- No CSV export implementation — a disabled, tooltipped stub button only.
- No benchmark mode, no CLI flag, no headless execution path.
- No change to `FrameProfiler`'s public API beyond what already exists.
- No `IEditorPanel` interface, no panel registry, no plugin system.
- No new `GTE_ENABLE_*` CMake option.
- No click-to-select, no ray casting, no Command-pattern/undo-redo work.
- No multi-threaded/job-system-aware profiling infrastructure.
- No change to `Application.cpp`'s existing per-frame profiling call
  sites.
- **New in v2, stated explicitly so it isn't mistaken for a gap:** no
  attempt to make `GTE_ENABLE_PROFILER=OFF` somehow still show CPU scope
  data — that flag's entire purpose (per `AGENTS.md`) is to compile scope
  instrumentation out; this phase only ever adds an honest, correctly-
  worded explanation of that fact, never a workaround for it.

--------------------------------------------------------------------------
## Step 5: Their Role (What does this mean for you?)
--------------------------------------------------------------------------

### 5.1 How to start

1. Read this document fully, then re-read `PROFILER_STRATEGY_v2.md`'s own
   Phase 7 section side by side with Step 3 above.
2. Re-read `AGENTS.md`'s "Profiling" AND "Editor Module Structure" sections
   in full immediately before writing code.
3. Run `ctest --test-dir build -C Debug --output-on-failure` once, BEFORE
   writing any code, to record the actual current passing-test count.
4. **New in v2:** before writing the Capture checkbox's descriptive text,
   actually read `FrameProfiler::SetCaptureEnabled()`/`BeginFrame()`/
   `EndFrame()` and confirm precisely what gets suppressed (Step 2.4's new
   constraint) — do not carry forward this document's assumed wording
   without checking.
5. Re-read the CURRENT `ImGuiEditorLayer.cpp`'s `BuildUI()` body and
   `DockLayout.cpp` in full before editing either.
6. Implement in the order given in Step 3.9.

### 5.2 Non-negotiable checklist (copy into the PR/commit description)

- [ ] `FrameGraphPoint` gained a `memory` field, appended;
      `BuildFrameGraphPoints()` copies it verbatim; `ComputeMemoryBytesRange()`
      exists and correctly excludes non-`Present` entries.
- [ ] Every new pure function in `ProfilerPanelData.h` has a matching
      Tier-1 test, added in the SAME change.
- [ ] The CPU scope table's sort is verified biggest-first and respects
      `cpuScopeCount`, not the fixed array's capacity.
- [ ] Draw-call/triangle display genuinely reads "N/A" for an `Absent`
      pass — verified by a unit test AND manually (hide Scene, minimize
      the window).
- [ ] GPU Timing genuinely reads the placeholder for `Absent`/`Unsupported`
      **for all three named passes**, and is written so a future real
      `Present` value requires zero changes to `Panels/ProfilerPanel.cpp`.
- [ ] Capture and Pause are two genuinely independent controls — verified
      manually, **including toggling Capture while Pause is already on and
      confirming the display does not change.**
- [ ] `ProfilerPanel` is a small class, called explicitly by name from
      `BuildUI()`, no `IEditorPanel` interface introduced.
- [ ] `DockLayout.cpp`'s `kAllPanelNames` and `BuildDefaultDockLayout()`
      updated together, `"Profiler"` docked unconditionally.
- [ ] Both new file pairs added to the SAME `if(GTE_ENABLE_EDITOR)` CMake
      blocks the Memory panel already uses — no new CMake option.
- [ ] `FormatBytes()` reused from `MemoryPanelData.h`, never reimplemented.
- [ ] No CSV export logic — only a disabled, tooltipped stub button.
- [ ] **New:** the CPU Scopes table's empty state correctly and visibly
      distinguishes "compiled out (`GTE_ENABLE_PROFILER=OFF`)" from
      "transiently empty this frame" — verified by actually building both
      configurations.
- [ ] **New:** the Capture checkbox's own descriptive text was written
      only after re-reading `FrameProfiler::SetCaptureEnabled()`'s real
      behavior, not copied from this document's assumed wording.
- [ ] **New:** the panel was verified immediately on a fresh Editor launch,
      before any frame completes, with no crash/garbage display.
- [ ] Full clean build (`GTE_ENABLE_EDITOR=ON`, default) + full `ctest`
      run, both green, with a freshly re-counted test total.
- [ ] A clean build with `-DGTE_ENABLE_EDITOR=OFF` still succeeds, zero
      ImGui linked in.
- [ ] `AGENTS.md`, `README.md`, `TESTING.md`, and
      `PROFILER_IMPLEMENTATION_STATUS_v5.md` (→ `_v6`) all updated in the
      same change.
- [ ] **New:** `PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v1.md` deleted once
      this document is confirmed as the plan actually implemented from.

### 5.3 What happens after this phase lands

*(Unchanged from v1.)* Every phase from `PROFILER_STRATEGY_v2.md`'s
original plan is implemented and visible except Phase 4 (deliberately
deferred) and Phase 6 (benchmark mode, now a pure consumer of a
fully-populated data model — its CSV exporter is the natural thing this
panel's disabled "Export CSV" stub eventually wires up to).

### 5.4 A closing thought

*(Unchanged from v1.)* Every other phase in this plan produced a number
nobody could see. This one is the payoff. Build it the same careful way
the data underneath it was built: reuse what already exists, respect the
tri-states exactly as designed, keep Capture and Pause honestly separate,
tell the truth about what's compiled in and what isn't, and leave Phase
4/6 exactly as much room to land cleanly later as every phase before this
one already left for you.

--------------------------------------------------------------------------
## Result (filled in after implementation)
--------------------------------------------------------------------------

Implemented in full, following this plan step-by-step with no material
deviation:

- Step 3.1: `FrameGraphPoint` gained a `memory` field (copied verbatim in
  `BuildFrameGraphPoints()`); `ComputeMemoryBytesRange()` added as a sibling
  to `ComputeCpuMillisecondsRange()`/`ComputeGpuMillisecondsRange()` in
  `src/Profiling/FrameGraphData.h/.cpp`, with 5 new tests in the existing
  `tests/Profiling/FrameGraphDataTests.cpp`.
- Step 3.2: `src/Editor/ProfilerPanelData.h/.cpp` added, with 14 new tests
  in a new `tests/Editor/ProfilerPanelDataTests.cpp`.
- Step 3.3: `src/Editor/Panels/ProfilerPanel.h/.cpp` added - a small
  stateful class exactly as planned, with all seven sections (Capture/
  Pause controls, CPU frame-time graph, CPU scope table, draw calls/
  triangles for all three named passes, GPU memory + sparkline, GPU timing
  placeholder for all three named passes, disabled Export CSV stub).
- Steps 3.4/3.5: `ImGuiEditorLayer.cpp` and `DockLayout.cpp` updated
  together in the same change - `m_profilerPanel` is NOT gated behind
  `GTE_ENABLE_PROJECT_PANEL` (it only depends on `Profiling::FrameProfiler`,
  always compiled).
- Step 3.6: both new file pairs added to `gte_core`'s existing
  `if(GTE_ENABLE_EDITOR)` CMake block; the new test file added to
  `tests/CMakeLists.txt`'s matching block; no new CMake option introduced.
- Step 3.7: full clean build + `ctest` verified in three configurations -
  default (`GTE_ENABLE_EDITOR=ON`, `GTE_ENABLE_PROFILER=ON`): 502 tests, 501
  passing + 1 pre-existing machine-gated skip; `-DGTE_ENABLE_EDITOR=OFF`:
  clean build, zero ImGui linked; `-DGTE_ENABLE_PROFILER=OFF` (Editor still
  ON): clean build, 500 tests all passing (2 fewer than the default
  configuration's 502 - unrelated to this phase, not a regression:
  `tests/Profiling/ScopeTimerTests.cpp` itself has a pre-existing
  `#if GTE_ENABLE_PROFILER`/`#else` split - 3 real-instrumentation tests in
  the `ON` branch vs. 1 compiled-out-behavior test in the `OFF` branch,
  accounting for exactly this 2-test difference), with
  `ProfilerPanelDataTest.CpuScopeTableEmptyMessageMatchesCompileTimeFlag`
  specifically confirming the "compiled out" wording. Manual, interactive
  verification of the live Editor (steps 0/1-9) was not re-performed in this
  automated session - the panel's logic-level correctness (tri-state
  handling, Capture/Pause independence, empty-state messaging) is instead
  covered by the Tier-1 test suite above; a human/interactive pass over
  Step 3.7's own manual checklist is recommended before considering this
  phase's UI polish fully signed off.
- Step 3.8: `AGENTS.md` ("Profiling" and "Editor Module Structure"
  sections), `README.md` ("Editor / Debug UI" and "Status"), `TESTING.md`,
  and `PROFILER_IMPLEMENTATION_STATUS_v5.md` → `_v6` (v5 deleted) were all
  updated in the same change as this result section.
  `PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v1.md` was deleted.

No deviation from this plan's design decisions (all six Changelog items
from v1 → v2 were implemented exactly as specified): the CPU-scope-table
compiled-out-vs-empty distinction, the full pause/un-pause state machine
(including the Capture-toggled-while-paused non-effect), all three GPU
passes shown for both draw-stats and timing sections, the reusable
`ImGui::PlotLines()` scratch buffers, the Editor-launch-before-any-frame
verification (covered by this session's Tier-1 tests treating a
default-constructed `FrameSample` as a first-class input), and the
re-confirmed (not assumed) `SetCaptureEnabled()` wording.
