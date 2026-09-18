# PHASE1 COMPLETION REPORT — Render Pass Core Abstraction

_Child of `PHASE0_MASTER_STRATEGY.md`. Campaign: `render-pass-1`, branch
`feature/render-pass-impl`._

## Summary

Implemented PHASE1 exactly per `PHASE1_RENDER_PASS_CORE_ABSTRACTION.md`: a new
`PassKind` enum (REPLACES the old plain `bool isComputePass`), a new,
orthogonal `RenderPassCategory` enum (purely descriptive Frame-Debugger
grouping metadata), and one new, official chokepoint,
`RenderGraphBuilder::AddRenderPass()` (two overloads), built directly on top
of the pre-existing `AddPass()`/`AddComputePass()` methods — no polymorphic
pass-object hierarchy, per `PHASE0`'s Locked Design Decision #1. Nothing in
this phase changes any pass's real runtime behavior: `AddRenderPass()` is not
called by any production code yet (that begins in PHASE2/PHASE3), exactly as
this phase's own "What We Will NOT Do" requires.

There was no `PHASE0_COMPLETION_REPORT.md` to read (PHASE0 is the campaign's
orchestrator/strategy document, not a phase with its own completion report),
so this phase started directly from `PHASE0_MASTER_STRATEGY.md` and its own
`PHASE1_RENDER_PASS_CORE_ABSTRACTION.md`.

## What Changed

### 1. `src/Renderer/RenderGraph/RenderGraphTypes.h`

- Added `enum class PassKind : std::uint8_t { Graphics, Compute };` plus
  `const char* ToString(PassKind) noexcept;`, placed next to `ViewScope`.
- Added `enum class RenderPassCategory : std::uint8_t { General,
  AtmosphereLut, GpuSkinning, Debug };` plus its own `ToString()`, placed
  immediately after `PassKind`.
- `PassRecord::isComputePass` (bool) → **replaced in place** by
  `PassRecord::kind` (`PassKind`, defaults to `PassKind::Graphics`).
- Added `PassRecord::category` (`RenderPassCategory`, defaults to `General`),
  appended at the very END of the struct (never inserted in the middle, per
  this codebase's own standing convention).

### 2. `src/Renderer/RenderGraph/RenderGraphTypes.cpp`

- Added `ToString(PassKind)` and `ToString(RenderPassCategory)`, both
  exhaustive switches with no `default:` case, mirroring
  `ToString(ResourceAccess)`'s existing convention exactly.

### 3. `src/Renderer/RenderGraph/RenderGraphBuilder.h`

- `AddComputePass()`'s 3-argument overload now stamps
  `m_passes.back().kind = PassKind::Compute;` instead of the old
  `isComputePass = true;`.
- Added the new `AddRenderPass()` chokepoint, verbatim per the phase
  document: a 5-argument form (`name, kind, viewScope, category, setup,
  execute`) that dispatches to `AddComputePass()`/`AddPass()` depending on
  `kind` and then stamps `category`, plus a 4-argument convenience overload
  defaulting `viewScope` to `Shared` and `category` to `General`.
  `AddPass()`/`AddComputePass()` themselves are untouched/still public — no
  deprecation.

### 4. `src/Renderer/RenderGraph/RenderGraphSnapshot.h` / `.cpp`

- `RenderGraphPassSnapshot::isComputePass` (bool) → renamed to
  `RenderGraphPassSnapshot::kind` (`PassKind`).
- Added `RenderGraphPassSnapshot::category` (`RenderPassCategory`).
- `BuildPassSnapshot()` now copies `pass.kind`/`pass.category` straight
  through for BOTH a surviving and a culled pass (unchanged copy-through
  discipline the old `isComputePass` line already had).

### 5. `src/Editor/FrameDebuggerData.cpp`

The only production reader of the old `isComputePass` field. Its three
`if (!pass.isComputePass || ...)` guards were mechanically updated to
`if (pass.kind != rg::PassKind::Compute || ...)` — behaviorally identical,
zero tree-shape change. A couple of nearby doc comments that named the old
field literally were also updated to reference `kind`/`PassKind::Compute` so
they stay accurate (this file's own real PHASE4 rework is still fully
deferred to that later phase — only the mechanical rename needed here to keep
compiling and to keep the comments truthful).

### 6. Comment-only touch-ups (no behavior change)

A few more files had doc comments that literally named the old
`PassRecord::isComputePass`/`RenderGraphPassSnapshot::isComputePass` fields as
a *fact about the current field* (not narrating campaign history) — these
were updated for accuracy, since they are trivial, in the same files/areas
already being touched:
- `src/Editor/FrameDebuggerData.h`
- `src/Editor/ImGuiEditorLayer.cpp`
- `src/Editor/Panels/FrameDebuggerPanel.h`

Historical `task_manager/*` campaign documents (`frame-debugger-5`,
`frame-debugger-6`, `frame-debugger-7`, this campaign's own earlier phase
files), and `README.md`/`TODO.md`/`docs/conventions/frame-debugger.md`'s own
changelog-style narrative of *past, already-shipped* work, were deliberately
**left untouched** — they are historical record of what was true at the time
each was written (this codebase's own established convention; the same
reason old completion reports are never retroactively edited), and
`docs/conventions/frame-debugger.md`/`AGENTS.md` updates for the *new* tree
shape are explicitly PHASE6's job per `PHASE0_MASTER_STRATEGY.md`'s own phase
index, not this phase's.

### 7. Tests

- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`: renamed
  `DefaultConstructedPassRecordIsEmptyAndNotCulled`'s `isComputePass`
  assertion to `EXPECT_EQ(record.kind, PassKind::Graphics)`; added
  `DefaultConstructedPassRecordHasGeneralCategory`, and new
  `RenderGraphPassKindTest`/`RenderGraphRenderPassCategoryTest` suites
  covering `ToString()` exhaustively (every enumerator, non-null,
  non-empty, distinct name per value) for both new enums.
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`: renamed
  `AddPassRecordsIsComputePassFalse` → `AddPassRecordsGraphicsKind`,
  `AddComputePassRecordsIsComputePassTrue` → `AddComputePassRecordsComputeKind`,
  `AddComputePassFourArgumentOverloadStampsViewScopeAndIsComputePass` →
  `...AndComputeKind` — same assertions, now against `.kind`/`PassKind`.
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`: renamed
  `IsComputePassIsCopiedThroughForSurvivingGraphicsAndComputePasses` →
  `KindIsCopiedThroughForSurvivingGraphicsAndComputePasses` and
  `CulledComputePassStillReportsIsComputePassTrueAndCorrectWriteKind` →
  `...ReportsComputeKindAndCorrectWriteKind`; the mixed-read/write-kind test's
  lone `EXPECT_TRUE(pass.isComputePass)` became
  `EXPECT_EQ(pass.kind, PassKind::Compute)`.
- `tests/Editor/FrameDebuggerDataTests.cpp` /
  `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`: the two hand-built
  `RenderGraphPassSnapshot` fixtures that used to set `.isComputePass = true`
  now set `.kind = rg::PassKind::Compute` — no assertions changed, since
  `BuildRealFrameDebuggerSnapshot()`'s own behavior is unchanged by this
  phase (PHASE4 is where its logic itself gets reworked).
- **New file**, `tests/Renderer/RenderGraph/RenderPassTests.cpp` (registered
  in `tests/CMakeLists.txt`, next to `RenderGraphBuilderTests.cpp`): exercises
  `AddRenderPass()` itself — both the `PassKind::Graphics` and
  `PassKind::Compute` branches of the 3-argument-plus-kind overload correctly
  delegate to `AddPass()`/`AddComputePass()` and still run `setup`
  synchronously; the 4-argument (`viewScope`, `category`) overload correctly
  stamps both for each `PassKind`; and `execute` is captured but never
  invoked by `AddRenderPass()`/`Finish()` (mirroring `AddPass()`'s own
  existing guarantee).

## Verification

- `search_in_dir` for the literal string `isComputePass` across `src/` and
  `tests/` now returns **zero** hits that are real field reads/writes — the
  only remaining hits are rename-history doc comments explicitly saying
  "RENAMED from the original plain `bool isComputePass`" (in
  `RenderGraphTypes.h`, `RenderGraphBuilder.h`, `RenderGraphSnapshot.h`/`.cpp`,
  and the three touched test files), which is expected and, per the phase
  document's own Definition of Done, acceptable as this phase's own
  doc-comment history. Historical `task_manager/*` docs from OTHER campaigns
  and README/TODO/docs-conventions changelog prose still say `isComputePass`
  too, by design (see "Comment-only touch-ups" above) — narrating what was
  literally true when those were written.
- Incremental compile: `cmake --build build --target gte_core` — clean
  build, zero errors/warnings related to this change.
- Incremental compile: `cmake --build build --target GreatTamanaEngineTests`
  — clean build.
- Incremental compile: `cmake --build build --target GreatTamanaEngine` (the
  real executable, since `Application.cpp`/`RenderPasses.cpp` both transitively
  include the touched headers) — clean build.
- Ran the full, exact set of touched/added test suites directly via
  `GreatTamanaEngineTests.exe --gtest_filter=...` (NOT the full `ctest` suite,
  per this campaign's own "no full regression test until PHASE7" rule):
  `RenderPassTest.*`, `RenderGraphPassKindTest.*`,
  `RenderGraphRenderPassCategoryTest.*`, `RenderGraphPassRecordTest.*`,
  `RenderGraphBuilderTest.*`, `RenderGraphSnapshotTest.*`,
  `FrameDebuggerSnapshotBuilderTest.*`, `FrameDebuggerDataTest.*` — **110/110
  passed**.

## Deviations From The Phase Document

None in substance. One minor scope judgment call: the phase document's own
Definition of Done only explicitly calls out updating readers in
`RenderGraphBuilder.h`, `RenderGraphSnapshot.h`/`.cpp`, `FrameDebuggerData.cpp`
"and their respective test files" — I additionally fixed a handful of
doc-comment-only mentions of the old field name in `FrameDebuggerData.h`,
`ImGuiEditorLayer.cpp`, and `Panels/FrameDebuggerPanel.h` (files not on that
explicit list) purely because they asserted the OLD field name as a present-
tense fact and were trivial one-line fixes while already grepping the area;
no logic in those three files changed.

## Definition of Done — Checklist

- [x] `PassKind`/`RenderPassCategory` exist in `RenderGraphTypes.h`, both with
      a `ToString()` helper.
- [x] `PassRecord::isComputePass` no longer exists — replaced by
      `PassRecord::kind` (`PassKind`).
- [x] `PassRecord::category` (`RenderPassCategory`, default `General`) exists.
- [x] `RenderGraphBuilder::AddRenderPass()` (both overloads) exists, compiles,
      and is exercised by new, passing Tier-1 tests.
- [x] `RenderGraphPassSnapshot` carries the renamed `kind` field and a new
      `category` field, both copied through unchanged for culled passes.
- [x] Every existing call site that read the old field is updated and still
      compiles/passes its own tests unchanged.
- [x] Incremental compile of `gte_core` and `GreatTamanaEngineTests` succeeds.
- [x] Nothing in this phase changes runtime rendering behavior — no real pass
      calls `AddRenderPass()` yet.

## Handoff To PHASE2

`AddRenderPass()`/`PassKind`/`RenderPassCategory` are ready to use.
PHASE2 (`PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`) can now
split `"GameView"` into real `"RenderOpaque"` + `"DrawSkyBackground"` passes
declared through this new chokepoint, and add the `"RenderTransparent"`
no-op scaffold pass, per `PHASE0`'s Locked Design Decision #2/#6.
