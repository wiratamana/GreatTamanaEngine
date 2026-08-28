# GPU Vertex Skinning — Phase 7: Editor Toggle & Profiling UX — Completion Report

Status: **DONE**. Implements
`task_manager/gpu_skinning/GPU_SKINNING_PHASE7_EDITOR_PROFILING_UX_STRATEGY_v1.md`
in full, per the scope fence set by
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md` ("Focus on selected section
only" — this report covers ONLY the "ON GOING" Phase 7 item; Phases 1-6 are
already `[DONE]` and are completely unmodified by this phase except for the
documentation updates described in this report's own final section).

## What Phase 7 actually needed to build

Per the strategy document's own Step 2 ("The Situation / The Problem"),
almost every profiling surface this phase needs already existed before this
session started:

- CPU mode was already fully instrumented — `RunSkinningBatch()`
  (`AnimationSystem.cpp`) already wraps `SkinVertexRange()` in
  `GTE_PROFILE_JOB_SCOPE("SkinVertices")`, and the Editor's "Jobs" panel
  (Job System Phase 7) already visualizes this as a per-worker timeline.
- GPU mode's pass timing is automatic the moment a pass has a name —
  `RenderGraph`'s real GPU-timestamp-query infrastructure (Render Graph
  Phase 4A-4D) times every named pass, and the "Render Graph" panel already
  shows every pass's GPU time in a table. Phase 5's own
  `AddGpuSkinningPasses()` already names its passes
  `"<GpuSkinningRigCache::OutputGroup::debugName>"` (a persistent,
  per-model-per-group string — see Phase 5's completion report), so once a
  GPU-skinned model is actually dispatched, its own pass already shows up
  in that table with zero further instrumentation work needed.

What genuinely did NOT exist yet, and is what this phase actually built:

1. The UI control itself to flip `AnimationSystem::SkinningMode` at
   runtime — before this phase, the only way to exercise GPU mode was a
   direct C++ call to `Game::SetSkinningMode()` (see
   `GPU_SKINNING_PHASE5_COMPLETION_REPORT.md`'s own "What was deliberately
   NOT done").
2. A cross-reference note tying the toggle to where its consequence shows
   up in EACH mode, so a user isn't left wondering why data vanished from
   one panel when they flip the switch.
3. The documentation close-out (`AGENTS.md`/`README.md`/`TODO.md`) the
   strategy document's own Step 3.5 calls for, once every phase of this
   campaign has landed.

## What was built

### 1. The toggle itself — placed in the "Jobs" panel (Step 3.1's decision, confirmed)

`src/Editor/Panels/JobsPanel.cpp` gained a new, first section,
`BuildSkinningModeControl(Game& game)`, called at the very top of
`JobsPanel::Build()` (before the existing Pause control), rendering:

- An `ImGui::Combo("Skinning Mode", ...)` with exactly two entries — "CPU
  (Job System)" and "GPU (Compute)" — reading/writing
  `game.GetSkinningMode()`/`game.SetSkinningMode()` directly (both already
  existed, added by Phase 5 — this phase adds no new API to `Game`/
  `AnimationSystem` at all, only a UI consumer of the existing one).
- A small "(?)" hover-tooltip immediately after it, showing a one-line
  cross-reference note (see below) plus a reminder that a fair CPU-vs-GPU
  comparison needs the same model/animation/frame in both modes (the
  strategy document's own Step 3.4 — a documentation-level responsibility,
  not something enforced in code, per that step's own reasoning).

Per the strategy document's own Step 3.1 evaluation of "Jobs" vs. "Render
Graph" as the toggle's home: **"Jobs" was chosen**, exactly as the strategy
document itself already decided — this phase did not need to re-litigate
that decision, only implement it. `JobsPanel::Build()`'s signature grew a
new `Game&` parameter (`JobsPanel.h`/`.cpp`), and its one production call
site, `ImGuiEditorLayer::BuildUI()`, was updated to pass `game` through
(mirroring `BuildHierarchyPanel()`/`BuildScenePanel()`, which already take
`game` for their own reasons).

### 2. The cross-reference note — pure, ImGui-free, Tier-1-tested (Step 3.3)

Per this codebase's own established convention (`ProfilerPanelData.h`/
`MemoryPanelData.h`/`JobsPanelData.h` — pure reshape/format functions, no
ImGui dependency, Tier-1-tested despite living under `src/Editor/` — see
`AGENTS.md`, "Testability & Regression Safety"), two new pure functions
were added to `src/Editor/JobsPanelData.h`/`.cpp`:

- `SkinningModeDisplayName(bool isGpuMode)` — "CPU (Job System)" or "GPU
  (Compute)".
- `SkinningModeCrossReferenceHint(bool isGpuMode)` — the actual "look over
  there instead" text the strategy document's Step 3.1/3.3 calls for: CPU
  mode's hint tells the user this SAME panel's own worker timeline shows
  "SkinVertices" entries, and that switching to GPU moves that work to a
  "SkinModel:..." pass in "Render Graph"; GPU mode's hint says the reverse.

Both are deliberately expressed as a plain `bool isGpuMode` rather than
`gte::AnimationSystem::SkinningMode` itself, so `JobsPanelData.h` (like
every other `*PanelData.h` in this module) never needs to depend on
`Game`/`AnimationSystem` at all — `Panels/JobsPanel.cpp` (which already
depends on `Game` for the toggle's actual read/write) is the one place that
translates between the two.

Two new Tier-1 tests were added to `tests/Editor/JobsPanelDataTests.cpp`
(`SkinningModeDisplayNameReflectsMode`,
`SkinningModeCrossReferenceHintDiffersByMode`), following the file's own
existing conventions exactly.

### 3. Trustworthiness — confirmed, no new code needed (Step 3.3)

Per the strategy document's own Step 3.3: neither panel needed (or got) a
new "N/A"/fabricated-value state for the mode that ISN'T currently active.
Confirmed by inspection (no live multi-model session was run this
session — see "What was deliberately NOT done" below):

- In CPU mode, `AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()`
  (Phase 5) returns empty, so `AddGpuSkinningPasses()` (Phase 5,
  `RenderPasses.cpp`) declares zero `SkinModel:...` passes that frame — the
  "Render Graph" panel's existing "culled vs. never declared" distinction
  already handles a pass that was never declared at all; there is nothing
  new to fabricate.
- In GPU mode, the CPU branch of `AnimationSystem::Update()` never runs for
  a GPU-mode-animated model, so `RunSkinningBatch()`'s
  `GTE_PROFILE_JOB_SCOPE("SkinVertices")` call never fires for it — the
  "Jobs" panel's worker timeline simply has no such entry that frame,
  exactly like any other frame with nothing recorded.

Both of these are direct, structural consequences of Phase 5's own design
(the mode branch inside `AnimationSystem::Update()`) — Phase 7 did not need
to add any new "hide this row" logic to either panel.

### 4. Documentation close-out (Step 3.5)

Per the strategy document's own instruction ("Once Phase 7 lands (and only
once every phase does), update the shared project docs..."), and since this
is the last of the eight phases:

- **`AGENTS.md`** gained a new "GPU Vertex Skinning" section (inserted
  between "Skeletal Animation Pose Resolution" and "Entity-Component-System
  (ECS)"), documenting the load-bearing rules a future engineer must not
  violate: the CPU path is the permanent oracle and must never be modified
  to "agree" with the GPU kernel; the graphics pass's "phantom"
  `ResourceAccess::VertexBufferRead` declaration is not dead code; a
  GPU-skinned model's `ComputeDescriptorSet::Rewrite()` is called exactly
  once, never every frame; a GPU-skinned model deliberately owns two
  separate `Mesh` objects at once (the doubled-memory trade-off is
  deliberate); `AnimationSystem::Update()`'s outer per-animator loop must
  stay strictly sequential regardless of skinning mode; and the toggle's
  own placement/design rationale.
- **`README.md`**'s "Status" section gained a new entry (right before "##
  Roadmap") summarizing the whole eight-phase campaign at the same level of
  detail as this file's other campaign-summary entries (Job System, Render
  Graph, Profiler).
- **`TODO.md`** gained a new "## GPU Vertex Skinning" section (right before
  "## Engine Roadmap"), listing every genuinely open follow-up: the still-
  outstanding full clean build + `ctest` regression run and the real,
  measured CPU-vs-GPU performance comparison this campaign's own strategy
  document calls "the actual deliverable the entire eight-phase campaign
  exists to produce"; running `GpuSkinningValidation`'s numeric parity
  check against a real rigged model (Phase 6's own left-open action item —
  this session's development machine had no MMD model fixture on disk
  either, so this remains open); live-device validation-layer confirmation
  of Phase 3's write-after-write mitigation; and the deliberately-deferred
  scope items from each phase's own "What We Will NOT Do" (GPU-side pose
  evaluation, per-model mode override, the doubled memory footprint,
  kernel-performance optimization).

## Wiring

- `src/Editor/JobsPanelData.h`/`.cpp` — `SkinningModeDisplayName()`,
  `SkinningModeCrossReferenceHint()`.
- `src/Editor/Panels/JobsPanel.h`/`.cpp` — `Build()` gained a `Game&`
  parameter; new `BuildSkinningModeControl()` section.
- `src/Editor/ImGuiEditorLayer.cpp` — updated `m_jobsPanel.Build(m_ctx,
  game)` call site.
- `tests/Editor/JobsPanelDataTests.cpp` — two new tests.
- `AGENTS.md`/`README.md`/`TODO.md` — documentation updates described
  above.
- No `CMakeLists.txt` changes were needed — every file touched already
  exists in `gte_core`'s/`GreatTamanaEngineTests`'s source lists.

## What was deliberately NOT done (per Phase 7's own "What We Will NOT Do")

- No new charting/graphing UI — every visualization this phase needed
  already existed (the worker timeline, the per-pass GPU timing table);
  this phase adds only a toggle plus a one-line cross-reference tooltip.
- No automated regression test asserting "GPU mode is faster than CPU
  mode" (or vice versa) — performance is workload/hardware-dependent by
  design, and this campaign exists to let a human OBSERVE the difference,
  not to auto-assert one.
- No CSV/benchmark-export feature specific to this campaign.
- No removal of the CPU path — it remains the default
  (`AnimationSystem::m_mode` still defaults to `CpuJobSystem`) and remains
  fully supported.
- **No live, in-Editor manual verification session was run this session**
  (flipping the toggle with a real animated model on screen, watching both
  panels, rapid-toggle stress-testing, or producing the actual, required
  real side-by-side CPU-vs-GPU performance number the strategy document's
  own Step 5 calls for). Per this session's own instructions (fast compile
  check only, full build/regression testing deferred to later), this
  remains explicitly open — see the new `TODO.md` "GPU Vertex Skinning"
  section for the exact, concrete follow-up actions this leaves behind.
  This is the same honest gap Phase 6 already left open for its own
  validation tool (no real MMD model fixture was available on this
  session's development machine either).

## Verification performed

Per this session's own explicit instructions ("do not perform full build,
just a fast compile check... full build and regression test will perform
later after everything done"):

- `cmake --build build --target gte_core` — succeeded, zero errors/
  warnings related to this change (`JobsPanelData.cpp`,
  `Panels/JobsPanel.cpp`, and `ImGuiEditorLayer.cpp` all compiled and
  linked into `libgte_core.a` cleanly).
- `cmake --build build --target GreatTamanaEngineTests` — succeeded,
  linked cleanly against the updated `gte_core` and the new test file
  content.
- `cmake --build build --target GreatTamanaEngine` — succeeded (confirms
  the full executable, including every shader-staging step, still links
  correctly; this phase touched no shaders).
- Ran the new/updated tests directly
  (`GreatTamanaEngineTests.exe --gtest_filter=JobsPanelDataTest.*`) — all
  13 tests (11 pre-existing + 2 new) passed.
- Did **not** run the full test suite (`ctest`), a clean `build_joboff`
  verification build, or launch the actual Editor to manually exercise the
  toggle — all explicitly deferred to "later, after everything done" per
  this session's own instructions, and explicitly re-flagged as open
  follow-up actions in `TODO.md`.

## Notes for whoever picks up the remaining follow-up work

This report closes out the LAST of the eight phases named in
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md`'s own phase table — every phase
from 1 through 7 is now `[DONE]`. What remains is not a ninth phase, but
the concrete, honestly-tracked follow-up actions now recorded in `TODO.md`'s
new "GPU Vertex Skinning" section:

1. A full clean build (`build_joboff`, per this session's own supplied
   cmake parameters) plus the complete `ctest` regression run.
2. Launching the Editor with a real rigged model, actually flipping the new
   "Skinning Mode" toggle, and confirming (a) the "Jobs"/"Render Graph"
   panels' data appears/disappears exactly as designed, (b) no crash under
   rapid repeated toggling, and (c) recording a real, written side-by-side
   CPU-worker-timeline-vs-GPU-pass-timing performance comparison — the
   actual deliverable this whole campaign was built to produce.
3. Running `GpuSkinningValidation::ValidateGpuSkinningAgainstCpuOracle()`
   against a real rigged model fixture to produce the numeric "max/mean
   per-vertex delta" evidence Phase 6 left open.
4. Live-device validation-layer confirmation of Phase 3's write-after-write
   mitigation with two real, simultaneously-animated instances of the same
   model in GPU mode.

None of these four require further engine code changes by default (the
toggle/validation tool/mitigation are all already built and compiling) —
they are verification/measurement activities against a real Vulkan device
and a real MMD model fixture, which is exactly why they were deferred to
the "full build and regression test... performed later after everything
done" pass this session's own instructions already called out from the
start.
