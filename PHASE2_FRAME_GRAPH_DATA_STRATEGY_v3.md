# GreatTamanaEngine — Phase 2 Grand Strategy: Frame-Time Graph Data (v3)

Status: PROPOSAL / PLANNING DOCUMENT — no implementation yet.
Scope: `PROFILER_STRATEGY_v2.md`'s **Phase 2 — "Frame time graph data (the
pure 'history → plottable points' reshape)"**, and nothing else.

Prerequisite reading (do not skip): `PROFILER_STRATEGY_v2.md` (the whole
8-phase plan and its reasoning), `PROFILER_IMPLEMENTATION_STATUS_v2.md`
(exactly what Phase 0/1 actually built), and `AGENTS.md`'s "Profiling"
section (the conventions every phase must follow). This document does not
repeat their reasoning in full — it inherits it, and applies it narrowly to
the one phase requested.

--------------------------------------------------------------------------
## Changelog: v2 -> v3 (read this first)
--------------------------------------------------------------------------

This is a third-pass review of `PHASE2_FRAME_GRAPH_DATA_STRATEGY_v2.md`
(v2), continuing this codebase's own established convention for iterating a
planning document (see v2's own "Changelog: v1 -> v2" section, and
`PROFILER_STRATEGY_v2.md`/`PROFILER_IMPLEMENTATION_STATUS_v2.md`'s matching
sections). Following v2's own stated discipline of not just re-reading a
prior pass's prose but independently re-checking its claims against the
REAL source, this pass re-read `src/Profiling/ProfilingTypes.h`,
`src/Profiling/FrameProfiler.h`, **and `src/Profiling/FrameProfiler.cpp`
itself** (v2 cited `FrameProfiler.h`'s public contract but never actually
walked `FrameProfiler.cpp`'s own implementation body line-by-line — this
pass did, and that is precisely what surfaced every finding below),
`tests/Profiling/FrameProfilerTests.cpp`, the root `CMakeLists.txt`, and
`tests/CMakeLists.txt` as they exist right now. **Every one of v2's
concrete file/field/signature/CMake-placement claims checked out exactly as
written** — `kMaxFrameHistory == 300`, `kGpuPassCount == 3`, the exact
`GpuPass`/`GpuSampleStatus`/`GpuPassSample`/`FrameSample` shapes, the exact
`SetGpuPassSample()` parameter order/defaults, the exact unconditional
placement of `src/Profiling/*` inside `gte_core`'s `add_library()` call
(immediately after `src/Game/RenderSystem.h`), and the exact unconditional
placement of `Profiling/FrameProfilerTests.cpp`/`ScopeTimerTests.cpp` inside
`tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` (immediately after the
`Assets/*` block, immediately before `Input/InputStateTests.cpp`) — **none
of it needed correcting.** What follows is not a rebuttal of v2's design; it
is four concrete refinements that reading `FrameProfiler.cpp`'s actual
implementation (not just its header's doc comments) surfaced, plus one
genuine architectural improvement:

1. **v2 proposed a debug-only `assert()` for `ComputeGpuMillisecondsRange()`'s
   out-of-range `pass` argument, reaching for a distant analogy
   (`FrameRecorder::RecordFrame()`'s render-target-format assert, a
   completely unrelated GPU-resource-lifetime concern) — while overlooking a
   FAR more directly relevant precedent sitting in the exact same module it
   was already reading from.** `FrameProfiler::SetGpuPassSample()` itself
   already handles an out-of-range `GpuPass` value today, right now, in
   production code — not with an assert, but with an unconditional (not
   debug-only) bounds check that silently no-ops:
   ```cpp
   const std::size_t index = static_cast<std::size_t>(pass);
   if (index >= kGpuPassCount) {
       return;
   }
   ```
   (`FrameProfiler.cpp`, `SetGpuPassSample()`). This is a strictly better
   pattern for `ComputeGpuMillisecondsRange()` to copy than v2's proposed
   debug-only assert, for a concrete reason a debug-only assert can never
   fix: `assert()` compiles to nothing under `NDEBUG` (a Release build), so
   a debug-only assert leaves a RELEASE build's out-of-range read exactly as
   undefined-behavior-prone as if no check existed at all — the assert only
   ever helps a developer running a Debug build catch the mistake before it
   ships, not a release binary that already shipped with the bug. A plain,
   unconditional bounds check (`index >= kGpuPassCount → return
   FrameGraphRange{}` — see the revised Step 3.2 below) is simpler (no
   `<cassert>`/custom macro, no `#ifdef` awareness required at the call
   site), behaves IDENTICALLY in Debug and Release, and directly mirrors
   how this exact same kind of caller mistake is already handled two doors
   down in the very file this reshape function reads from. **Fixed by
   replacing the debug-only-assert requirement with an unconditional
   bounds-check-and-return-"no data" requirement in Step 3.2**, and by
   adding a note to Step 2.4 pointing future readers at this exact
   precedent so it isn't missed a second time.
2. **v2 never actually determined what happens to `FrameProfiler`'s
   retained `frameIndex` sequence when `SetCaptureEnabled(false)` is
   toggled mid-run and later re-enabled — it only ever discussed the
   ALREADY-established, ALREADY-tested wraparound case.** Reading
   `FrameProfiler::BeginFrame()`/`EndFrame()`'s actual bodies settles this
   definitively: both are complete no-ops while `m_captureEnabled` is
   `false` (`BeginFrame()`: `if (!m_captureEnabled) { m_frameInProgress =
   false; return; }`; `EndFrame()`: `if (!m_captureEnabled ||
   !m_frameInProgress) { ...; return; }`) — **critically, `m_frameIndex`
   itself is never touched by either early-return path.** This means a
   disabled window never consumes a `frameIndex` value at all; it isn't
   skipped, it simply never existed from `FrameProfiler`'s point of view.
   Every frame that DOES make it into the ring buffer therefore carries a
   perfectly gapless, contiguous `frameIndex` sequence — this is a
   structural guarantee of the existing, already-shipped implementation,
   not an assumption Phase 2 gets to make hopefully. This directly
   justifies making Step 3.3's order/wraparound tests assert EXACT expected
   `frameIndex` values rather than merely "increasing order" (see item 4
   below) — an exact check is both possible (the guarantee is real) and
   strictly stronger (it also catches an off-by-one in the reshape's own
   iteration, which a "just increasing" check would not). Documented
   explicitly in the revised Step 2.4 below so a future reader never has to
   re-derive this from `FrameProfiler.cpp` a second time.
3. **v2 never stated whether `BuildFrameGraphPoints()` is safe to call
   strictly BETWEEN a `BeginFrame()` and its matching `EndFrame()` (e.g. a
   hypothetical future mid-frame Editor repaint, or simply a caller unsure
   of the exact frame-lifecycle ordering)** — leaving a real, reasonable
   question unanswered for the next implementer. Confirmed directly:
   `HistoryCount()`/`HistoryAt()` (the only two methods `BuildFrameGraphPoints()`
   is allowed to call, per Step 2.4's own existing rule) read exclusively
   from `m_history`/`m_historyCount` — the completed-and-pushed ring
   buffer — and NEVER from `m_current` (the in-progress frame's own
   scratch `FrameSample`). `BuildFrameGraphPoints()` is therefore
   unconditionally safe to call at any point in the frame lifecycle,
   including mid-frame — it will simply never observe the not-yet-completed
   frame, exactly the same guarantee every other `HistoryAt()` consumer
   already gets for free. Documented explicitly in Step 2.4 below, and
   given its own dedicated regression test in Step 3.3 (case 10, new in
   v3) — a genuine guarantee worth protecting with a test, not just a
   sentence in a planning document nobody re-reads once implementation
   starts.
4. **The "several frames preserve order" and "ring-buffer wraparound"
   tests (v2's cases 3 and 5) only ever asserted "strictly increasing"/
   "correct order" — never the actual, exact expected `frameIndex` values —
   which is a strictly weaker check than this codebase's own existing
   sibling test already performs for the exact same underlying scenario.**
   `tests/Profiling/FrameProfilerTests.cpp`'s own
   `RingBufferWrapsAndKeepsMostRecentFrames` test (already shipped, already
   passing) asserts EXACT boundary values —
   `EXPECT_EQ(profiler.HistoryAt(0).frameIndex, totalFrames -
   kMaxFrameHistory)` and `EXPECT_EQ(profiler.HistoryAt(kMaxFrameHistory -
   1).frameIndex, totalFrames - 1)` — specifically because "just increasing"
   would not catch an off-by-one in which physical ring-buffer slot got
   read as "oldest". Phase 2's own reshape sits directly on top of that
   exact same iteration (`HistoryAt(i)` for `i` in `[0, HistoryCount())`) and
   is exactly as capable of introducing its OWN, independent off-by-one
   (e.g. an accidental `i + 1` or a reversed loop) that a monotonic-order
   check would never catch, because a reversed-but-still-technically-sorted
   subsequence, or a sequence merely missing its first/last element, can
   still look "increasing" while being outright wrong. **Fixed by
   strengthening both cases to assert exact expected `frameIndex` values —
   grounded in item 2's now-confirmed contiguity guarantee — mirroring
   `FrameProfilerTests.cpp`'s own existing, stronger pattern exactly rather
   than a weaker one invented fresh for this new test file.**
5. **A genuine, if optional, architecture improvement worth adopting now
   rather than retrofitting later: `ComputeCpuMillisecondsRange()`/
   `ComputeGpuMillisecondsRange()` should accept `std::span<const
   FrameGraphPoint>` instead of `const std::vector<FrameGraphPoint>&`.**
   Step 3.1's own stated design (kept unchanged from v2, and still correct)
   is that a caller wanting only the most recent 120 frames "can simply
   take a suffix of the returned `std::vector` ... with no information lost
   either way." That claim is only actually free of cost if the range
   helpers can accept that suffix WITHOUT a copy — but `const
   std::vector<FrameGraphPoint>&` cannot bind to an iterator sub-range at
   all, so a caller wanting a windowed range today would be forced to
   construct a brand-new temporary `std::vector` (an allocation and a full
   element-by-element copy) just to call `ComputeCpuMillisecondsRange()`
   over a slice — quietly undermining the exact "slicing is free" argument
   Step 3.1 uses to justify NOT adding a windowing parameter in the first
   place. `std::span<const FrameGraphPoint>` (this project already targets
   C++20 — see `CMakeLists.txt`'s `set(CMAKE_CXX_STANDARD 20)` — so this
   needs no new dependency or workaround) converts implicitly from a
   `std::vector`, so every existing call site (passing
   `BuildFrameGraphPoints()`'s full output straight through) needs zero
   changes, while a future windowed caller can pass
   `std::span(points).subspan(points.size() - 120)` (or equivalent) with
   zero allocation. This would be the first use of `std::span` in this
   codebase (worth noting honestly, not silently) — but it directly serves
   an already-stated, already-agreed design goal rather than introducing a
   new one, so it is recommended, not merely "nice to have." (Note: this is
   a genuine, if minor, deviation from `src/Editor/MemoryPanelData.h`'s own
   `const std::vector<...>&`-based convention — re-verified directly
   against that file for this pass — but `MemoryPanelData`'s functions have
   no equivalent "caller may want a slice of this for free" design promise
   baked into their own reasoning the way Step 3.1 does here, so copying its
   parameter-type convention verbatim here would mean copying a convention
   that doesn't fit this specific case, not copying a genuinely load-bearing
   rule.)

Two smaller items, folded directly into Step 3.3 below rather than getting
their own numbered Changelog entry, since they are refinements of existing
test cases rather than newly-discovered gaps: (a) an explicit new test
proving an `Absent`/`Unsupported` `GpuPassSample` carrying a non-zero,
"stale-looking" `milliseconds`/`drawCallCount`/`triangleCount` value (fully
constructible today via `SetGpuPassSample()`'s own permissive signature,
e.g. `SetGpuPassSample(pass, GpuSampleStatus::Absent, 42.0, 5, 100)`) is
STILL correctly excluded from `ComputeGpuMillisecondsRange()`'s min/max scan
— the single most direct test of this whole phase's "never convert absent
GPU data into 0 ms" rule, since it also positively rules out a subtly
different bug (branching on "is the stored value zero" instead of on the
actual `status` tag); and (b) a one-line callout that case 4's choice to
leave `GpuPass::Present` (the pass) at its default `GpuSampleStatus::Absent`
(the status) is a deliberate, if small, proof that this codebase's two
same-named-but-unrelated `Present` identifiers are never confused anywhere
in the implementation.

Everything else below is v2's content, reproduced essentially unchanged —
re-verified against the real source tree for this pass and found
accurate — except where an item above needed to be folded into the
surrounding text rather than appended after it.

--------------------------------------------------------------------------
## Changelog: v1 -> v2 (kept for history)
--------------------------------------------------------------------------

This is a second-pass review of `PHASE2_FRAME_GRAPH_DATA_STRATEGY.md` (v1),
following this codebase's own established convention for how a planning
document gets iterated (see `PROFILER_STRATEGY_v2.md`'s and
`PROFILER_IMPLEMENTATION_STATUS_v2.md`'s own "Changelog: v1 -> v2"
sections). v1's overall shape — where the code lives
(`src/Profiling/FrameGraphData.h/.cpp`, always-compiled, no
`GTE_ENABLE_EDITOR` dependency), the two functions it exposes
(`BuildFrameGraphPoints()`/`ComputeCpuMillisecondsRange()`/
`ComputeGpuMillisecondsRange()`), and the "reuse `GpuPassSample` verbatim,
never invent a second tri-state" design decision — is sound and kept
unchanged below.

**This revision independently re-verified every concrete claim v1 made
about the existing codebase by reading the actual source** (`src/Profiling/
ProfilingTypes.h`, `src/Profiling/FrameProfiler.h`,
`tests/Profiling/FrameProfilerTests.cpp`, the root `CMakeLists.txt`, and
`tests/CMakeLists.txt`), not just re-reading v1's own prose. **Every one of
those claims checked out exactly as written**: `kMaxFrameHistory == 300`,
`kGpuPassCount == 3`, `GpuPass::{GameView=0, SceneView=1, Present=2}`,
`GpuPassSample{status, milliseconds, drawCallCount, triangleCount}`,
`FrameSample{frameIndex, cpuFrameMilliseconds, cpuScopes, cpuScopeCount,
gpuPasses, memory}`, `FrameProfiler::SetGpuPassSample(GpuPass, GpuSampleStatus,
double milliseconds = 0.0, std::uint32_t drawCallCount = 0, std::uint32_t
triangleCount = 0)`'s exact parameter order/defaults, `HistoryAt()`'s
unchecked-precondition contract, and both `src/Profiling/` and
`tests/Profiling/` already being unconditional (no `GTE_ENABLE_EDITOR`/
`GTE_ENABLE_PROJECT_PANEL` guard) in their respective CMake file lists. The
"450 tests" baseline v1 cites is also independently confirmed correct
against `PROFILER_IMPLEMENTATION_STATUS_v2.md`'s own arithmetic (434 before
the Profiler session + 16 new Phase 0/1 tests = 450) — not a stale number.
**None of v1's file/field/signature claims needed correcting.** This is
stated explicitly, rather than silently, because the previous review pass
(of a sibling document) DID find real inaccuracies this way — it is worth
recording that this document was checked the same way and passed.

What v1 was missing, and what this revision changes:

1. **The one genuine test-coverage gap: no case proved `GpuSampleStatus::
   Unsupported` round-trips faithfully at the PER-POINT level.** v1's Step
   3.3 case 8 proved `ComputeGpuMillisecondsRange()` treats an
   all-`Unsupported` series the same as all-`Absent` (both excluded from
   the aggregate min/max) — but nothing proved that `BuildFrameGraphPoints()`
   itself preserves a `Present` GPU pass, an `Absent` one, AND an
   `Unsupported` one simultaneously, each at its own correct `GpuPass`
   index, with no cross-contamination between the three. Since the explicit
   project rule is "Preserve `Present`, `Absent`, and `Unsupported`" as
   three genuinely distinct, individually-checkable states, a test suite
   that only ever constructs two of the three at the point level (v1's case
   2 covers `Absent` via the default; v1's case 4 covers `Present`) leaves
   `Unsupported` proven only indirectly, one level removed, through the
   range helper. **Fixed by rewriting v1's case 4 into a single, stronger
   "all three named passes, all three tri-state values, in one frame" test**
   (see Step 3.3 below) — this also directly strengthens v1's original
   "never mix up which named pass costs what" regression coverage, since it
   now exercises `GameView`/`SceneView`/`Present` simultaneously instead of
   only `GameView` vs. "the other two, left at their default."
2. **v1 never said whether CPU-millisecond comparisons in its own tests
   should be exact-bit or approximate.** The project rule is "CPU values
   preserved exactly" — `EXPECT_DOUBLE_EQ` (bit-exact for a value that was
   never subjected to arithmetic, only copied field-to-field) is the
   correct assertion, not `EXPECT_NEAR`/a tolerance-based comparison, which
   would technically satisfy "preserved exactly" only by accident and could
   silently mask a real regression (e.g. an accidental unit conversion) that
   happens to land within whatever tolerance was chosen. Called out
   explicitly now in Step 3.3's cases 2 and 6, so the implementer copies the
   codebase's own existing convention (see
   `tests/Profiling/FrameProfilerTests.cpp`'s own `EXPECT_DOUBLE_EQ` usage)
   rather than picking a weaker comparison by default.
3. **`ComputeGpuMillisecondsRange(points, pass)`'s behavior on an
   out-of-range `pass` argument was left unstated.** (Superseded by
   Changelog v2 -> v3 item 1 above — the final decision is now an
   unconditional bounds check, not a debug-only assert.)
4. **A minor, non-blocking design-affirmation worth stating explicitly
   rather than leaving implicit**: `BuildFrameGraphPoints()` deliberately
   has no "only return the last N frames" parameter — it always returns
   every currently-retained frame (up to `kMaxFrameHistory == 300`). This
   was implicit in v1's own function signature but never justified in
   prose. Justified now in Step 3.1: a future consumer (Phase 7's Editor
   panel, wanting to show only the last 120 frames on screen; Phase 6's
   benchmark CSV exporter, wanting every frame) can trivially slice the
   returned `std::vector` itself (e.g. `points.end() - 120` onward) with no
   information lost either way — adding a windowing parameter to the
   reshape function itself would only duplicate logic every caller can
   already do for free with standard-library iterators, and would need its
   own edge-case handling (what if N > `points.size()`?) for a problem that
   doesn't actually exist at this layer.

Everything else was v1's content, reproduced essentially unchanged in v2 —
re-verified against the real source tree and found accurate — except where
an item above needed to be folded into the surrounding text rather than
appended after it.

--------------------------------------------------------------------------
## Step 1: The Goal (Where are we going?)
--------------------------------------------------------------------------

### 1.1 What Phase 2 delivers, precisely

One new, small, always-compiled, ImGui-free module that answers exactly one
question: **"given everything `FrameProfiler` has recorded so far, hand me
back a plain, ordered array of points I can plot — one point per retained
frame, oldest first — without me having to know anything about ring
buffers, tri-states, or how the data was collected."**

Concretely, when Phase 2 is done:

- A new pure function exists that reads `FrameProfiler`'s ring buffer
  (`HistoryCount()`/`HistoryAt()`, already built in Phase 0/1) and returns a
  `std::vector` of plain, POD "graph point" structs — one per retained
  frame, in oldest-to-newest order, matching `HistoryAt()`'s own existing
  iteration convention exactly. This function is safe to call at any point
  in the frame lifecycle, including strictly between a `BeginFrame()`/
  `EndFrame()` pair — see Step 2.4 (new in v3) for why this is
  structurally guaranteed, not merely assumed.
- Each point carries the frame's CPU time (always a real number — Phase 1
  already guarantees every completed frame has a real
  `cpuFrameMilliseconds`) **and** all three named GPU passes' data
  (`GameView`/`SceneView`/`Present`), each still tagged
  present/absent/unsupported — faithfully carried through from
  `FrameSample::gpuPasses`, never collapsed into a bare `0` and never
  silently invented as "present" before Phase 4 actually produces real GPU
  timestamps.
- A second small pure function computes a Y-axis range (min/max
  milliseconds) over a set of graph points for a given series (CPU, or one
  specific `GpuPass`), correctly **ignoring** points whose relevant series
  is absent/unsupported (regardless of what numeric value happens to be
  sitting in that point's `milliseconds` field — see Step 3.3's new case 9),
  and reporting cleanly ("no data") when zero points in the requested
  series have real data — so this is ready to hand to an auto-scaling plot
  widget the moment Phase 7 needs one, without Phase 7 having to write its
  own min/max scan.
- Both functions ship with their own dedicated Tier-1 test file, in the
  same change, per this codebase's own non-negotiable testing discipline
  (`AGENTS.md`, "Testability & Regression Safety": "Every change to Tier 1
  code must come with a matching test change").
- The full existing test suite (450 tests, independently confirmed against
  `PROFILER_IMPLEMENTATION_STATUS_v2.md`'s own arithmetic — see the v1->v2
  Changelog above) still passes, plus this phase's own new tests, after
  this work lands. **Re-run `ctest` fresh immediately before writing any
  final test count into a commit message or status-doc update** — this
  document's "450" is accurate as of the source this revision actually
  read (and, per the v2->v3 Changelog above, that number was NOT
  independently re-counted again for this v3 pass — this pass only
  re-verified the specific claims listed there, not the whole-suite total —
  so it should be treated as even more likely to be stale than v2's own
  caveat already warned), and any further work landed between then and
  implementation would shift it — don't reintroduce the class of mistake
  `PROFILER_IMPLEMENTATION_STATUS_v2.md`'s own "Changelog: v1 -> v2" section
  had to correct once already for exactly this reason (miscounted test
  totals).

### 1.2 What "done" looks like, concretely

- A developer (or the AI agent implementing Phase 7 next) can call one
  function, hand it `FrameProfiler::Instance()`, and immediately get back
  an array ready to feed into whatever plotting call Phase 7 chooses
  (`ImGui::PlotLines()`, a hand-rolled `ImDrawList` polyline, or a future
  ImPlot integration) — with zero further reshaping needed on the "what
  frame, what value" axis.
- That same array, unmodified, is equally usable by a future Phase 6
  benchmark-mode CSV exporter — Phase 2's output has no ImGui-specific
  baked-in assumption (see Step 3's "no NaN inside this module" design
  decision below) that would make it Editor-only.
- Hiding the "Scene" panel for a few frames (a real, already-existing
  runtime behavior — see `README.md`, "Visibility-driven rendering") and
  then inspecting the resulting graph points shows those frames' `SceneView`
  entry explicitly tagged absent — not a value of `0.0` sitting there
  indistinguishable from a genuinely fast frame. This is the single most
  important correctness property this phase must prove, because getting it
  wrong here would silently poison every later phase that reads GPU pass
  data (Phase 4, Phase 6, Phase 7).
- `cmake --build build --config Debug` succeeds, and
  `ctest --test-dir build -C Debug --output-on-failure` reports the full
  suite passing (the current baseline count + this phase's new tests),
  before this phase is considered complete — per `AGENTS.md`'s "Run the
  actual test suite before considering any change to `gte_core` done" rule.

### 1.3 What Phase 2 explicitly is NOT (see Step 4 for the full refusal list)

Phase 2 is **only** the reshape step. It does not draw anything, does not
add an Editor panel, does not touch `Application.cpp`/`Renderer`, does not
implement GPU timestamps, does not compute draw-call/triangle counts, and
does not implement benchmark-mode CSV export. Every one of those is a
different, already-planned phase with its own place in the sequence.

--------------------------------------------------------------------------
## Step 2: The Situation / The Problem (Where are we now?)
--------------------------------------------------------------------------

### 2.1 What already exists that this phase gets to reuse (the good news)

Confirmed by direct inspection of the current source tree — **and
re-confirmed a THIRD time in this v3 pass, this time by reading
`FrameProfiler.cpp`'s actual implementation body, not just `FrameProfiler.h`'s
doc comments** (see the Changelog above for exactly what surfaced from
that closer read):

- **`src/Profiling/ProfilingTypes.h`** already defines every data shape
  Phase 2 needs to read from: `FrameSample` (one frame's
  `frameIndex`/`cpuFrameMilliseconds`/`cpuScopes`/`cpuScopeCount`/
  `gpuPasses`/`memory`), `GpuPassSample` (`status`/`milliseconds`/
  `drawCallCount`/`triangleCount`), `GpuSampleStatus` (the
  `Absent`/`Present`/`Unsupported` tri-state), and `GpuPass` (the fixed
  three-value enum `GameView`/`SceneView`/`Present`, with
  `kGpuPassCount == 3`). Nothing new needs to be added to this file —
  Phase 2 is a pure CONSUMER of it. (`cpuScopes`/`cpuScopeCount` are the
  per-system CPU breakdown Phase 1 already collects — Phase 2 does not
  touch them at all; they belong to a different, already-shipped feature,
  not the frame-time-graph reshape this phase is scoped to.)
- **`src/Profiling/FrameProfiler.h`** already exposes exactly the read
  surface Phase 2 needs: `HistoryCount()` (how many frames are currently
  retained, up to `kMaxFrameHistory == 300`) and
  `HistoryAt(std::size_t indexFromOldest)` (oldest-to-newest ordering,
  already documented and already exercised by
  `tests/Profiling/FrameProfilerTests.cpp`'s own wraparound test,
  `RingBufferWrapsAndKeepsMostRecentFrames`). Phase 2 does not need to
  touch `FrameProfiler` itself at all — it only calls these two
  already-existing, already-tested methods. **Confirmed directly against
  `FrameProfiler.cpp` for this pass: both methods read exclusively from
  `m_history`/`m_historyCount` (the completed-and-pushed ring buffer),
  never from `m_current` (the in-progress frame's own scratch
  `FrameSample`) — this is what makes `BuildFrameGraphPoints()` safe to
  call at any point in the frame lifecycle, including mid-frame (new in
  v3 — see 2.4 below).**
- **The tri-state discipline is already fully designed and already
  enforced end-to-end up to this point.** `FrameSample::gpuPasses` is
  already an `std::array<GpuPassSample, kGpuPassCount>` with every entry
  defaulting to `GpuSampleStatus::Absent` — Phase 0/1 already built this
  correctly (confirmed directly:
  `FrameProfilerTests.cpp`'s `GpuPassAndMemorySamplesDefaultToAbsent` test
  asserts exactly this); Phase 2 only has to preserve it faithfully, not
  invent it.
- **A directly-reusable Tier-1 precedent already exists for exactly this
  shape of work**: `src/Editor/MemoryPanelData.h/.cpp`'s
  `BuildMemoryRows()`/`BuildHeapBudgetRows()` are pure functions that take
  an already-collected data structure (`GpuMemoryTracker::Entry`/
  `VmaBudget`) and reshape it into plain, display-ready rows, tested in
  `tests/Editor/MemoryPanelDataTests.cpp` with zero ImGui/Renderer
  involvement. Phase 2's new function(s) should read as the direct sibling
  of this pattern, just for time-series points instead of a sorted table —
  **with one deliberate, narrow deviation from that sibling's own parameter
  types, discussed in Step 3.1/3.2 below (new in v3): the range helpers
  accept `std::span<const FrameGraphPoint>`, not `const
  std::vector<FrameGraphPoint>&` like `BuildMemoryRows()` does, because
  Phase 2's own design (unlike `BuildMemoryRows()`'s) explicitly promises a
  caller can slice its output for free.**
- **The testing convention for touching `FrameProfiler::Instance()` is
  already established**: `tests/Profiling/FrameProfilerTests.cpp` already
  calls `ResetForTesting()` (plus `SetCaptureEnabled(true)`) before AND
  after each test case, then drives
  `BeginFrame()`/`RecordCpuScope()`/`SetGpuPassSample()`/`EndFrame()`
  directly to construct synthetic history, then asserts on
  `HistoryCount()`/`HistoryAt()`. Phase 2's own tests should follow this
  exact same recipe — there is no need to invent a second way to seed
  synthetic frame history for testing purposes.
- **The CMake wiring precedent is already established and requires no new
  option.** `src/Profiling/` is *already* unconditionally compiled into
  `gte_core` (confirmed directly against the current root `CMakeLists.txt`
  — the four `src/Profiling/` entries sit unconditionally inside the main
  `add_library(gte_core STATIC ...)` call, immediately after
  `src/Game/RenderSystem.h`, not inside any `if(GTE_ENABLE_EDITOR)`/
  `if(GTE_ENABLE_PROFILER)` block — only `ScopeTimer`'s own *body*, not its
  header's presence in this list, is compile-time-gated), and
  `tests/Profiling/FrameProfilerTests.cpp`/`ScopeTimerTests.cpp` are
  *already* listed unconditionally in `tests/CMakeLists.txt`'s
  `GTE_TEST_SOURCES` (confirmed directly — immediately after the
  `Assets/*Tests.cpp` block and immediately before
  `Input/InputStateTests.cpp` — the exact spot Phase 2's own new test file
  should be inserted next to). Phase 2's new files/tests should be added to
  these exact same two unconditional lists — no new CMake option, no new
  `if()` block.

### 2.2 What is genuinely missing today (the actual gap)

- **No function anywhere turns `FrameProfiler`'s history into a plottable
  point array.** This was already confirmed and stated plainly in
  `PROFILER_IMPLEMENTATION_STATUS_v2.md`'s own "Phase 2" entry: "No
  function anywhere turns `FrameProfiler`'s ring buffer into an array of
  `(frameIndex, cpuMs, gpuMsOrAbsent)` points ready for a plotting widget."
  This is a from-scratch addition, not an extension of something
  half-built.
- **No min/max/range helper exists for auto-scaling a graph's Y axis.**
  Nothing today answers "what's the tallest CPU frame time in the last N
  frames" or "what's the range of `GameView` GPU time, ignoring frames
  where it didn't run" — a future Phase 7 panel will need this the moment
  it tries to draw an auto-scaled line plot rather than a plot with a
  hardcoded, guessed Y range.
- **No test file exists yet for this reshape step** — `tests/Profiling/`
  currently holds exactly `FrameProfilerTests.cpp` and
  `ScopeTimerTests.cpp`; a new file is needed for Phase 2's own logic,
  matching the "new engine module gets its own test file from day one"
  convention `AGENTS.md`/`PROFILER_STRATEGY_v2.md` both already establish.
- **No decision has yet been made about WHERE this reshape code should
  live.** This matters more than it might first appear — see 2.3 below.

### 2.3 A design question that must be answered BEFORE writing any code

`PROFILER_STRATEGY_v2.md`'s own Phase 7 section says the Editor panel's
`ProfilerPanelData.h/.cpp` (an `src/Editor/`-only, `GTE_ENABLE_EDITOR`-gated
file, following the `MemoryPanelData.h/.cpp` precedent) is what will
"build the graph's plottable point array (Phase 2, including its 'gap'
handling for absent-pass frames)" — worded in a way that could be
misread as "Phase 2's own code lives inside `src/Editor/`, written only when
Phase 7 is written." **This strategy deliberately does NOT do that, and
states the reasoning explicitly so a future implementer doesn't
second-guess it:**

- `PROFILER_STRATEGY_v2.md`'s own Step 1.2 success table lists **Phase 6
  (benchmark mode, no Editor compiled in at all)** as a first-class
  deliverable that needs "every metric" dumped to CSV — and Phase 6 is
  explicitly designed to build with `-DGTE_ENABLE_EDITOR=OFF` as one of its
  two supported paths (`PROFILER_STRATEGY_v2.md`, Phase 6, path (a)). A
  history-to-points reshape function that only exists inside
  `src/Editor/` would be **unusable** by that CSV exporter in an
  Editor-disabled build — forcing Phase 6 to either duplicate this exact
  reshape logic a second time (violating the plan's own explicit rule,
  restated in its Phase 6 section: "this phase is explicitly a *consumer*,
  not a parallel reimplementation") or forcing Phase 2's logic to be moved
  out of `src/Editor/` retroactively once Phase 6 discovers the problem.
- **Decision: Phase 2's reshape functions live in `src/Profiling/`
  itself** (a new pair of files, `FrameGraphData.h`/`FrameGraphData.cpp`),
  compiled unconditionally into `gte_core` exactly like
  `FrameProfiler.h/.cpp` already are — no `GTE_ENABLE_EDITOR` dependency,
  no `GTE_ENABLE_PROFILER` dependency either (this reshape step reads
  already-collected `FrameSample` data; it has nothing to do with whether
  `ScopeTimer` itself compiles to a no-op — see `AGENTS.md`'s existing
  precedent that `FrameProfiler` itself "always compiles in, regardless of
  `GTE_ENABLE_PROFILER`"). This is not a new pattern — it is the exact
  same "the class stays available/testable even when its production call
  site is gated off" precedent already established twice in this codebase
  (`SdlMemoryTracker`, then `FrameProfiler`), applied a third time.
  `src/Editor/ProfilerPanelData.h/.cpp` (built later, in Phase 7) will then
  simply **call** `gte::Profiling::BuildFrameGraphPoints()` and reshape
  its *output* further into whatever ImGui-specific form the plot widget
  needs (see Step 4's "no NaN inside `src/Profiling/`" refusal) — a thin
  Editor-side wrapper around an Editor-independent core, exactly mirroring
  how `Panels/MemoryPanel.cpp` is a thin ImGui wrapper around
  `MemoryPanelData.h`'s pure logic today.
- This resolves the ambiguity in `PROFILER_STRATEGY_v2.md`'s own wording
  cleanly: Phase 7's "(Phase 2, including its 'gap' handling...)" phrase
  is read as "Phase 7's panel *uses* Phase 2's already-built, already-
  tested output," not "Phase 7 is when Phase 2's code first gets written."

### 2.4 Constraints discovered while reading the code (must be respected)

- **`FrameSample::gpuPasses` is a fixed `std::array<GpuPassSample, 3>`,
  index-addressed by the `GpuPass` enum (`GameView = 0`, `SceneView = 1`,
  `Present = 2`).** Any reshape function must preserve this exact
  correspondence — a bug that reads `gpuPasses[GpuPass::SceneView]`'s data
  while labeling it `GameView` (or any other index mixup) would silently
  corrupt every downstream consumer's understanding of which named pass
  cost what, directly contradicting `PROFILER_STRATEGY_v2.md`'s Step 2.3
  concern about the Game view vs. Scene view ambiguity. **This is the
  specific failure mode Step 3.3's strengthened test case 4 (see the
  v1->v2 Changelog above) exercises directly, across all three passes at
  once rather than one at a time — and note that `GpuPass::Present` (a
  pass name) and `GpuSampleStatus::Present` (a status value) are two
  entirely unrelated identifiers that happen to share a name; case 4
  deliberately leaves `GpuPass::Present` at its default
  `GpuSampleStatus::Absent` specifically so a reader can see the two are
  never confused anywhere in the implementation (new in v3).**
- **Every currently-retained frame's `gpuPasses` are, today, unconditionally
  `Absent`** (Phase 4 hasn't landed — see
  `PROFILER_IMPLEMENTATION_STATUS_v2.md`'s "Known rough edges": "every
  `FrameSample` collected today has all-`Absent` GPU passes"). Phase 2's
  own tests must therefore prove correct behavior for the present-day
  reality (all-absent GPU data alongside real CPU data), for a synthetic
  `Present` sample, AND for a synthetic `Unsupported` sample (all
  constructed via the already-existing `SetGpuPassSample()` test hook) —
  never assume "GPU data exists" just because it will, eventually, in Phase
  4, and never assume `Unsupported` is adequately covered just because
  `ComputeGpuMillisecondsRange()` happens to treat it the same as `Absent`
  in aggregate (see the v1->v2 Changelog above — the two statuses are
  still distinct, individually-reported values at the per-point level and
  must each be proven to round-trip correctly on their own).
- **`HistoryAt()`'s precondition is "caller's responsibility, no
  bounds-checking"** (see `FrameProfiler.h`'s own comment: "same
  ...contract as `std::array::operator[]` elsewhere in this codebase").
  Phase 2's reshape function must itself only ever call `HistoryAt(i)` for
  `i` in `[0, HistoryCount())` — i.e. it owns responsibility for iterating
  safely; it must never be written in a way that assumes some external
  caller will already have range-checked for it.
- **`GpuPass` out-of-range handling has an EXACT, already-shipped precedent
  in this exact module — follow it, don't invent a different one (new in
  v3, supersedes v2's debug-assert proposal).** `FrameProfiler::
  SetGpuPassSample()` (`FrameProfiler.cpp`) already bounds-checks its own
  `pass` parameter with a plain, UNCONDITIONAL (not debug-only) check —
  `if (static_cast<std::size_t>(pass) >= kGpuPassCount) { return; }` —
  silently doing nothing for an invalid value rather than reading/writing
  out of bounds. `ComputeGpuMillisecondsRange()`'s own `pass` parameter
  faces the exact same risk (it also indexes
  `gpuPasses[static_cast<std::size_t>(pass)]`) and should follow this
  SAME sibling convention: an unconditional bounds check, active in both
  Debug and Release, returning `FrameGraphRange{}` (`hasData == false`) for
  an out-of-range value — not a debug-only `assert()`, which would leave a
  Release build's out-of-range read exactly as unsafe as having no check
  at all. See the revised Step 3.2 below for the exact requirement.
- **This engine is explicitly single-threaded** (`GpuMemoryTracker`'s own
  class comment, restated throughout this codebase). Phase 2's reshape
  function needs no locking, no atomics, and no thread-safety
  consideration whatsoever — a single, synchronous read-then-copy over
  `FrameProfiler`'s already-populated history.
- **`BuildFrameGraphPoints()` is safe to call at ANY point in the frame
  lifecycle, including strictly between a `BeginFrame()`/`EndFrame()` pair
  (new in v3).** Confirmed directly against `FrameProfiler.cpp`:
  `HistoryCount()`/`HistoryAt()` read exclusively from `m_history`/
  `m_historyCount` (the completed-and-pushed ring buffer) and never from
  `m_current` (the in-progress frame's own scratch `FrameSample`). A
  reshape call issued mid-frame will therefore simply never observe the
  not-yet-completed frame — there is no undocumented "don't call this
  mid-frame" precondition to worry about, and no special handling is
  needed in `BuildFrameGraphPoints()`'s own implementation to achieve this;
  it falls out automatically from only ever calling `HistoryAt()`/
  `HistoryCount()`. Given its own dedicated regression test — see Step
  3.3's new case 10.
- **Toggling `FrameProfiler::SetCaptureEnabled(false)` mid-run and later
  re-enabling it produces NO gap in the retained `frameIndex` sequence
  (new in v3, confirmed directly against `FrameProfiler.cpp`).**
  `BeginFrame()`/`EndFrame()` are both complete no-ops while capture is
  disabled, and neither touches `m_frameIndex` on that early-return path —
  a disabled window never consumes a `frameIndex` value at all, it simply
  never happened from `FrameProfiler`'s point of view. Every frame that
  DOES make it into the ring buffer therefore carries a perfectly
  contiguous `frameIndex` sequence; Phase 2's reshape can rely on this
  (and its own tests should assert exact expected values because of it —
  see the revised Step 3.3 below) rather than merely tolerating "probably
  contiguous, but check anyway just in case."
- **No heap allocation may occur inside `FrameProfiler`'s own per-frame hot
  path** (`Step 3a` of `PROFILER_STRATEGY_v2.md`) — but this rule is about
  the COLLECTION path (`BeginFrame`/`EndFrame`/`RecordCpuScope`), not about
  Phase 2's reshape function, which runs **on demand** (once per Editor
  frame when Phase 7's panel is visible, or once at the end of a benchmark
  run for Phase 6), never inside the profiled hot path itself. Phase 2's
  reshape function is explicitly allowed — and expected — to return a
  heap-allocated `std::vector`, exactly as `BuildMemoryRows()` already does
  for the Memory panel. Do not over-apply the zero-allocation rule to code
  it was never meant to constrain.
- **Nothing in `src/Profiling/` may include an ImGui header, directly or
  transitively** — restated from `AGENTS.md`'s "Profiling" section and
  `PROFILER_STRATEGY_v2.md`'s own per-phase checklist ("Nothing outside
  `src/Editor/` ... includes an ImGui header"). This directly shapes the
  "no NaN, no ImGui-specific sentinel" design decision in Step 3 below —
  ImGui's own plotting widgets are what want a NaN-as-gap convention, and
  that translation belongs strictly on the Editor side of the boundary,
  in Phase 7, not baked into this phase's otherwise ImGui-agnostic output.

--------------------------------------------------------------------------
## Step 3: The Plan (How will we get there?)
--------------------------------------------------------------------------

Six concrete, sequential, independently-verifiable steps. Each one builds
and the full test suite passes before moving to the next — mirroring how
every phase in `PROFILER_STRATEGY_v2.md` is meant to be delivered.

### Step 3.1 — Define the plain data shapes

New file: **`src/Profiling/FrameGraphData.h`**, inside `namespace
gte::Profiling` (matching `ProfilingTypes.h`/`FrameProfiler.h`'s own
namespace exactly — per `AGENTS.md`'s "Namespace" rule, every new type this
project defines lives inside `gte`, here nested under the same
`gte::Profiling` sub-namespace its siblings already use).

- **`FrameGraphPoint`** — one point per retained frame:
  - `frameIndex` (`std::uint64_t`) — copied straight from
    `FrameSample::frameIndex`, so a consumer can label the X axis with the
    real frame number rather than a synthetic 0-based index that would
    silently misrepresent history once the ring buffer has wrapped around
    past `kMaxFrameHistory`. This value is guaranteed gapless/strictly
    increasing across every point this module ever returns — see Step 2.4
    (new in v3) for why that is a structural guarantee of `FrameProfiler`'s
    own existing implementation, not merely an expectation.
  - `cpuMilliseconds` (`double`) — copied straight from
    `FrameSample::cpuFrameMilliseconds`. No tri-state needed here: Phase 1
    already guarantees every frame that reaches the ring buffer has a real,
    measured CPU time (there is no "CPU time didn't happen this frame"
    concept in this engine — every frame is, by definition, CPU work).
  - `gpuPasses` (`std::array<GpuPassSample, kGpuPassCount>`) — **the exact
    same type `FrameSample` itself already uses**, copied verbatim,
    index-for-index. Deliberately NOT reshaped into a new, Phase-2-specific
    type: reusing `GpuPassSample` directly means zero new tri-state
    plumbing to get subtly wrong, and a caller who already understands
    `FrameSample::gpuPasses` (e.g. anyone who read Phase 0's own data
    model) already understands this field with no new concept to learn.
  - **Design decision, stated explicitly so it is never re-litigated**: a
    `FrameGraphPoint` carries **all three** named GPU passes per point,
    rather than requiring the caller to request one `GpuPass` at a time via
    three separate function calls. This was chosen over the alternative
    (`BuildFrameGraphPoints(profiler, GpuPass pass)`, returning one
    GPU-series-only array per call) for three concrete reasons: (1) it
    mirrors `FrameSample` itself, which already carries all three passes
    together — no new shape to invent; (2) it guarantees the CPU line and
    every GPU line a future Phase 7 panel overlays are built from the
    exact same underlying frame list in one pass, with no risk of two
    separately-called reshapes silently drifting in length/order if
    `FrameProfiler`'s history mutates between calls (unlikely today given
    the single-threaded engine, but not a risk worth accepting for free);
    and (3) it is strictly cheaper to call once and let the caller index
    into whichever `GpuPass` it currently wants to draw, than to force three
    separate full history walks for what is, today, the same 300-frame
    array three times over.
  - **No windowing/"last N frames" parameter, by design.**
    `BuildFrameGraphPoints()` always returns every currently-retained frame
    (up to `kMaxFrameHistory == 300`). A caller wanting only the most
    recent 120 frames (say, for a fixed-width graph widget) can simply take
    a suffix of the returned `std::vector` — `std::vector` iterators/
    `.size()`/`.end() - N` already make this trivial with no information
    lost, so adding a second parameter to the reshape function itself would
    only duplicate logic every caller can already do for free, while adding
    its own edge case to get wrong (what happens when the requested window
    is larger than the available history?). Keep the reshape function's
    contract simple: "give me everything currently retained, in order."
    **For this "slice for free" promise to actually be free (not merely
    "free of an extra reshape-function parameter, but still secretly
    forcing a copy at the range-helper call site"), the two range helpers
    below must be able to accept that slice directly — see their revised
    parameter type in Step 3.2 (new in v3).**

- **`FrameGraphRange`** — the Y-axis auto-scale helper's result:
  - `hasData` (`bool`) — `false` if zero points in the requested series had
    real data (e.g. asking for `GameView`'s range when every retained frame
    is `Absent`, which is exactly today's reality pre-Phase-4, OR when an
    out-of-range `GpuPass` value was passed — new in v3, see Step 3.2). A
    caller (Phase 7) checks this FIRST and falls back to a sensible default
    range (or simply doesn't draw that line at all) rather than ever
    plotting a meaningless `0.0..0.0` range.
  - `minMilliseconds` / `maxMilliseconds` (`double`) — only meaningful when
    `hasData` is `true`.
  - **No `sampleCount`/average/other statistics field is added here (new
    in v3, see Step 4)** — min/max is exactly what an auto-scaling Y axis
    needs, and this phase deliberately does not speculate about what a
    future statistics view might also want.

No other new types are needed for this phase. Nothing here references
`FrameProfiler` directly in its own definition — these are plain, storage-
only structs, exactly as `AGENTS.md`'s "Entity-Component-System" section
already mandates for plain-data types ("Components are plain data... at
most small pure-math helper methods").

### Step 3.2 — Implement the reshape function

New file: **`src/Profiling/FrameGraphData.cpp`**, implementing:

- **`std::vector<FrameGraphPoint> BuildFrameGraphPoints(const FrameProfiler&
  profiler)`** — the core of this phase. Implementation shape: reserve a
  `std::vector` sized to `profiler.HistoryCount()` (a single allocation,
  known size up front — no repeated reallocation/`push_back` growth), then
  iterate `i` from `0` to `HistoryCount() - 1`, reading
  `profiler.HistoryAt(i)` and copying `frameIndex`/`cpuFrameMilliseconds`/
  `gpuPasses` into a `FrameGraphPoint` at the same index — a direct,
  1:1, order-preserving transcription. An empty history (nothing recorded
  yet, e.g. a freshly-constructed `FrameProfiler` before the first
  `BeginFrame()`/`EndFrame()` pair, or right after `ResetForTesting()`)
  simply returns an empty vector — never a special-cased default point, so
  a caller can safely check `.empty()` and draw nothing, exactly the same
  "no special-casing, no invented default row" precedent
  `BuildMemoryRows()` already sets when given an empty entry list. This
  function is safe to call mid-frame (between a `BeginFrame()`/`EndFrame()`
  pair) with no special handling needed in its own body — this falls out
  automatically from only ever calling `HistoryAt()`/`HistoryCount()`,
  which never expose the in-progress frame (see Step 2.4, new in v3).
- **`FrameGraphRange ComputeCpuMillisecondsRange(std::span<const
  FrameGraphPoint> points)`** — scans every point's `cpuMilliseconds`
  (always real data, per Step 3.1) and returns the min/max; `hasData ==
  false` only if `points` itself is empty. **Parameter type changed from
  `const std::vector<FrameGraphPoint>&` to `std::span<const
  FrameGraphPoint>` (new in v3 — see the v2->v3 Changelog above for the
  full reasoning)** — a `std::vector` argument still converts implicitly,
  so `ComputeCpuMillisecondsRange(points)` (passing `BuildFrameGraphPoints()`'s
  full output) needs no change at any call site; only a future WINDOWED
  caller benefits, by being able to pass a sub-range with zero copying.
- **`FrameGraphRange ComputeGpuMillisecondsRange(std::span<const
  FrameGraphPoint> points, GpuPass pass)`** — scans every
  point's `gpuPasses[static_cast<std::size_t>(pass)]`, including **only**
  entries whose `status == GpuSampleStatus::Present` in the min/max
  computation, and setting `hasData = true` only if at least one such
  entry was found. This is the one place in this phase where the
  present/absent/unsupported tri-state actually changes behavior (as
  opposed to `BuildFrameGraphPoints()`, which merely carries it through
  unexamined) — get this one right and the "hidden panel reads as free"
  failure mode `PROFILER_STRATEGY_v2.md`'s Step 2.3/3a warn about is
  structurally impossible to reintroduce here, since an absent/unsupported
  frame's `milliseconds` value (whatever it happens to hold — see Step
  3.3's new case 9, since it is not even guaranteed to be `0.0`) is never
  even looked at by the min/max scan; only `status` is ever branched on.
  **Bounds-checks `pass` unconditionally, in both Debug and Release builds
  (new in v3, supersedes v2's debug-only-assert proposal — see the
  v2->v3 Changelog above and Step 2.4's newly-added constraint):**
  `if (static_cast<std::size_t>(pass) >= kGpuPassCount) { return
  FrameGraphRange{}; }` at the top of this function, mirroring
  `FrameProfiler::SetGpuPassSample()`'s own already-shipped handling of
  this exact same kind of invalid input, rather than a debug-only
  `assert()` that would do nothing at all in a Release build. Every real
  call site today only ever passes one of the three named `GpuPass`
  values, so this branch is never expected to be taken in practice — it
  exists purely as a defensive guard against a future mistake (e.g. an
  invalid `static_cast<GpuPass>` from an integer), the same role
  `SetGpuPassSample()`'s own check already plays two doors down. An
  out-of-range `pass` is treated identically to "a valid pass with zero
  `Present` entries" — both simply report `hasData == false` — rather than
  inventing a third, distinct "this was actually an error" return state;
  a caller passing a garbage `GpuPass` value has no real data to plot
  either way, and a separate error channel for a path that should never be
  reachable from real code would be speculative complexity this phase
  doesn't need.

Both functions are pure: they take their inputs by value/`const&`/`span`
(the last of which is itself a cheap, non-owning view — no allocation of
its own), allocate exactly the one `std::vector` `BuildFrameGraphPoints()`
needs to return (the two range helpers allocate nothing at all), and have
no side effects, no static/global state of their own, and no dependency on
anything except `FrameProfiler`'s already-public, already-`const`-qualified
read API (`HistoryCount()`/`HistoryAt()`) and `ProfilingTypes.h`'s existing
types.

### Step 3.3 — Write the Tier-1 test file

New file: **`tests/Profiling/FrameGraphDataTests.cpp`**, following the
exact "reset, seed synthetic frames via
`BeginFrame()`/`RecordCpuScope()`/`SetGpuPassSample()`/`EndFrame()`, then
assert" recipe `FrameProfilerTests.cpp` already established (see 2.1),
including its own `SetUp()`/`TearDown()` pattern of resetting the singleton
both before AND after each test case. At minimum, the following cases —
each one chosen because it directly guards against a specific failure mode
identified in Step 2.4 (**cases marked "(v2)" were new/materially
strengthened in the v1->v2 pass; cases/edits marked "(v3)" are new/
materially strengthened in this pass — see both Changelogs above**):

1. **Empty history → empty output.** `ResetForTesting()`, then call
   `BuildFrameGraphPoints()` with zero completed frames — assert the
   returned vector is empty. Also assert `ComputeCpuMillisecondsRange({})`
   and `ComputeGpuMillisecondsRange({}, GpuPass::GameView)` both report
   `hasData == false`.
2. **A single completed frame round-trips exactly.** Seed one frame with a
   known `cpuFrameMilliseconds` (via however `FrameProfiler`'s test hooks
   let a test control this — following `FrameProfilerTests.cpp`'s own
   existing pattern for asserting `cpuFrameMilliseconds`) — assert the one
   returned point's `frameIndex` matches exactly and its `cpuMilliseconds`
   matches **bit-exactly** (`EXPECT_DOUBLE_EQ`, not a tolerance-based
   comparison — see the v1->v2 Changelog above, since the project rule is
   "CPU values preserved exactly" and this value is a plain copy, never
   arithmetic, so an exact match is both correct and achievable), and its
   `gpuPasses` are all `Absent` (today's correct default — see Step 2.4).
3. **Multiple frames preserve oldest-to-newest order — checked via EXACT
   expected `frameIndex` values, not merely "increasing" (strengthened in
   v3 — see the v2->v3 Changelog above, item 4).** Seed 3+ frames — assert
   the returned points' `frameIndex` values are EXACTLY `0, 1, 2, ...` in
   that order (matching what `HistoryAt(0..N-1)` itself would give for a
   freshly-reset profiler that has never wrapped), not just "strictly
   increasing" — a monotonic-only check would not catch a reshape that
   accidentally dropped the first element and shifted everything down by
   one, since the remaining subsequence would still look "increasing."
4. **All three named GPU passes, all three tri-state values, in ONE frame,
   all round-trip to exactly the right index — with no cross-contamination
   between passes (v2).** Seed a single frame and call
   `SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Present, 3.5, 12,
   400)`, `SetGpuPassSample(GpuPass::SceneView, GpuSampleStatus::Unsupported)`,
   and leave `GpuPass::Present` untouched (still its default `Absent`)
   before `EndFrame()` — assert the resulting point's
   `gpuPasses[GameView]` is exactly `Present`/`3.5`/`12`/`400`,
   `gpuPasses[SceneView]` is exactly `Unsupported` (and NOT `Absent` —
   proving the two distinct "no real value" statuses are never conflated
   with each other, not just distinguished from `Present`), and
   `gpuPasses[Present]` is exactly `Absent`. This single test directly
   proves ALL THREE of: (a) "never mix up which named pass costs what"
   (Step 2.4's original concern, now checked across all three passes at
   once instead of one at a time), (b) `Present` preserves every one of
   its four fields exactly, and (c) `Unsupported` round-trips as a
   genuinely distinct value from `Absent` at the per-point level — closing
   the exact gap identified in the v1->v2 Changelog above. **(v3 note:**
   leaving `GpuPass::Present` — the pass name — at `GpuSampleStatus::Absent`
   — the status value — is itself a small, deliberate proof that this
   codebase's two same-named-but-unrelated `Present` identifiers are never
   confused with each other anywhere in the implementation; call this out
   in the test's own comment so a future reader doesn't assume it's a
   typo.**)
5. **Ring buffer wraparound is transparently handled — checked via EXACT
   expected boundary `frameIndex` values (strengthened in v3 — see the
   v2->v3 Changelog above, item 4).** Push more than `kMaxFrameHistory`
   (300) frames — say, `kMaxFrameHistory + 5` total, mirroring
   `FrameProfilerTests.cpp`'s own existing
   `RingBufferWrapsAndKeepsMostRecentFrames` test exactly — and assert
   `BuildFrameGraphPoints()` returns exactly `kMaxFrameHistory` points,
   with `points.front().frameIndex == totalFrames - kMaxFrameHistory` and
   `points.back().frameIndex == totalFrames - 1` (the same exact-boundary
   pattern `FrameProfilerTests.cpp` already uses for `HistoryAt()` itself
   — grounded in Step 2.4's newly-confirmed guarantee that `frameIndex` is
   always gapless/contiguous across retained frames, new in v3), not
   merely "in the correct order" — proving this phase's reshape correctly
   rides on top of `FrameProfiler`'s own already-tested wraparound behavior
   rather than needing to reimplement any of it, and catching an
   off-by-one in this reshape's OWN iteration that a weaker "just
   increasing" check would miss.
6. **`ComputeCpuMillisecondsRange()` ignores nothing (CPU data is always
   real).** Seed frames with distinct CPU millisecond values including a
   known min and a known max somewhere in the middle of the sequence —
   assert the returned range's `minMilliseconds`/`maxMilliseconds` match
   **bit-exactly** (`EXPECT_DOUBLE_EQ` — see the v1->v2 Changelog above)
   and `hasData == true`.
7. **`ComputeGpuMillisecondsRange()` correctly ignores `Absent`/
   `Unsupported` entries.** Seed a mix: some frames with
   `GpuPass::GameView` left `Absent` (the default — simply don't call
   `SetGpuPassSample()` for those frames), interleaved with a few frames
   where it's explicitly set `Present` with known values — assert the
   computed range's min/max reflect ONLY the `Present` frames' values, and
   that a request for `GpuPass::SceneView` (never set `Present` in this
   test) reports `hasData == false` even though `GameView`'s range is
   valid — this is the direct regression test for the single most
   important correctness property this phase must prove (see Step 1.2).
   Choose `milliseconds`/`drawCallCount`/`triangleCount` values that all
   differ from one another for the `Present` entries (mirroring case 4's
   own good practice — new in v3) so that a hypothetical bug reading the
   wrong `GpuPassSample` field would be caught by this test too, not just
   by inspection.
8. **An all-`Unsupported` series also reports no data.** Seed at least one
   frame with `SetGpuPassSample(pass, GpuSampleStatus::Unsupported)` and no
   `Present` entries at all for that pass across the whole seeded history —
   assert `hasData == false`, proving `Unsupported` is treated the same as
   `Absent` for range-computation purposes (both mean "no real number
   exists to include in a min/max scan"), even though they remain a
   distinct, separately-reported status on each individual point (Step
   3.1's `gpuPasses` field is never collapsed down to a plain "has
   data"/"doesn't" boolean at the per-point level — only the aggregate
   range helper simplifies it that way, and only for its own min/max
   purpose — see test case 4 above for the point-level proof that the two
   statuses stay genuinely distinct where it matters).
9. **(NEW in v3) An `Absent`/`Unsupported` `GpuPassSample` carrying a
   non-zero, "stale-looking" value is STILL correctly excluded — the single
   most direct test of "never convert absent GPU data into 0 ms."** Seed a
   frame via `SetGpuPassSample(GpuPass::GameView, GpuSampleStatus::Absent,
   42.0, 5, 100)` — `SetGpuPassSample()`'s own signature permits passing a
   non-default `milliseconds`/`drawCallCount`/`triangleCount` alongside
   `GpuSampleStatus::Absent` (there is nothing in `FrameProfiler` today that
   prevents a future, buggy caller from doing exactly this), so this is a
   fully realistic, constructible-today scenario, not a hypothetical one —
   assert `ComputeGpuMillisecondsRange()` still reports `hasData == false`
   for `GameView` (no frame contributes `42.0` to any min/max), and repeat
   the same construction with `GpuSampleStatus::Unsupported` in place of
   `Absent`. This test positively rules out a subtly different bug than
   case 7/8 already cover: an implementation that accidentally branches on
   "is the stored value zero" instead of on the actual `status` tag would
   pass every prior case (where absent entries happen to hold their
   natural `0.0` default) while silently failing this one — this is what
   makes it the single most direct test of this whole phase's "never
   convert absent GPU data into 0 ms" rule, rather than an indirect
   consequence of testing something else.
10. **(NEW in v3) `BuildFrameGraphPoints()` never observes an in-progress
    (not-yet-`EndFrame()`'d) frame.** After completing 2 full
    `BeginFrame()`/`EndFrame()` pairs, call `BeginFrame()` a THIRD time
    but deliberately do NOT call the matching `EndFrame()` before calling
    `BuildFrameGraphPoints()` — assert the returned vector still has
    exactly 2 points (not 3), i.e. the in-progress third frame is
    completely invisible to this reshape. This is a direct regression test
    for the guarantee established in Step 2.4 (new in v3) — grounded in
    `HistoryCount()`/`HistoryAt()` only ever reading from
    `FrameProfiler`'s completed-frame ring buffer, never its in-progress
    scratch `FrameSample` — and protects a real, previously-undocumented
    property with an actual test rather than leaving it as a sentence in a
    planning document nobody re-reads once implementation starts.

### Step 3.4 — Wire into the build

- **Root `CMakeLists.txt`**: add `src/Profiling/FrameGraphData.h` and
  `src/Profiling/FrameGraphData.cpp` to `gte_core`'s unconditional
  `add_library(gte_core STATIC ...)` file list, immediately alongside the
  four existing `src/Profiling/` entries (`ProfilingTypes.h`,
  `FrameProfiler.h`, `FrameProfiler.cpp`, `ScopeTimer.h`) — no new `option()`,
  no new `target_compile_definitions()`, no new `if()` block. This is a
  direct, mechanical addition to an already-existing, already-unconditional
  list. (Re-confirmed directly against the current root `CMakeLists.txt`
  for this v3 pass, exactly as v2 already found: the four existing
  `src/Profiling/` entries sit unconditionally inside the main
  `add_library()` call, immediately after `src/Game/RenderSystem.h`, not
  inside any `if(GTE_ENABLE_EDITOR)`/`if(GTE_ENABLE_PROFILER)` block — only
  `ScopeTimer`'s own *body*, not its header's presence in this list, is
  compile-time-gated. No drift found since v2's own check.)
- **`tests/CMakeLists.txt`**: add `Profiling/FrameGraphDataTests.cpp` to the
  unconditional `GTE_TEST_SOURCES` list, immediately alongside
  `Profiling/FrameProfilerTests.cpp`/`Profiling/ScopeTimerTests.cpp` — same
  reasoning, no new guard needed. (Re-confirmed directly for this v3 pass:
  no drift since v2's own check — the two entries still sit exactly where
  described.) Also add one short bullet to this file's own header-comment
  "Test taxonomy" list (matching the existing documentation style for
  every other entry there, including the existing
  `Profiling/FrameProfilerTests.cpp`/`Profiling/ScopeTimerTests.cpp`
  entries this new one should sit directly beside) describing what
  `FrameGraphDataTests.cpp` covers, so the file's own self-documentation
  stays accurate — this file already treats itself as living
  documentation of the whole suite's shape, and should keep being kept
  that way.

### Step 3.5 — Build and verify

- `cmake --build build --config Debug` — must succeed cleanly, with no new
  warnings introduced by the new files. (Note: `std::span` requires
  `#include <span>` — a small, standard-library-only addition; no new
  third-party dependency or CMake wiring is needed for it, since this
  project already targets C++20 unconditionally.)
- `ctest --test-dir build -C Debug --output-on-failure` — must report the
  full suite passing: the pre-existing baseline (last confirmed at 450 as
  of the v2 pass — **re-confirm the exact current number with a fresh
  `ctest` run immediately before implementation; this v3 pass did not
  re-count the whole suite either, per its own Changelog note in Step
  1.1, so treat "450" as informational history only, not a number to
  trust**), plus this phase's own new test cases (10 minimum, per Step
  3.3 — up from v2's 8, due to the two new v3 cases), with zero
  regressions. Any pre-existing test that starts failing after this change
  is a real regression to understand and fix, not something to work
  around — per `AGENTS.md`'s own testing discipline.
- As an optional, additional sanity check (not a substitute for the
  automated tests above): a short-lived, throwaway diagnostic (a temporary
  log line printing `BuildFrameGraphPoints()`'s result for the last few
  real frames, removed before this phase is considered done — the same
  "temporary, not committed" convention `README.md`'s own IK-solver/
  append-bone-solver diagnostics used) run against the real, live engine
  can confirm the numbers look sane against a stopwatch-level gut check,
  mirroring how Phase 1 itself was manually sanity-checked before being
  considered complete.

### Step 3.6 — Documentation, kept in sync as-you-go

- **`AGENTS.md`'s existing "Profiling" section** gains a short new bullet
  (not a new section — this is an extension of the same convention, not a
  new one) stating: `src/Profiling/FrameGraphData.h/.cpp` is the one place
  history is reshaped into plottable points, it is always-compiled and
  ImGui-free exactly like `FrameProfiler` itself, and any future consumer
  (a benchmark-mode CSV exporter, a different graph widget, ...) should
  call `BuildFrameGraphPoints()`/`ComputeCpuMillisecondsRange()`/
  `ComputeGpuMillisecondsRange()` rather than re-deriving the same
  history-walk logic a second time. This is the same "document the
  convention as it's built, not after" discipline Phase 1 itself already
  applied when it first added the "Profiling" section (see
  `PROFILER_STRATEGY_v2.md`, Phase 1's own "document the convention as
  it's built" note).
- **`PROFILER_IMPLEMENTATION_STATUS_v2.md`** (or a fresh `_v3` revision of
  it, following that document's own established "bump the version, keep
  the changelog" convention rather than silently editing v2 in place) gets
  its "Phase 2" entry moved from "What was NOT implemented" into "What was
  implemented this session," with the same level of concrete detail
  (exact file names, exact function signatures, exact test count) the
  existing entries for Phase 0/1 already have — including a fresh, verified
  `ctest` total (see Step 3.5's own note on not trusting a copied-forward
  number). This is not optional busywork — it is what lets the NEXT
  phase's implementer (Phase 3, or Phase 7 reaching back to reuse this
  phase's output) trust the status document instead of having to re-derive
  "is this actually done" from the source tree themselves.
- **This document itself** should gain a short "Result" addendum once
  implemented (mirroring how `PROFILER_STRATEGY_v2.md`'s own closing
  section asks every phase to record real, measured outcomes back into the
  planning document rather than treating it as a frozen, one-time spec).

--------------------------------------------------------------------------
## Step 4: What We Will NOT Do (Focus)
--------------------------------------------------------------------------

- **No Editor panel, and no ImGui code of any kind, in this phase.**
  Drawing an actual graph is Phase 7's job, deliberately sequenced last in
  `PROFILER_STRATEGY_v2.md` for good reason (there is nothing for a panel
  to show correctly until Phases 2/3/5 all exist — building it early would
  mean either a half-feature or guessed-at data shapes). This phase
  produces data a future panel will consume; it does not draw one.
- **No NaN, no ImGui-specific sentinel value, and no "gap" representation
  of any kind baked into `FrameGraphPoint`/`FrameGraphRange` themselves.**
  `src/Profiling/` must never include an ImGui header (2.4), and NaN as a
  "skip this point" convention is specifically an `ImGui::PlotLines()`-
  style plotting-widget concern, not a property of the underlying data.
  `GpuSampleStatus::Absent`/`Unsupported` already say everything that needs
  saying, faithfully, at the `FrameGraphPoint` level — the translation
  from "this specific point's tri-state" into "how ImGui's specific plot
  API wants to see a gap" belongs entirely inside Phase 7's
  `src/Editor/ProfilerPanelData.h/.cpp`, where it can be one small,
  ImGui-adjacent conversion function reading Phase 2's untouched output,
  not something Phase 2 has to guess at without knowing which plotting API
  Phase 7 will eventually choose.
- **No GPU timestamp data, and no attempt to make `GameView`/`SceneView`/
  `Present` report anything other than `Absent` for real.** That is
  entirely Phase 4's job. Every test in this phase that wants a `Present`
  or `Unsupported` GPU sample constructs it synthetically via the
  already-existing `FrameProfiler::SetGpuPassSample()` test hook — this
  phase never adds a single real caller of that function anywhere in
  engine code. `PROFILER_IMPLEMENTATION_STATUS_v2.md`'s own "Known rough
  edges" note ("`SetGpuPassSample()`/`SetMemorySnapshot()` are fully
  implemented and tested but have ZERO real callers") remains true and
  unchanged after this phase.
- **No draw-call/triangle-count logic.** `GpuPassSample::drawCallCount`/
  `triangleCount` are carried through `FrameGraphPoint` unexamined
  (Step 3.1's verbatim copy), exactly as they already sit inside
  `FrameSample` today — this phase does not compute, validate, or display
  them; that is Phase 3's job.
- **No GPU-memory-over-time reshape.** `FrameSample::memory`
  (`MemorySnapshot`) is not touched by this phase at all — that is Phase
  5's job, and it is expected to follow the exact same "reuse the one ring
  buffer, add one small reshape function" shape this phase establishes,
  once its turn comes.
- **No benchmark-mode CSV exporter.** Phase 2 makes such an exporter
  *possible* to build cheaply later (by producing Editor-independent,
  plain data), but does not itself write one — that is Phase 6's job, and
  it is explicitly designed as a pure consumer of this phase's output, per
  `PROFILER_STRATEGY_v2.md`'s own "if a phase before this one needs to
  change to make benchmark mode work, that is a sign an earlier phase's
  design was wrong" rule.
- **No windowing/"last N frames" parameter on `BuildFrameGraphPoints()`
  itself** (see Step 3.1) — a caller slices the returned `std::vector`
  itself; this phase's contract stays "everything currently retained, in
  order." **The range helpers' `std::span` parameter (new in v3) is what
  makes that slicing genuinely free at the call site too — it is a
  parameter-type change, not a reintroduction of windowing logic inside
  this module.**
- **No `sampleCount`/average/other statistics field added to
  `FrameGraphRange` (new in v3).** Min/max is exactly what this phase's
  one stated consumer (an auto-scaling Y axis) needs — anything more is
  unrequested scope creep this phase deliberately declines to guess at
  ahead of a real need.
- **No new CMake option, no new compile-time switch.** This phase adds two
  files to two already-unconditional lists (Step 3.4) — it introduces no
  new `GTE_ENABLE_*` toggle of any kind, because there is nothing about
  this reshape step that should ever need to be disabled independently of
  the `FrameProfiler` data it reads from.
- **No change to `FrameProfiler`, `ScopeTimer`, or `ProfilingTypes.h`
  themselves.** Every type/method this phase needs already exists and is
  already tested; this phase is additive-only, reading through their
  existing, unmodified public API.
- **No change to any existing call site** (`Application.cpp`, `Game.cpp`,
  `AnimationSystem.cpp`, `RenderSystem.cpp`) that already calls
  `GTE_PROFILE_SCOPE(...)` or `FrameProfiler::Instance()`. Phase 2 is
  purely a new, separate consumer of already-collected data — it has no
  reason to touch a single existing instrumentation point.

--------------------------------------------------------------------------
## Step 5: Their Role (What does this mean for you?)
--------------------------------------------------------------------------

This section is written for whoever implements this phase next — likely
the same AI agent or developer who is reading this document right now,
immediately after finishing Phase 0/1.

### 5.1 How to start

1. Read this document fully, then re-read `PROFILER_STRATEGY_v2.md`'s
   Phase 2 section and Step 3a one more time, side by side with this
   document's Step 3 — this document is a strict refinement/expansion of
   that section, not a replacement or reinterpretation of it. If anything
   here appears to contradict `PROFILER_STRATEGY_v2.md`, that is a sign to
   stop and reconcile the two before writing code, not to silently pick
   one.
2. Re-read `AGENTS.md`'s "Profiling" section, "Namespace" rule, and
   "Testability & Regression Safety" section in full — every convention
   this phase must follow is already written down there; nothing here
   invents a new rule.
3. Run `ctest --test-dir build -C Debug --output-on-failure` once, BEFORE
   writing any code, to record the actual current passing-test count on
   your own machine/checkout — this document's own "450" figure was only
   ever independently re-derived once (during the v2 pass) and has not
   been re-counted since; treat it as historical context only, never as a
   number to trust without a fresh check.
4. Before writing `ComputeGpuMillisecondsRange()`, actually open
   `src/Profiling/FrameProfiler.cpp` and re-read `SetGpuPassSample()`'s own
   bounds-check one more time (new in v3) — it is the exact pattern to
   copy for handling an out-of-range `GpuPass`, not a debug-only assert.
5. Implement in the exact order given in Step 3 (types → reshape function
   → tests → CMake wiring → build/verify → documentation) — each step is
   individually small and individually verifiable; do not jump ahead to
   writing tests before the types they test exist, and do not skip the
   documentation step at the end just because the code already works.

### 5.2 Non-negotiable checklist for this phase (copy into the PR/commit description)

- [ ] `src/Profiling/FrameGraphData.h/.cpp` added, inside
      `namespace gte::Profiling`, with zero ImGui/SDL/Vulkan includes,
      direct or transitive.
- [ ] `FrameGraphPoint` carries `frameIndex`, `cpuMilliseconds`, and the
      full `std::array<GpuPassSample, kGpuPassCount> gpuPasses` verbatim —
      no new tri-state representation invented, no field dropped.
- [ ] `BuildFrameGraphPoints()` returns points in the exact same
      oldest-to-newest order `FrameProfiler::HistoryAt()` already uses, and
      never accesses `HistoryAt(i)` for any `i >= HistoryCount()`.
- [ ] `ComputeCpuMillisecondsRange()`/`ComputeGpuMillisecondsRange()` both
      accept `std::span<const FrameGraphPoint>`, not
      `const std::vector<FrameGraphPoint>&` (new in v3) — a `std::vector`
      argument still converts implicitly, so no caller needs a special
      case, but a future windowed caller can pass a sub-range with zero
      copying.
- [ ] `ComputeGpuMillisecondsRange()` genuinely ignores `Absent`/
      `Unsupported` entries **regardless of what numeric value happens to
      be stored alongside that status** — verified by a dedicated test
      that deliberately seeds a non-zero/"stale-looking" value alongside
      `Absent`/`Unsupported` (Step 3.3, case 9, new in v3), not just by a
      test where the absent value happens to be its natural `0.0` default
      — AND bounds-checks `pass` UNCONDITIONALLY (not debug-only),
      returning `FrameGraphRange{}` for an out-of-range value, mirroring
      `FrameProfiler::SetGpuPassSample()`'s own already-shipped handling of
      the exact same situation (new in v3, supersedes v2's debug-assert
      requirement).
- [ ] A dedicated test proves `Present`, `Absent`, AND `Unsupported` all
      round-trip correctly, simultaneously, across all three named
      `GpuPass` values in a single frame, with no cross-contamination
      between passes (Step 3.3, case 4) — not merely proven indirectly
      through the range-computation tests.
- [ ] Every CPU-millisecond assertion in the new test file uses an exact
      comparison (`EXPECT_DOUBLE_EQ`), not a tolerance-based one, matching
      the project rule "CPU values preserved exactly."
- [ ] The "several frames" order test and the ring-buffer-wraparound test
      both assert EXACT expected `frameIndex` values (mirroring
      `FrameProfilerTests.cpp`'s own `RingBufferWrapsAndKeepsMostRecentFrames`
      pattern), not merely "increasing"/"correct order" (new in v3, Step
      3.3 cases 3/5).
- [ ] A dedicated test proves `BuildFrameGraphPoints()` never observes an
      in-progress (`BeginFrame()`'d but not yet `EndFrame()`'d) frame (new
      in v3, Step 3.3 case 10).
- [ ] Every new function has a matching Tier-1 test in
      `tests/Profiling/FrameGraphDataTests.cpp`, added in the SAME change
      (`AGENTS.md`'s "Every change to Tier 1 code must come with a
      matching test change").
- [ ] Both new source files and the new test file are added to their
      respective ALREADY-UNCONDITIONAL CMake lists — no new
      `GTE_ENABLE_*` option introduced.
- [ ] Full clean build + full `ctest` run, both green, before calling this
      phase done — not just "it compiles." The exact passing-test count
      used in any commit message/status-doc update is taken from a FRESH
      run performed at that time, not copied from this planning document.
- [ ] `AGENTS.md`'s "Profiling" section updated with a short pointer to
      this new file, in the same change.
- [ ] `PROFILER_IMPLEMENTATION_STATUS_v2.md` updated (or a `_v3` revision
      created) moving Phase 2 from "not implemented" to "implemented,"
      with concrete file/function/test-count detail, in the same spirit as
      its existing Phase 0/1 entries.

### 5.3 What happens after this phase lands

Once Phase 2 is done and verified, the natural next step — per
`PROFILER_STRATEGY_v2.md`'s own explicit ordering, and per
`PROFILER_IMPLEMENTATION_STATUS_v2.md`'s own "Suggested next steps" list —
is **Phase 3 (draw-call and triangle counts)**, which is similarly cheap
and mechanical, followed by **Phase 5 (GPU memory history)**, before
**Phase 7 (the actual Editor panel)** finally has enough real data
(frame-time graph points, draw/triangle counts, memory history) to be worth
building at all. Do not jump to Phase 4 (GPU timestamp queries) or Phase 6
(benchmark mode) before Phases 3/5 exist and their own tests pass — both
are, by this plan's own explicit design, consumers of a data model that
should be fully assembled first, exactly the same reasoning that governed
sequencing Phase 2 itself ahead of them.

### 5.4 A closing thought, in the spirit of this document's own opening

A single, well-scouted river crossing, taken deliberately and verified
before the next is attempted, wins more campaigns than a dozen ambitious
ones attempted at once. Phase 2 is a small, low-risk crossing by design —
treat it that way: implement it completely, test it thoroughly, document it
honestly, and stop there. The next phase's strategy can be written once
this one is real.

--------------------------------------------------------------------------
## Result (filled in after implementation)
--------------------------------------------------------------------------

Phase 2 was implemented on `feature/profiler-impl` following this plan
exactly, with one genuine test-authoring gap discovered only once actual
test-writing started, and fixed properly (a small, test-only addition to
`FrameProfiler`) rather than worked around, per a follow-up review — see
below:

- `src/Profiling/FrameGraphData.h/.cpp` added exactly as designed:
  `FrameGraphPoint`/`FrameGraphRange`, `BuildFrameGraphPoints()`,
  `ComputeCpuMillisecondsRange()`/`ComputeGpuMillisecondsRange()` (the
  latter two accepting `std::span<const FrameGraphPoint>`), with
  `ComputeGpuMillisecondsRange()`'s out-of-range `pass` handled via the
  revised unconditional bounds-check-and-return-`FrameGraphRange{}`
  requirement (Step 3.2), not the superseded debug-only assert.
- `tests/Profiling/FrameGraphDataTests.cpp` added with **12** tests (10
  minimum required by Step 3.3, plus the two extra cases 9 split into one
  test per tri-state, `Absent` and `Unsupported`, rather than one combined
  test — a small, deliberately stronger split, not a shortcut).
- **A genuine test-authoring gap, and how it was actually resolved (worth
  recording honestly, including the false start)**: cases 2 and 6 describe
  seeding a frame with a "known" `cpuFrameMilliseconds` value, but
  `FrameProfiler` has no test hook to force that field to a literal — it is
  always genuinely computed from `SDL_GetPerformanceCounter()` inside
  `EndFrame()`. A first pass worked around this with an `SDL_Delay()`-based
  helper (reading the real, actually-measured value back via `HistoryAt()`
  right after recording it, using increasing delays to separate frames'
  real elapsed time). **On review, this was judged the wrong fix and
  replaced**: real-wall-clock-timing-based tests are flaky by construction
  (OS scheduling jitter, worse under CI load) and strictly weaker than the
  bit-exact, literal-value assertions this codebase's testing convention
  uses everywhere else (e.g. `SetGpuPassSample(pass, status, 3.5, 10,
  200)`'s own hand-chosen literals) — and there turned out to be a better
  option than tolerating that trade-off: add a small, narrowly-scoped,
  test-only method, `FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting(double)`
  (`src/Profiling/FrameProfiler.h/.cpp`), in the exact same spirit as the
  already-existing `ResetForTesting()`. It overwrites only the most
  recently completed history entry's `cpuFrameMilliseconds` with an exact
  caller-chosen literal, touching nothing else (not the in-progress frame,
  not any other retained entry, not any of `BeginFrame()`/`EndFrame()`/
  `RecordCpuScope()`/`SetGpuPassSample()`/`SetMemorySnapshot()`'s own
  real-time behavior) — a deliberate, narrow addition to `FrameProfiler`'s
  TEST-ONLY surface, not a change to its production contract. This
  reconciles cleanly with Step 4's "no change to `FrameProfiler`" refusal:
  that refusal was written on the assumption that Phase 2 needed nothing
  new from `FrameProfiler`'s *production* API, which remains true and
  unchanged; it did not anticipate this specific test-support gap, which is
  a different, narrower thing. `tests/Profiling/FrameProfilerTests.cpp`
  gained two new tests for this method
  (`OverrideLastFrameCpuMillisecondsForTestingReplacesOnlyTheMostRecentEntry`,
  `OverrideLastFrameCpuMillisecondsForTestingOnEmptyHistoryIsNoOp`), per
  `AGENTS.md`'s "every change to Tier 1 code must come with a matching test
  change" rule, and `FrameGraphDataTests.cpp`'s cases 2/6 were rewritten to
  use literal, hand-chosen values via this hook instead of `SDL_Delay()` —
  fully deterministic, zero added wall-clock test time, and a strictly
  stronger assertion.
- CMake wiring landed exactly as Step 3.4 described: both new source files
  added to the root `CMakeLists.txt`'s already-unconditional `gte_core`
  file list, and the new test file added to `tests/CMakeLists.txt`'s
  already-unconditional `GTE_TEST_SOURCES` list plus its own new "Test
  taxonomy" header-comment bullet — no new CMake option introduced either
  place.
- **Build + test verification**: `cmake -S . -B build -G Ninja` then
  `cmake --build build` succeeded cleanly (no new warnings), and a fresh
  `ctest --test-dir build --output-on-failure` run reported **464/464
  passing** (450 pre-existing + 12 new `FrameGraphDataTests.cpp` + 2 new
  `FrameProfilerTests.cpp`), with the exact same single pre-existing,
  unrelated, machine-gated smoke test skipped as before
  (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`) —
  zero regressions. This "464" was freshly counted for this result, not
  copied forward from this document's own earlier "450" mention.
- `AGENTS.md`'s "Profiling" section gained two new bullet additions: one
  pointing at `src/Profiling/FrameGraphData.h/.cpp`, and one documenting
  `FrameProfiler::OverrideLastFrameCpuMillisecondsForTesting()` right
  alongside its existing `ResetForTesting()` mention.
- `PROFILER_IMPLEMENTATION_STATUS_v2.md` was superseded by a new
  `PROFILER_IMPLEMENTATION_STATUS_v3.md` (moving Phase 2 from "not
  implemented" into "implemented," with the v2 file deleted per this
  codebase's own established "bump the version, delete the superseded
  file" convention — the same thing that happened to v1 when v2 was
  created), rather than silently edited in place.

Every item in Step 5.2's non-negotiable checklist above is satisfied. Phase
3 (draw-call/triangle counts) is the natural next crossing, per Step 5.3.
