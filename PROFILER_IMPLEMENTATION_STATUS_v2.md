# Profiler Implementation Status (v2)

Status: LIVING DOCUMENT — reflects exactly what exists in the codebase as
of the fix-up commit made directly on top of `23c2a44` on
`feature/profiler-impl` (see "Changelog: v1 -> v2" below for exactly what
changed and why). Update this file (or fold it into `TODO.md`/`README.md`
and delete it) the next time a Profiler phase is implemented, rather than
letting it silently drift out of sync with reality — see
`PROFILER_STRATEGY_v2.md`'s own closing section for why this codebase
treats planning/status documents this way.

This file exists purely to answer, for anyone (human or AI agent) picking
this up next: **"what already works, what doesn't exist yet, and why was
it deliberately left for later rather than done now?"** It does not repeat
`PROFILER_STRATEGY_v2.md`'s own design reasoning in full — read that
document for the *why behind the plan itself*; this document is only about
*how much of that plan is actually built*.

--------------------------------------------------------------------------
## Changelog: v1 -> v2 (read this first)
--------------------------------------------------------------------------

v1 of this document (`PROFILER_IMPLEMENTATION_STATUS.md`, written by a
human reviewing the session's own commit) was independently checked
against the actual code and `ctest` output. **Its overall structure and
conclusions were correct** — Phase 0/1 genuinely work, Phases 2-7
genuinely don't exist yet, and the reasoning for deferring each one
matches `PROFILER_STRATEGY_v2.md`'s own phase ordering. Two concrete
inaccuracies were found and are fixed below, plus the bug v1 flagged as a
"known rough edge" has now actually been fixed in code (not just
documented):

1. **The `"Game::Update"` double-instrumentation v1 flagged as a "rough
   edge" was a real bug, confirmed by re-reading both call sites, and has
   now been FIXED** (not merely documented) — see "What changed since v1"
   below. v1's own suggested fix (remove the outer wrap in
   `Application.cpp`, keep the inner one in `Game::Update()` itself, since
   it's closer to the actual work being measured) is exactly what was
   applied.
2. **v1's test counts were wrong — verified directly against `ctest`'s own
   numbered output, not re-derived from source by eye.** v1 claimed
   `FrameProfilerTests.cpp` has "14 tests" and `ScopeTimerTests.cpp` has
   "6 tests, split by `#if GTE_ENABLE_PROFILER`" (implying 20 new tests
   total, matching the *previous* session's own commit message, which
   made the identical miscount). The actual numbers, confirmed by
   `ctest`'s own test IDs (`306` through `321` inclusive - test 305 is the
   last pre-existing test, `InputStateTest.QuitAndWindowResizedEvents_...`,
   and test 322 is the next pre-existing one,
   `SdlMemoryTrackerTest.MallocIncreasesLiveBytesAndCount_FreeUndoesIt`):
   - `ScopeTimerTests.cpp` compiles **3** tests in the default
     (`GTE_ENABLE_PROFILER=ON`) build (`ScopeTimerRecordsAPositiveDurationIntoTheCurrentFrame`,
     `NestedScopesEachGetTheirOwnFlatEntry`,
     `ScopeTimerRecordsNothingWhenCaptureIsDisabled`), plus **1** separate
     test (`CompiledOutScopeTimerConstructsWithoutRecordingAnything`) that
     only compiles under the `#else` (`GTE_ENABLE_PROFILER=OFF`) branch —
     never both at once. So "6" should have been "3 in the default build
     (4 total possible across both build configurations, mutually
     exclusive)".
   - `FrameProfilerTests.cpp` compiles **13** tests, not 14.
   - Total new tests added this session, in the default build
     configuration: **16** (3 + 13), not 20. This is independently
     confirmed by simple arithmetic: the full suite was 434 tests before
     this session (see `README.md`'s own "Status" section, which was last
     updated to say "434 tests, 1 pre-existing machine-gated smoke test
     skipped" before this session started) and is 450 now — a difference
     of exactly 16, not 20.

Everything else in v1 below is reproduced essentially unchanged (only
re-worded where the fix above needed to be reflected inline), since it
was independently re-verified against the actual source tree and found
accurate.

--------------------------------------------------------------------------
## What changed since v1 (the actual code fix)
--------------------------------------------------------------------------

`src/Application/Application.cpp`'s `Application::Run()` no longer wraps
`m_game.Update(deltaSeconds, inputState)` in its own
`GTE_PROFILE_SCOPE("Game::Update")` — that call site now just calls
`Update()` directly (with a comment explaining why, pointing at
`Game::Update()`'s own internal scope in `src/Game/Game.cpp`, which is
unchanged and still the ONLY `"Game::Update"` scope left). This closes the
double-counting v1 flagged: `FrameSample::cpuScopes`'s `"Game::Update"`
entry now gets `callCount == 1` and a non-doubled `totalMilliseconds` per
frame, exactly as every other single-instrumented scope name already did.

No other call site had this problem — verified by re-reading every
`GTE_PROFILE_SCOPE(...)` call site in the codebase (`Application.cpp`,
`Game.cpp`, `AnimationSystem.cpp`, `RenderSystem.cpp`) and confirming each
distinct scope name appears in exactly one place.

Rebuilt cleanly (`cmake --build build --config Debug`) and re-ran the full
suite (`ctest -C Debug`) after this fix: **450/450 passing**, same as
before the fix (the fix does not add or remove a test — it corrects
runtime accumulation behavior only) — no test asserted on the buggy
double-counted behavior, so nothing needed updating on the test side.

--------------------------------------------------------------------------
## Quick answer: is there a Profiler window in the Editor?
--------------------------------------------------------------------------

**No.** The engine is silently *collecting* real per-frame CPU timing data
every frame (see "What was implemented" below), but there is currently
**no UI, no CSV export, no log line — nothing that displays it anywhere.**
Opening the Editor today still only shows the pre-existing panels
(Hierarchy/Inspector/Scene/Game/Memory/Project) — no "Profiler" tab exists
yet. See "What was NOT implemented" for exactly why, and what it would
take to add one.

--------------------------------------------------------------------------
## What was implemented this session
--------------------------------------------------------------------------

This session implemented **Phase 0 (Foundation)** and **Phase 1 (wired
into existing call sites)** of `PROFILER_STRATEGY_v2.md`'s 8-phase plan —
the first two of eight, done in the order the strategy document itself
mandates ("implement phases in order... do not start Phase 4/6 before
Phases 0-3 exist").

### Phase 0 — the data model + the on/off switch

A new, always-compiled engine module, `src/Profiling/` (no
`GTE_ENABLE_EDITOR` dependency at all — same tier as `src/Animation/`/
`src/Assets/`), gated by its own new `GTE_ENABLE_PROFILER` CMake option
(`ON` by default, `PUBLIC`-defined exactly like `GTE_ENABLE_EDITOR`/
`GTE_ENABLE_PROJECT_PANEL`):

- **`src/Profiling/ProfilingTypes.h`** — the plain data shapes:
  - `CpuScopeSample` — one named CPU scope's aggregated
    `{name, totalMilliseconds, callCount}` for the CURRENT frame only. The
    model is deliberately **flat, not a nested tree** — every scope with
    the same name, no matter how deeply nested, sums into one entry (see
    `PROFILER_STRATEGY_v2.md`, Phase 0's own "hierarchy vs. flat list"
    decision). **This flat-summing behavior is precisely what made the
    now-fixed `"Game::Update"` double-instrumentation bug possible in the
    first place** — two distinct call sites sharing one name are
    indistinguishable from one call site invoked twice, by design (see
    "Known rough edges" below for why this is an accepted model
    limitation, not something to redesign around).
  - `GpuSampleStatus` — a tri-state enum (`Absent` / `Present` /
    `Unsupported`), so a GPU measurement that didn't happen this frame is
    never confused with one that measured exactly `0`. **Not fed by
    anything real yet** — see below.
  - `GpuPass` — a small, fixed enum (`GameView` / `SceneView` / `Present`)
    and `GpuPassSample` — storage for a future GPU-timing phase, unused
    today.
  - `MemorySnapshot` — a plain, Vulkan-free copy of
    `GpuMemoryTracker::Totals`'s shape — storage for a future GPU-memory-
    history phase, unused today.
  - `FrameSample` — one whole frame's record: `frameIndex`,
    `cpuFrameMilliseconds`, a fixed `std::array<CpuScopeSample, 64>`, a
    fixed `std::array<GpuPassSample, 3>`, and a `MemorySnapshot`. Every
    field is fixed-size/POD — no heap allocation anywhere in this struct.

- **`src/Profiling/FrameProfiler.h/.cpp`** — the actual collector, a
  Meyers singleton (`FrameProfiler::Instance()`):
  - `BeginFrame()`/`EndFrame()` bracket one frame, pushing a completed
    `FrameSample` into a **fixed-capacity, 300-frame ring buffer**
    (`kMaxFrameHistory`) — allocated once, never grown.
  - `RecordCpuScope(name, ms)` — the flat aggregation-by-name step (called
    by `ScopeTimer`'s destructor, never directly).
  - `SetGpuPassSample()`/`SetMemorySnapshot()` — the write API a future
    phase would call; nothing calls these yet (see below).
  - `SetCaptureEnabled(bool)`/`IsCaptureEnabled()` — the **runtime** half
    of the two-layer on/off switch (Phase 0b): while disabled,
    `BeginFrame`/`EndFrame`/`RecordCpuScope`/etc. are all true no-ops (no
    clock read, no ring-buffer write). Defaults to `true`.
  - `HistoryCount()`/`HistoryAt()`/`LastCompletedFrame()` — read access to
    the ring buffer, oldest-to-newest.
  - `ResetForTesting()` — lets tests reset the shared singleton to a known
    baseline (mirrors the existing `SdlMemoryTracker`/`ImGuiMemoryTracker`
    testing convention).
  - This class **always compiles in**, regardless of `GTE_ENABLE_PROFILER`
    — only `ScopeTimer`'s body is gated (see below) — so it stays
    testable in every build configuration, the same precedent
    `SdlMemoryTracker` already established.
  - Uses `SDL_GetPerformanceCounter()`/`SDL_GetPerformanceFrequency()` as
    its one clock (never `std::chrono`), per
    `PROFILER_STRATEGY_v2.md`'s Step 3a.

- **`src/Profiling/ScopeTimer.h`** — the actual instrumentation API:
  - `GTE_PROFILE_SCOPE("Name")` — a macro that declares a local RAII
    `ScopeTimer` bound to the rest of the enclosing block. This is the
    **only** sanctioned way to add a new CPU profiling call site (see the
    new "Profiling" section in `AGENTS.md`).
  - Under `GTE_ENABLE_PROFILER=ON` (the default): reads the clock in its
    constructor/destructor and calls `FrameProfiler::RecordCpuScope()` —
    but skips even the clock read if `FrameProfiler::IsCaptureEnabled()`
    is `false`.
  - Under `GTE_ENABLE_PROFILER=OFF`: `ScopeTimer` becomes a genuinely
    **empty struct with a no-op constructor** — the compiler has nothing
    left to even inline away. This is the "true zero cost" release
    branch the strategy document's Step 1.3 success criteria describe.

- **`AGENTS.md`** gained a new **"Profiling"** section documenting these
  conventions (clock choice, flat-vs-nested model, the no-heap-allocation
  rule, the two-layer switch, the singleton/testing convention, the
  tri-state GPU/memory rule) so a future contributor has one place to
  learn the pattern from.

### Phase 1 — wired into existing per-frame call sites

`GTE_PROFILE_SCOPE(...)` was added at exactly the call sites
`PROFILER_STRATEGY_v2.md`'s Phase 1 names, and `FrameProfiler::Instance()`
`.BeginFrame()`/`.EndFrame()` now bracket the whole frame:

- **`src/Application/Application.cpp`** (`Application::Run()`):
  - `Profiling::FrameProfiler::Instance().BeginFrame()` at the very top of
    the `while (running)` loop, `.EndFrame()` at the very bottom.
  - `GTE_PROFILE_SCOPE("Application::PollEvents")` around the
    `SDL_PollEvent` loop.
  - `m_game.Update(deltaSeconds, inputState)` is called PLAIN here now
    (see "What changed since v1" above) — the actual
    `GTE_PROFILE_SCOPE("Game::Update")` lives ONLY inside
    `Game::Update()`'s own body (`src/Game/Game.cpp`), not duplicated at
    this call site anymore.
  - `GTE_PROFILE_SCOPE("Renderer::RenderOffscreen(GameView)")` and
    `GTE_PROFILE_SCOPE("Renderer::RenderOffscreen(SceneView)")` around
    each of the two `RenderOffscreen()` calls — named distinctly so their
    cost is never conflated, per the strategy document's own concern
    about the Game view vs. Scene view ambiguity.
  - `GTE_PROFILE_SCOPE("IEditorLayer::BuildUI")` around
    `m_editorLayer->BuildUI(...)`.
  - `GTE_PROFILE_SCOPE("Renderer::Present")` around `m_renderer.Present(...)`.
- **`src/Game/Game.cpp`** (`Game::Update()`): wrapped in
  `GTE_PROFILE_SCOPE("Game::Update")` — this is now the ONE and ONLY
  place this scope name is recorded from (see "What changed since v1").
- **`src/Game/Animation/AnimationSystem.cpp`** (`AnimationSystem::Update()`):
  wrapped in `GTE_PROFILE_SCOPE("AnimationSystem::Update")` — proves the
  flat model correctly handles a scope nested one level inside
  `Game::Update()`.
- **`src/Game/RenderSystem.cpp`**: `CollectRenderables()` wrapped in
  `GTE_PROFILE_SCOPE("RenderSystem::CollectRenderables")`, and the
  `Draw(Registry&, Renderer&, const Mat4&)` overload (the one that
  actually does the work; the `float aspectWidthOverHeight` overload just
  forwards into it) wrapped in `GTE_PROFILE_SCOPE("RenderSystem::Draw")`.

Every `GTE_PROFILE_SCOPE(...)` name used anywhere in the codebase today is
now confirmed to appear at exactly ONE call site (re-verified as part of
writing this v2 document) — the double-instrumentation class of bug v1
found is now closed, not just for `"Game::Update"` specifically but
confirmed absent everywhere else too.

### Testing

- **`tests/Profiling/FrameProfilerTests.cpp` (13 tests, not 14 — see
  Changelog above)** — ring buffer wraparound/most-recent-frames-kept,
  flat name aggregation, overflow (more than 64 distinct scope names in
  one frame) dropped without crashing, the runtime capture-enabled flag
  genuinely skipping everything, GPU-pass/memory tri-state defaulting to
  `Absent`, `SetGpuPassSample()`/`SetMemorySnapshot()` round-tripping,
  `LastCompletedFrame()` correctness (including on empty history).
- **`tests/Profiling/ScopeTimerTests.cpp` (3 tests in the default
  `GTE_ENABLE_PROFILER=ON` build; a 4th, separate test only compiles
  under `GTE_ENABLE_PROFILER=OFF` — never both at once, see Changelog
  above)** — a real `GTE_PROFILE_SCOPE` records a genuinely positive
  duration, nested scopes each get their own flat entry, and
  capture-disabled genuinely records nothing (plus the OFF-build variant:
  a compiled-out `ScopeTimer` constructs without recording anything).
- Both files were added to `tests/CMakeLists.txt`, unconditionally (no
  `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` guard needed, since this
  module has neither dependency).
- **Verified**: full clean build (`cmake --build build --config Debug`)
  succeeds, full test suite (`ctest`) reports **450/450 passing** (1
  pre-existing, unrelated, machine-gated smoke test skipped, as before
  this session) — this was re-verified again after the `"Game::Update"`
  double-instrumentation fix above, with the identical 450/450 result.
  The real `GreatTamanaEngine.exe` was also launched and observed running
  without crashing for a few seconds with the new instrumentation active
  (a manual Tier-2-style sanity check, not an automated test).

--------------------------------------------------------------------------
## What was NOT implemented, and why
--------------------------------------------------------------------------

Every phase below is a genuine gap, left for a future session — none of
these are "secretly done" or partially wired up anywhere. (Re-confirmed
unchanged from v1 — nothing in the v2 fix-up touched any of Phases 2-7.)

### Phase 2 — Frame-time graph data (the pure "history → plottable points" reshape)

**Not implemented.** No function anywhere turns `FrameProfiler`'s ring
buffer into an array of `(frameIndex, cpuMs, gpuMsOrAbsent)` points ready
for a plotting widget.

**Why deferred:** `PROFILER_STRATEGY_v2.md` explicitly sequences this
AFTER Phase 1 (CPU data collection actually working) but treats it as
its own reviewable step, and this session's scope/time budget was spent
making sure Phase 0/1's collection pipeline itself was correct and fully
tested first, rather than rushing ahead into a second phase without that
foundation being verified. Phase 2 is low-risk/mechanical once Phase 0/1
exist, so it's a natural, cheap next step for whoever picks this up.

### Phase 3 — Draw-call and triangle counts

**Not implemented.** `FrameRecorder::RecordFrame()`'s per-draw loop still
has no pure counting step extracted from it, and `FrameSample::gpuPasses`'
`drawCallCount`/`triangleCount` fields are never written to by anything.

**Why deferred:** Same reasoning as Phase 2 — it's a self-contained,
low-risk extraction (mirroring how `MeshVertexPacking`/
`MeshMaterialPartitioner` were pulled out of `Game.cpp` before), but this
session prioritized finishing Phase 0/1 correctly and fully tested over
starting a third phase in parallel.

### Phase 4 — Vulkan GPU timestamp queries

**Not implemented.** No `VkQueryPool`, no `vkCmdWriteTimestamp2` call,
no device timestamp-support query anywhere in the codebase.

**Why deferred:** This is explicitly called out in
`PROFILER_STRATEGY_v2.md` itself as **"the substantial technical phase"**
and the most technically risky one in the whole plan (query pool
reset/lifetime differs between the synchronous offscreen path and the
double-buffered swapchain path, real driver-specific edge cases are
likely). The strategy document's own risk/rollback note for this phase
says CPU-only profiling (Phases 0-3/5-7) is "already a complete, useful
deliverable on its own" without it — i.e. this phase was deliberately
saved for a dedicated session with room to debug real Vulkan
synchronization issues, rather than attempted piecemeal alongside
everything else.

### Phase 5 — GPU memory usage over time

**Not implemented.** Nothing calls `Renderer::GetMemoryTotals()` once per
frame to feed `FrameProfiler::SetMemorySnapshot()`, despite that
function already existing and already being O(1) (used today only by the
existing "Memory" panel's instantaneous snapshot).

**Why deferred:** `PROFILER_STRATEGY_v2.md` describes this as "by a wide
margin, the cheapest phase in the whole plan" — genuinely trivial once
Phase 0 exists. It was still left undone this session simply because
implementing it means touching `Application.cpp`/`Renderer` again for a
THIRD unrelated concern in the same session, and — with no Phase 7 UI to
actually display it yet — there would be no way to verify it visually,
only via another Tier-1 test. Cheap, but not yet done; a very short
follow-up.

### Phase 6 — Benchmark mode (headless-of-the-Editor CLI run)

**Not implemented.** No `--benchmark` CLI flag exists in `main.cpp`, no
CSV/summary exporter, no warm-up-frame handling, no fixed-`deltaSeconds`
simulation mode.

**Why deferred:** `PROFILER_STRATEGY_v2.md` explicitly designs this as a
PURE CONSUMER of Phases 0-5's data model — it would be premature (and
risk having to redesign the exporter) to build it before those phases'
actual shape is finalized, per the strategy document's own phase-ordering
rule ("if a phase before this one needs to change to make benchmark mode
work, that is a sign an earlier phase's design was wrong"). It also needs
a defined, reproducible workload (the strategy document proposes driving
`Game`'s existing spawn API via new CLI arguments) which is itself a
non-trivial design decision better made once there's real profiling data
worth benchmarking against.

### Phase 7 — The Editor "Profiler" panel

**Not implemented — this is the actual visible window you asked about.**
No `src/Editor/Panels/ProfilerPanel.h/.cpp`, no
`src/Editor/ProfilerPanelData.h/.cpp`, no "Profiler" entry in
`DockLayout.cpp`'s `kAllPanelNames`/default layout. Opening the Editor
today shows exactly the same panel set as before this session
(Hierarchy/Inspector/Scene/Game/Memory/Project) — nothing new to click on.

**Why deferred:** `PROFILER_STRATEGY_v2.md` deliberately sequences this
LAST ("the last phase, deliberately, since it has nothing of its own to
build except presentation on top of an already-proven data pipeline") —
building the panel before Phases 2-5 exist would mean either a panel that
only shows CPU scope names with no graph/GPU/memory data at all (a
half-feature), or reaching ahead and guessing at those phases' data
shapes before they're actually implemented and tested. This session
focused on proving the underlying collection pipeline (Phase 0/1) is
correct FIRST, exactly as the strategy document's own "implement phases
in order" instruction requires, rather than building a UI on top of data
that doesn't fully exist yet.

--------------------------------------------------------------------------
## Known rough edges in what WAS implemented
--------------------------------------------------------------------------

- **~~`"Game::Update"` was instrumented TWICE~~ — FIXED, see "What changed
  since v1" above.** No longer an open issue. Left here, struck through,
  rather than deleted outright, so a future reader who only skims this
  section still sees that this specific risk was checked for and closed,
  not merely never mentioned.
- **The flat, name-keyed aggregation model has NO protection against a
  future reintroduction of the same class of bug** — if two different
  call sites are ever given the same `GTE_PROFILE_SCOPE("...")` name
  again (whether by copy-paste mistake or a deliberate-but-wrong choice),
  `FrameProfiler` will silently sum them together with no warning,
  exactly as it did before this fix. This is an accepted, documented
  limitation of the deliberately-simple flat model (see
  `PROFILER_STRATEGY_v2.md`, Phase 0's own reasoning for choosing flat
  over a nested tree) — not something this fix-up changed or could
  practically change without abandoning the flat model entirely. Anyone
  adding a new `GTE_PROFILE_SCOPE` call site should grep for the exact
  name string first to confirm it isn't already used elsewhere, since
  nothing will catch a collision automatically.
- **`SetGpuPassSample()`/`SetMemorySnapshot()` are fully implemented and
  tested but have ZERO real callers** anywhere in the engine yet — every
  `FrameSample` collected today has all-`Absent` GPU passes and an
  all-`Absent` memory snapshot. This is expected/by-design (Phases 4/5
  are what would call these), not a bug, but worth stating plainly so
  nobody assumes GPU timing already works because the storage for it
  exists.

--------------------------------------------------------------------------
## How to verify the current state yourself
--------------------------------------------------------------------------

- Build: `cmake -S . -B build` then `cmake --build build --config Debug`.
- Test: `ctest --test-dir build -C Debug --output-on-failure` (expect
  450 passing, 1 skipped).
- There is currently no visual/CLI way to see the collected profiling
  data — the only way to inspect it today is to attach a debugger to
  `gte::Profiling::FrameProfiler::Instance()` while the engine runs, or
  to write a throwaway diagnostic (temporary log line, a quick unit test)
  against its public API (`LastCompletedFrame()`, `HistoryAt()`, etc.).
- To sanity-check the double-instrumentation fix specifically: grep the
  codebase for `GTE_PROFILE_SCOPE("Game::Update")` and confirm it appears
  exactly once (`src/Game/Game.cpp`), not in `src/Application/Application.cpp`
  anymore.

--------------------------------------------------------------------------
## Suggested next steps, in priority order
--------------------------------------------------------------------------

1. ~~Fix the double-`"Game::Update"` instrumentation rough edge~~ — DONE
   as part of this v2 pass.
2. Phase 2 (frame-time graph data reshape) and Phase 3 (draw-call/
   triangle counts) — both cheap, mechanical, low-risk.
3. Phase 5 (GPU memory history) — also cheap, per the strategy document's
   own assessment.
4. Phase 7 (the Editor "Profiler" panel) — once Phases 2/3/5 give it
   real data to show, even before Phase 4/6 exist (it can simply show
   "N/A" for GPU timing until Phase 4 lands, matching every other
   tri-state "absent" convention already established).
5. Phase 4 (GPU timestamp queries) and Phase 6 (benchmark mode) — the two
   most substantial remaining phases, best tackled as their own
   dedicated sessions per the strategy document's own risk framing.

See `PROFILER_STRATEGY_v2.md` for the full design reasoning behind every
phase above.
