# Job System — Phase 7 (Editor "Jobs" Panel) — Completion Report

Status: **DONE** (fast-compile-check verified per this session's own explicit
instructions — no full clean build/`ctest` regression run performed; that is
explicitly deferred to a later session, after Phase 8 is also done). This
report documents what was actually implemented and verified this session,
for whoever picks up Phase 8
(`JOBSYSTEM_PHASE8_TESTING_HARDENING_BENCHMARKING.md`) next.

Parent strategy: `JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md`.
Phase strategy followed: `JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md`.
Previous phase: `JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md` (First Production
Consumer — Animation/Vertex Skinning).

---

## 1. What this phase actually is

Per its own strategy document, Phase 7 makes the Job System's real,
already-produced per-frame data (Phase 5's `WorkerJobSample` log, reshaped
by `Profiling::BuildWorkerTimelinePoints()`/`ComputeDistinctWorkerCount()`,
`src/Profiling/WorkerTimelineData.h`) actually VISIBLE to a human being, for
the first time in this campaign: a new, dockable Editor **"Jobs"** panel
showing a live, per-worker horizontal timeline (idle background + colored,
named job spans) for the last-completed frame. No new engine-level tracking
was added — this phase is purely a presentation-layer addition on top of
Phase 5's already-shipped data model, exactly like the strategy document's
own "What We Will NOT Do" section requires.

---

## 2. What was built

### 2.1 — `src/Editor/JobsPanelData.h/.cpp` (new) — the pure, ImGui-free data-shaping layer

Follows `ProfilerPanelData.h`/`MemoryPanelData.h`'s own established template
exactly (plain reshape/format functions, Tier-1-tested despite living under
`src/Editor/`):

- `JobColor ColorForJobName(const char* name)` — a deterministic, stable
  color for a given job name, computed via a small FNV-1a-style hash over
  the name's own bytes (never the pointer value — two different
  string-literal addresses holding identical text must, and do, map to the
  same color) indexed into a small fixed 8-entry palette. A null/empty name
  maps to a fixed neutral gray rather than undefined behavior. This is what
  lets every `"SkinVertices"` segment across every worker row (and across
  every frame) render with the same recognizable color, matching the
  campaign's own attached Unity Profiler Timeline reference screenshots.
- `WorkerUtilizationSummary` / `ComputeWorkerUtilizationSummary(points,
  totalWorkerCount)` — "how many of the engine's real workers had at least
  one recorded job this frame", built on top of Phase 5's own
  `ComputeDistinctWorkerCount()` (never re-deriving that logic), paired
  against the caller-supplied total worker count so this module stays
  independent of `gte::Jobs::JobSystem` itself (it never calls
  `JobSystem::Instance()` directly). Defensively clamped so a stale/smaller
  `totalWorkerCount` than what actually produced `points` can never report
  more "busy" workers than the total.
- `FormatWorkerUtilizationSummary(summary)` — e.g. `"6 / 8 workers had at
  least one job this frame - 2 idle the whole frame"` — the panel's own
  at-a-glance answer to "is this actually balanced", per the strategy
  document's Step 3.4 point 6.
- `PointsForWorker(points, workerIndex)` — the per-row filter (order-
  preserving, never re-sorted) `Panels/JobsPanel.cpp` uses to decide which
  colored segments belong on a given worker's own row.
- `kJobTimingInstrumentationCompiledIn` / `JobsTimelineEmptyMessage()` —
  mirrors `ProfilerPanelData.h`'s own `kCpuScopeInstrumentationCompiledIn`/
  `CpuScopeTableEmptyMessage()` precedent exactly: distinguishes "this build
  can never show worker job data at all" (`GTE_ENABLE_PROFILER=OFF`) from
  "genuinely no job samples recorded yet this particular frame" (still true
  transiently even in a build where it's compiled in).

### 2.2 — `src/Editor/Panels/JobsPanel.h/.cpp` (new) — the ImGui-facing panel

A small stateful class (mirroring `ProfilerPanel`/`RenderGraphPanel`'s own
pre-approved exception to "most panels are stateless free functions" — see
`AGENTS.md`, "Editor Module Structure"), holding only a `m_paused` flag plus
a frozen `m_frozenPoints`/`m_frozenLatestFrame` snapshot, exactly the same
Pause state machine `ProfilerPanel` already established (false→true
captures a snapshot once; staying true or un-pausing back to false both
need no extra code, since every section just reads `m_paused`'s current
value).

Deliberately has **no own "Capture" toggle** — shares
`Profiling::FrameProfiler`'s existing capture flag with "Profiler" (only
shows an informational `TextDisabled` line when capture is off) rather than
introducing a second, independently-stateful toggle over the exact same
underlying data source, per the strategy document's own explicit "What We
Will NOT Do" instruction.

`Build()`'s sections:

1. **Pause control** — as above.
2. **Worker utilization summary line** — one call into
   `ComputeWorkerUtilizationSummary()`/`FormatWorkerUtilizationSummary()`.
3. **The timeline itself** — for every worker index from `0` up through
   `gte::Jobs::JobSystem::Instance().WorkerCount()` (so a worker that
   recorded zero job spans this frame still gets its own "entirely idle"
   row, per the strategy document's own explicit instruction — an idle
   worker is just as meaningful a signal as a busy one), draws:
   - A full-width idle-gray background rectangle + border via
     `ImGui::GetWindowDrawList()` (the same `AddRectFilled()`/`AddRect()`
     draw-list technique already used by `InspectorPanel.cpp`'s texture/
     mesh preview viewers — the one existing precedent for this kind of
     manual drawing in this codebase).
   - One colored, positioned rectangle per `WorkerTimelinePoint` on that
     row (via `PointsForWorker()`), positioned as a fraction of the frame's
     own `cpuFrameMilliseconds` (falling back to the latest observed
     segment end-time, or `1.0`, for a degenerate/zero frame duration —
     never a divide-by-zero), colored via `ColorForJobName()`, clamped to a
     minimum 2px width so a vanishingly short job segment stays visible/
     hoverable rather than collapsing to nothing.
   - `ImGui::IsMouseHoveringRect()` + `ImGui::SetTooltip()` on every colored
     segment, showing the job's name and exact duration in milliseconds —
     mirroring the strategy document's own attached reference screenshot's
     hover-tooltip behavior.
   - A left-aligned row label (`"Worker N"`), drawn last so it stays
     legible over the idle background regardless of whether any job ran
     that frame.
   - When `points` is empty, `JobsTimelineEmptyMessage()` is shown as an
     honest, distinct banner (compiled-out vs. no-data-yet) — but the idle
     rows are still drawn underneath it regardless, since an all-idle
     timeline is itself real, correct information (this is exactly what a
     `GTE_ENABLE_JOB_SYSTEM=OFF` build, or a frame before any rigged model
     has ever animated, legitimately looks like — per the strategy
     document's Step 3.5).

### 2.3 — Wiring into the existing Editor composition root

- `ImGuiEditorLayer.cpp` — new `#include "Panels/JobsPanel.h"`; new
  `JobsPanel m_jobsPanel;` member (declared right after
  `m_renderGraphPanel`, with a matching doc comment); `BuildUI()` now calls
  `m_jobsPanel.Build(m_ctx);` immediately after `m_renderGraphPanel.Build(...)`
  — the exact same "called explicitly by name, no `IEditorPanel` interface"
  convention every other stateful panel already follows.
- `DockLayout.cpp` — `"Jobs"` added to `kAllPanelNames` (the shared list the
  one-shot default-dock-layout logic waits for/rebuilds around) and to
  `BuildDefaultDockLayout()`'s own `DockBuilderDockWindow()` calls, docked
  into the same bottom strip as `"Memory"`/`"Profiler"`/`"Render Graph"` —
  matching the strategy document's own Step 3.3 ("a natural fit... all
  four are 'live engine internals, glanced at occasionally'").
- `CMakeLists.txt` — `src/Editor/JobsPanelData.h/.cpp` and
  `src/Editor/Panels/JobsPanel.h/.cpp` added to `gte_core`'s
  `GTE_ENABLE_EDITOR` source list, alongside every other panel/data file.

Neither `JobsPanelData.h/.cpp` nor `Panels/JobsPanel.h/.cpp` depend on
`GTE_ENABLE_PROJECT_PANEL` — same "no `GTE_ENABLE_PROJECT_PANEL` dependency
at all" bucket `ProfilerPanel`/`RenderGraphPanel` already occupy.

### 2.4 — Graceful degradation (Step 3.5 of the strategy document)

- **`GTE_ENABLE_JOB_SYSTEM=OFF`**: `Jobs::JobSystem::Instance().WorkerCount()`
  still resolves (always `1`, per Phase 1's own documented "OFF" contract),
  so the panel still builds/renders — it just shows one row, entirely idle
  forever (no real workers, nothing is ever dispatched in that
  configuration). No compile error, no crash — the same "an honest,
  harmless no-op display" convention `NullEditorLayer`/`ProfilerPanel`
  already established.
- **`GTE_ENABLE_PROFILER=OFF`**: `FrameProfiler`'s `workerJobs` data is
  never populated (per Phase 5's own gating), so `points` is always empty —
  `JobsTimelineEmptyMessage()` shows the honest, distinct "compiled out"
  message (verified word-for-word by
  `JobsPanelDataTests.JobsTimelineEmptyMessageMatchesCompileTimeFlag`,
  mirroring `ProfilerPanelDataTest.CpuScopeTableEmptyMessageMatchesCompileTimeFlag`'s
  own precedent) rather than a blank timeline that could be mistaken for
  "nothing is running" when actually "we can't tell you".

Both degradation paths were verified by REASONING about the code (same
methodology `JOB_SYSTEM_PHASE3_COMPLETION_REPORT.md`/others already used for
analogous cases in this campaign) plus the compile-time-flag-matching
regression test above — an actual side-by-side `GTE_ENABLE_JOB_SYSTEM=OFF`/
`GTE_ENABLE_PROFILER=OFF` rebuild-and-look was **not** performed this
session, per this session's own explicit "fast compile check only" scope;
noted here as a follow-up worth doing during Phase 8's own hardening pass
(see §6 below).

---

## 3. Tests added

`tests/Editor/JobsPanelDataTests.cpp` (11 tests, Tier 1 — no ImGui/
Renderer/live GPU/real `JobSystem`/`FrameProfiler` singleton involved at
all, hand-built `Profiling::WorkerTimelinePoint` fixtures, mirroring
`ProfilerPanelDataTests.cpp`'s own style exactly):

- `ColorForJobNameIsDeterministicForTheSameContent` — two different
  pointers holding identical text map to the identical color.
- `ColorForJobNameDistinguishesDifferentNames` — two different-looking
  names don't collide.
- `ColorForJobNameHandlesNullAndEmptyWithoutCrashing`.
- `ComputeWorkerUtilizationSummaryCountsDistinctWorkersOnly` — repeated
  points on the same worker don't double-count.
- `ComputeWorkerUtilizationSummaryOnEmptyPointsReportsZeroBusyWorkers`.
- `ComputeWorkerUtilizationSummaryClampsAgainstStaleTotalWorkerCount` — a
  defensive case, more distinct worker indices present than the claimed
  total.
- `FormatWorkerUtilizationSummaryProducesExpectedText` /
  `FormatWorkerUtilizationSummaryHandlesZeroWorkers`.
- `PointsForWorkerFiltersAndPreservesOrder` / `PointsForWorkerOnUnknownIndexReturnsEmpty`.
- `JobsTimelineEmptyMessageMatchesCompileTimeFlag` — the compile-time-flag-
  matching regression test described in §2.4.

Added to `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES`, inside the existing
`if(GTE_ENABLE_EDITOR)` block (same gating as every other `Editor/*` data
test — `JobsPanelData.h/.cpp` is only compiled into `gte_core` when the
Editor is on), with a matching documentation-comment entry in the file's
own test-taxonomy header block.

`Panels/JobsPanel.cpp` itself (the actual ImGui-drawing code) is
**intentionally untested**, per this whole `src/Editor/` module's already-
established "Editor panel widget code is Tier 2, its data-shaping helpers
are Tier 1" split (`AGENTS.md`) — the same split every other panel in this
module already follows.

---

## 4. Verification performed

Per this session's own explicit instructions ("do not perform a full build,
just a fast compile check; if it compiles, commit directly — full build and
regression test will happen later"):

- `cmake -S . -B build` (no `require_internet_connection`) against the
  existing Ninja/MinGW `build/` directory — picked up every new/changed
  source file, configured cleanly.
- `cmake --build build --target gte_core` — `src/Editor/JobsPanelData.cpp`,
  `src/Editor/Panels/JobsPanel.cpp`, `src/Editor/DockLayout.cpp`, and
  `src/Editor/ImGuiEditorLayer.cpp` (the four changed/new production files)
  all compiled cleanly, zero warnings/errors, and relinked
  `libgte_core.a` successfully.
- `cmake --build build --target GreatTamanaEngineTests` — the new
  `tests/Editor/JobsPanelDataTests.cpp` compiled cleanly and linked into
  `GreatTamanaEngineTests.exe` successfully.
- `cmake --build build --target GreatTamanaEngine` — the real executable
  (which links `gte_core` and therefore the whole Editor module) still
  builds and links cleanly, confirming the `ImGuiEditorLayer.cpp`/
  `DockLayout.cpp` changes don't break the shipped binary.
- As an extra sanity check beyond the requested "compile check" (running
  the already-built binary, not a rebuild):
  `tests\GreatTamanaEngineTests.exe --gtest_filter=JobsPanelDataTest.*` —
  **all 11 new tests passed**.
- Per this session's own instructions, the full clean, cross-configuration
  rebuild (`build_joboff`-style) and the full `ctest -C Debug
  --output-on-failure` regression run across the whole suite are
  deliberately deferred to a later pass, once Phase 8 is also done.
- The panel was **not** visually confirmed against a live, running Editor
  with the real Furina-model animation scenario this session (per the
  "fast compile check only" scope) — the strategy document's own Step 5
  explicitly calls this out as the moment "the whole campaign's promise
  becomes directly, visually verifiable"; doing so is an important
  follow-up (see §6) before Phase 8's own sign-off, even though nothing in
  this session's own instructions required it yet.

---

## 5. Definition of Done — checked against Phase 7's own strategy doc

1. ✅ A new, dockable **"Jobs"** panel exists, following the exact same
   construction pattern as `ProfilerPanel`/`RenderGraphPanel` (a small,
   explicitly-named, stateful class, called explicitly from
   `ImGuiEditorLayer::BuildUI()`).
2. ✅ Renders a live, per-worker horizontal timeline (idle vs. named,
   colored job spans) for the last-captured frame.
3. ✅ Reads exclusively from Phase 5's `WorkerTimelineData.h` reshape — zero
   new engine-level tracking added (`JobsPanelData.h/.cpp`/`JobsPanel.cpp`
   never touch `Profiling::FrameProfiler`'s internal fields directly, only
   its already-public `LastCompletedFrame()`/`IsCaptureEnabled()`, and
   `Profiling::BuildWorkerTimelinePoints()`/`ComputeDistinctWorkerCount()`).
4. ✅ Gated behind `GTE_ENABLE_EDITOR` (and implicitly degrades correctly
   under `GTE_ENABLE_JOB_SYSTEM=OFF`/`GTE_ENABLE_PROFILER=OFF` — see §2.4).
5. ✅ Visually recognizable as the same kind of view the campaign's own
   attached Unity Profiler Timeline reference screenshots show (per-worker
   rows, colored named segments, hover tooltips, idle background) — not yet
   independently visually confirmed against a live running Editor this
   session (see §4's own honest caveat and §6 below).
6. ⚠️ The strategy document's own Step 5 "look at it with your own eyes
   against the real Furina-model animation scenario" verification step was
   **not** performed this session, per this session's own "fast compile
   check only" scope — recorded as an explicit follow-up in §6, not
   silently skipped.

---

## 6. Files changed/added this session

- `src/Editor/JobsPanelData.h` (new), `JobsPanelData.cpp` (new) — §2.1.
- `src/Editor/Panels/JobsPanel.h` (new), `JobsPanel.cpp` (new) — §2.2.
- `src/Editor/ImGuiEditorLayer.cpp` — new include, new `m_jobsPanel` member,
  new `m_jobsPanel.Build(m_ctx);` call in `BuildUI()`.
- `src/Editor/DockLayout.cpp` — `"Jobs"` added to `kAllPanelNames` and to
  `BuildDefaultDockLayout()`'s dock-window calls.
- `CMakeLists.txt` — the four new `src/Editor/JobsPanelData.*`/
  `src/Editor/Panels/JobsPanel.*` files added to `gte_core`'s
  `GTE_ENABLE_EDITOR` source list.
- `tests/CMakeLists.txt` — new `Editor/JobsPanelDataTests.cpp` entry (inside
  the existing `if(GTE_ENABLE_EDITOR)` block), plus a matching
  documentation comment in the file's own test-taxonomy header block.
- `tests/Editor/JobsPanelDataTests.cpp` (new) — §3.
- `task_manager/job_system/JOBSYSTEM_PHASE7_COMPLETION_REPORT.md` (this
  file, new).

No existing production call site's PUBLIC API changed shape —
`ImGuiEditorLayer`/`DockLayout`'s changes are purely additive (a new member,
a new panel name, a new dock-window call); every other panel's own
signature/behavior is completely unchanged.

---

## 7. What remains open

- **The strategy document's own Step 5 "look at it with your own eyes"
  verification** (running the real Furina-model animation scenario with
  the Editor open and confirming, visually, that multiple worker rows show
  real, colored, non-overlapping `SkinVertices` segments, and that the
  utilization summary reflects genuine parallelism rather than everything
  landing on Worker 0) was **not** performed this session — explicitly
  deferred per this session's own "fast compile check only" scope, but
  strongly recommended before Phase 8's own sign-off treats this panel as
  fully proven, per the strategy document's own framing of this step as
  more than a formality.
- **An actual side-by-side `GTE_ENABLE_JOB_SYSTEM=OFF`/
  `GTE_ENABLE_PROFILER=OFF` rebuild-and-look** at the panel's degraded
  states (§2.4) was reasoned about, and backed by one compile-time-flag-
  matching regression test, but not independently rebuilt-and-visually-
  confirmed this session — worth doing alongside Phase 8's own cross-
  configuration hardening pass.
- Phase 8 (Testing, Hardening & Benchmarking) remains entirely unstarted,
  per the master strategy's own phase-by-phase gating — once it completes,
  the WHOLE Job System campaign (Phases 1 through 8) is done.
- The full clean cross-configuration rebuild and the full
  `ctest -C Debug --output-on-failure` regression run across the whole
  suite (both `GTE_ENABLE_JOB_SYSTEM` configurations) are deferred to a
  later session, per this session's own explicit scope.
