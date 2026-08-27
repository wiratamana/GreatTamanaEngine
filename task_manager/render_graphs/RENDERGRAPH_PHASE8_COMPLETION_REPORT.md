# RENDERGRAPH_PHASE8_COMPLETION_REPORT.md

Session report for **Phase 8 — Add a Debug Window in the Editor**, the
eighth and final MVP implementation chunk of the Render Graph campaign
described in `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`. Scope was taken
from `RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md`'s own "Step 3:
The Plan", with a handful of deliberate, documented deviations described
below where the doc's own sketch needed a small adjustment once real code
was written (the same kind of finding every earlier phase's own completion
report has logged at least once).

## What shipped

### 1. `src/Renderer/RenderGraph/RenderGraphSnapshot.h`/`.cpp` (new)

The pure, ImGui-free "flatten a compiled graph into something displayable"
reshape the strategy document's own Step 3.1 calls for — `RenderGraphSnapshot`
(a `std::vector<RenderGraphPassSnapshot>` in real execution order, with every
CULLED pass appended after them, still visible and clearly tagged, plus a
`std::vector<RenderGraphResourceSnapshot>`), and `BuildRenderGraphSnapshot()`,
a pure function taking a `CompiledGraph`, a `CompiledGraphInput`, and a
caller-supplied `statsLookup` callback — never a live `RenderGraph&`, which is
what keeps this function genuinely Tier-1-testable (see "Deviation 1" below
for why this diverges from the strategy document's own literal signature
sketch).

- **`RenderGraphPassSnapshot`** — `name`/`readNames`/`writeNames` are
  resolved into plain, OWNED `std::string`s (never a raw `const char*` into
  `PassRecord`'s own name tables) so a captured snapshot stays valid/
  displayable indefinitely, exactly matching this phase's own Step 3.4 "this
  snapshot must outlive the frame it describes, for Pause support" note. A
  culled pass's `stats` field is always left at its default
  (`PassGpuStats{}` — empty `DrawStats`, an `Absent` `GpuTimingSample`) —
  `BuildRenderGraphSnapshot()` deliberately never calls `statsLookup()` for
  one, so a pass that happens to share a name with an earlier, different
  call's surviving pass can never show stale, misleadingly-real-looking
  numbers.
- **`RenderGraphResourceSnapshot`** — `name`/`isImported`/
  `firstUsePassIndex`/`lastUsePassIndex`, the last two copied verbatim from
  `RenderGraphCompiler.h`'s own `ResourceLifetime` (positions into the
  SURVIVING prefix of `passesInExecutionOrder`, never the culled tail — see
  the header's own comment for why this distinction matters).
- **`PassGpuStats`** was **relocated** here from where Phase 6 originally
  defined it inline inside `RenderGraph.h` — see "Deviation 2" below for why
  this was necessary, not optional.

### 2. `RenderGraph.h`/`.cpp` growth (Phase 6/7 files, extended)

- `RenderGraph` gained `LastSnapshot(ExecuteTimingMode mode) const noexcept`,
  returning a `const RenderGraphSnapshot&` for the regime's most recent
  `Execute()` call (a default-constructed, empty one before that regime has
  ever run).
- `ExecuteCompiledGraph()` now builds a `RenderGraphSnapshot` **after** its
  whole per-pass loop finishes (so `statsLookup` — a lambda wrapping
  `LastKnownStatsFor()` — sees this exact call's own freshly-recorded
  `DrawStats`/timing for every surviving pass) and stores it into
  `m_synchronousSnapshot`/`m_pipelinedSnapshot` depending on `timingMode` —
  the two regimes' snapshots are kept just as independent as their existing
  `RenderGraphNameSlotTable` instances already are.

### 3. `src/Editor/Panels/RenderGraphPanel.h`/`.cpp` (new)

A Unity-Profiler-window-style **"Render Graph"** panel, docked alongside
"Memory"/"Profiler" along the bottom (`DockLayout.cpp`). A small, STATEFUL
class (not a stateless free function), mirroring `ProfilerPanel`'s own
already-pre-approved exception (`AGENTS.md`, "Editor Module Structure") —
this panel's own **Pause** control needs to freeze a snapshot across frames,
exactly the same real reason `ProfilerPanel`/`BoneViewerWindow` are also
small stateful classes.

Layout, per the strategy document's own Step 3.2:

- Two independent sections, one per `ExecuteTimingMode` regime — **"Offscreen
  Regime (Game View + Scene View)"** and **"Pipelined Regime (Present)"** —
  since this campaign's own `RenderGraph::Execute()` genuinely runs twice per
  frame with two independent snapshots (see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s
  own V2 Revision Note 2); showing them as one merged list would misrepresent
  which passes actually ran together in the same submission.
- Each section's ordered PASS TABLE: name, draw-call count, triangle count,
  GPU time (or "N/A"/"Unsupported" — honestly reflecting that GPU timing is
  still `Absent` for every pass today, per Phase 6/7's own documented,
  deliberate scope decision — never a fabricated "0.00 ms"), and its declared
  reads/writes as comma-joined resource-name lists. A CULLED pass renders
  de-emphasized (`ImGui::TextDisabled`) with a "culled" placeholder in the
  draw/tri/timing columns and a hover tooltip explaining why ("no path from
  this pass's declared writes to this call's own final output(s) was
  found").
- Each section's RESOURCES table: name, "Imported"/"Transient", and a
  "`<first-use pass name>` -> `<last-use pass name>`" lifetime string —
  resolved back from `firstUsePassIndex`/`lastUsePassIndex` to the actual
  surviving pass's NAME (never a bare integer, which would be meaningless to
  a human reader), or "never used (fully culled)" for a resource nothing
  surviving ever touches.
- A disabled, tooltipped **"Export DOT"** button — the exact same
  deliberate stub-first pattern `ProfilerPanel`'s own "Export CSV" button
  already established, pointing at Phase 9 (see the strategy document's own
  Step 4).
- **No "Capture" control** — only "Pause". Building a `RenderGraphSnapshot`
  costs nothing beyond copying already-computed small strings/vectors once
  per `Execute()` call (no new Vulkan call, no new GPU cost at all), so there
  is no meaningful "disable snapshot capture" runtime toggle to offer —
  exactly as the strategy document's own Step 3.2 anticipated.

### 4. Wiring: `IEditorLayer::BuildUI()` gained a `const rg::RenderGraph&` parameter

`RenderGraphPanel::Build()` needs the SAME `gte::rg::RenderGraph` instance
`Application` already owns and drives twice a frame (`Application::m_renderGraph`)
— there was no existing seam for the Editor to read it through, so
`IEditorLayer::BuildUI(Game&, Renderer&)` grew a third parameter,
`const rg::RenderGraph& renderGraph`, threaded through:

- `EditorLayer.h` — new `namespace rg { class RenderGraph; }` forward
  declaration, updated `BuildUI()` signature/doc comment.
- `NullEditorLayer.cpp` — updated override, still a pure no-op.
- `ImGuiEditorLayer.cpp` — updated override; calls
  `m_renderGraphPanel.Build(m_ctx, renderGraph)` right alongside the existing
  `m_profilerPanel.Build(m_ctx)` call; added the `RenderGraphPanel
  m_renderGraphPanel` member, docked the same way `m_profilerPanel` already
  is.
- `Application.cpp` — the one production call site,
  `m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph)`.

This is a genuine, if small, interface change — but it costs nothing to
`Game`/`Renderer`/`RenderSystem`, and `NullEditorLayer`'s implementation
stays a trivial no-op, so a release build (`GTE_ENABLE_EDITOR=OFF`) is
completely unaffected.

### 5. `DockLayout.cpp` growth

`"Render Graph"` was added to `kAllPanelNames` (the one-shot default-layout
bookkeeping list) and to `BuildDefaultDockLayout()`'s own
`DockBuilderDockWindow()` calls, docked alongside `"Memory"`/`"Profiler"` —
unconditionally, matching `"Profiler"`'s own `GTE_ENABLE_PROJECT_PANEL`-
independent treatment, since this panel has no such dependency either.

### 6. `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` (new)

6 new Tier-1 tests, built through a real `RenderGraphBuilder`/
`RenderGraphCompiler::Compile()` (mirroring `RenderGraphCompilerTests.cpp`'s
own convention) with a hand-fabricated `statsLookup` stand-in (a small
`RecordingStatsLookup` functor that returns canned values AND records every
name it was actually called with):

- An empty graph produces an empty snapshot.
- Surviving passes appear in real execution order with correctly-resolved
  read/write resource names, and `statsLookup` is called exactly once per
  surviving pass, by name.
- A culled pass is appended after every surviving pass, tagged
  `isCulled == true`, with default (`DrawStats{}`, `Absent` timing) stats —
  and, critically, `statsLookup` is proven to have **never** been called for
  it at all (the direct regression test for this file's own
  "never resolve stats for a culled pass" rule).
- An imported texture resource and a transient texture resource both report
  the correct `isImported`/lifetime values (copied verbatim from
  `CompiledGraph::textureLifetimes`).
- A buffer resource is never reported as imported (no `ImportBuffer()`
  concept exists anywhere in this campaign).
- A default-constructed (empty) `statsLookup` never crashes and leaves every
  pass's stats at their default.

## Deviations from the strategy document's own sketch

**Deviation 1 — `BuildRenderGraphSnapshot()`'s third parameter is a
`std::function<PassGpuStats(const char*)>` stats-lookup callback, not a
`const RenderGraph&`.** The strategy document's own Step 3.1 code sketch
shows `BuildRenderGraphSnapshot(const CompiledGraph&, const CompiledGraphInput&,
const RenderGraph& graphForStats)`. Taking a live `RenderGraph&` here would
have made this function depend on a genuinely Tier-2 (GPU-touching) type,
directly contradicting the SAME paragraph's own explicit requirement one
sentence earlier: "`BuildRenderGraphSnapshot()` is a PURE, Tier-1-testable
reshaping function (hand-fabricate a `CompiledGraph`/`CompiledGraphInput`
and assert on the resulting snapshot's shape...)". It would also have
created a circular header dependency: `RenderGraph.h` needs
`RenderGraphSnapshot.h` (to store/serve a snapshot), and
`RenderGraphSnapshot.h` would then need `RenderGraph.h` back (for the
`RenderGraph&` parameter type). Taking a plain callback instead resolves
both problems at once — `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`
supplies a hand-written stand-in with zero live device involved, and
`RenderGraph.cpp`'s own production call site simply passes
`[this](const char* name) { return LastKnownStatsFor(name); }`, which is
exactly as capable as passing `*this` would have been, just decoupled.

**Deviation 2 — `PassGpuStats` was relocated to `RenderGraphSnapshot.h` from
where Phase 6 originally defined it inline in `RenderGraph.h`.** This is the
one small, genuinely shared type between `RenderGraph.h` (which needs it for
`LastKnownStatsFor()`'s return type, unchanged since Phase 6) and this
phase's new `RenderGraphSnapshot.h` (which needs it for
`RenderGraphPassSnapshot::stats` and `BuildRenderGraphSnapshot()`'s own
`statsLookup` callback type). Rather than leaving two independent, drifting
copies of the same struct, or introducing a circular include (see Deviation
1), `RenderGraphSnapshot.h` became its single home and `RenderGraph.h` now
just includes that header — a pure, mechanical relocation with zero
behavioral change (verified by the full pre-existing Phase 6/7 test suite
passing unchanged).

**Deviation 3 — `IEditorLayer::BuildUI()`'s signature change was not
anticipated by the strategy document at all.** The document's own Step 3.2
code sketch shows `Panels/RenderGraphPanel.h`'s `BuildXPanel(...)` taking an
`EditorContext&` "plus whatever else a panel needs" without naming a
`RenderGraph` parameter explicitly, and never discusses how `ImGuiEditorLayer`
(which owns no `RenderGraph` of its own — `Application` does) would obtain
one to pass through. This was worked out during implementation: the cleanest
fix, matching this codebase's own "pass what a panel needs explicitly, don't
reach for a global" convention (see `AGENTS.md`, "Editor Module Structure"),
was threading `const rg::RenderGraph&` through `IEditorLayer::BuildUI()`
itself, exactly parallel to how `Game&`/`Renderer&` already arrive there —
see "What shipped" item 4 above for the full, small diff this required.

## Verification performed

- Reconfigured with CMake (reusing the existing `build/` Ninja
  configuration) — no network access needed, everything was already
  fetched.
- Built `GreatTamanaEngineTests` from the existing incremental build —
  compiled with zero warnings/errors introduced by any new/changed file.
- Ran the **new** Render Graph tests in isolation
  (`--gtest_filter=*RenderGraph*`) — all **102** pass (the 96 pre-existing
  Phase 1–7 tests, unchanged, plus the 6 new Phase 8
  `RenderGraphSnapshotTests.cpp` tests described above).
- Ran the **entire** test suite — **623 tests total**, **622 passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest`, the same pre-existing
  machine-gated smoke test noted in every prior phase's report, unrelated to
  this change). **Zero regressions.**
- Built the real `GreatTamanaEngine.exe` (Editor build) — succeeded cleanly.
- Ran the Editor-build `GreatTamanaEngine.exe` under `run_app_background`,
  confirmed via `tasklist` that it stayed alive (~6 seconds, no crash)
  before stopping it with `stop_app_background` — exercising `BuildUI()`'s
  new `RenderGraph&` parameter and the new "Render Graph" panel's `Build()`
  call every frame against a live Vulkan device with validation layers
  enabled (this build's default).
- Configured, built, ran (~5 seconds, no crash via `tasklist`), and tore
  down a completely separate `-DGTE_ENABLE_EDITOR=OFF -DGTE_BUILD_TESTS=OFF`
  build (`build_noeditor_check/`, deleted at the end of this session — not
  committed) — confirming `NullEditorLayer::BuildUI()`'s updated signature
  still satisfies `IEditorLayer` correctly and the release build is
  completely unaffected by this phase's interface change.
- **Manual visual QA of the "Render Graph" panel's actual on-screen
  appearance was not possible in this environment** (no screenshot/window-
  capture tool was available for an arbitrary desktop application in this
  session's toolset, the same limitation Phase 7's own completion report
  already noted). What WAS verified is that the panel builds/runs without
  crashing and without any ImGui assertion firing for the whole run. A human
  should open the Editor and inspect the "Render Graph" tab (alongside
  "Memory"/"Profiler") before treating this panel's actual layout/readability
  as fully proven — per this phase's own Step 5, the concrete thing to check
  is whether the panel can easily answer "confirm Present's only declared
  read is whatever ImGui itself samples, and Present writes only the
  swapchain image" (see "Known limitations" below for why this specific
  question is currently harder to answer than it should be).

## Known limitations / deliberately deferred work

- **GPU timing is still honestly `Absent` for every pass, exactly as Phase
  6/7 left it.** This phase adds no new GPU-timing wiring at all — the
  "Render Graph" panel's "GPU Time" column faithfully shows "N/A" for every
  pass today, which is the CORRECT, honest behavior given the current state
  of the engine (see `RENDERGRAPH_PHASE7_COMPLETION_REPORT.md`'s own "Known
  limitations" — this remains the single highest-priority follow-up for the
  whole campaign).
- **Dear ImGui's own Game/Scene-view sampling is still invisible to this
  panel, exactly as flagged by `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s
  V2 Revision Note 4 and `RENDERGRAPH_PHASE7_COMPLETION_REPORT.md`'s own
  "Known limitations."** Neither `AddGameViewPass()` nor `AddSceneViewPass()`
  ever declares a `ReadTexture()` for the Game/Scene `RenderTexture` — ImGui
  samples it entirely outside the graph's own resource model (see
  `RenderPasses.cpp`'s `FinalizeRenderTextureForExternalSampling()`) — so the
  "Render Graph" panel's own "Reads" column for `"Present"` will show `"-"`
  for the Game/Scene textures even though ImGui visibly displays their
  contents that frame. This is not a bug in this phase's own code — it
  faithfully reflects what the graph actually knows — but it IS the exact
  gap Phase 7's own Step 3.7 (the Scene-view outline-highlight validation
  step, still not implemented) exists to eventually close, and it is worth
  flagging here again since this phase's own panel is the first place a
  human can directly SEE this gap by simply opening it and comparing
  "Present"'s declared reads against what's visibly on screen.
- **The "Export DOT" button remains a disabled stub**, exactly as this
  phase's own strategy document's Step 4 requires — no DOT/Graphviz export
  logic was written.
- **No interactivity was added** (no click-to-disable-a-pass, no drag-to-
  reorder) — this panel is purely READ-ONLY, matching "Memory"/"Profiler"'s
  own established convention, per the strategy document's own Step 4.
- **Manual visual QA of the panel's actual on-screen layout has not been
  performed** — see "Verification performed" above.

## What was deliberately NOT done (per the strategy document's own Step 4)

- No "Export DOT" implementation — shipped disabled/stubbed only.
- No interactive/editable panel behavior of any kind.
- No actual node-graph visual (boxes/connecting lines/force-directed
  layout) — the ordered-list + culled-section + resource-table presentation
  is the whole of this phase's UI, exactly as scoped.
- No new per-pass GPU/CPU measurement was added — this phase is purely a new
  VIEW over data Phases 1–7 already computed (`RenderGraph::LastKnownStatsFor()`/
  `CompiledGraph`'s own resource lifetimes); zero new instrumentation.

## Handoff notes for whoever picks up the next session

- **This closes out the Render Graph campaign's Phases 1–8 MVP scope.** Per
  this phase's own strategy document ("Step 5: Their Role"), the next step
  is a short campaign-level `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`
  tying all eight phases together — written alongside this report in the
  same session (see that file).
- **Top priority, unchanged from Phase 7: wire real GPU timing into the
  render-graph passes.** See `RENDERGRAPH_PHASE7_COMPLETION_REPORT.md`'s own
  "Handoff notes" for the detailed plan (`RenderGraphNameSlotTable`'s
  already-exercised slot assignments need a real, generalized `VkQueryPool`
  behind them, replacing `GpuTimingService`'s fixed 3-slot one). This
  phase's own "Render Graph" panel is now the most direct way to SEE this
  land once it does — its "GPU Time" column will go from "N/A" to a real
  millisecond value with no further Editor-side work needed.
- **Second priority, unchanged from Phase 7: Step 3.7's cross-pass-read
  validation** (the Scene-view outline-highlight post-process) — see
  `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision Note 4. This
  phase's own "Known limitations" section above gives a concrete, now-
  visible-in-the-Editor way to confirm this gap still exists — re-check the
  "Render Graph" panel's "Present" pass row once that follow-up lands; its
  "Reads" column should then show a real texture name instead of "-".
- **A future "Export DOT" implementation** (Phase 9 backlog) should build
  directly on `RenderGraphSnapshot`/`BuildRenderGraphSnapshot()` — the data
  model already carries everything a DOT/Graphviz export needs (pass names,
  culled status, resource names per read/write edge); no new data-gathering
  should be required, only a serialization step.
- Do not reintroduce a `debugName`/name-like field onto `TextureDesc`/
  `BufferDesc` — see Phase 1 v2's own standing rule, still fully in force,
  and unaffected by anything in this phase (`RenderGraphSnapshot`'s own
  `name` fields are populated from `RenderGraphBuilder`'s separate name
  tables, exactly like every other consumer of those tables).
