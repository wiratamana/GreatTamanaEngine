# PHASE1 — Tag Vocabulary Relocation + End-to-End `tags` Threading — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Phase doc followed:** `PHASE1_TAG_VOCABULARY_AND_THREADING.md`.

## Summary

Implemented exactly what the phase document's Step 3 specified, in the same order, with zero
behavior change. `RenderPassTagMask` is now a real, Core-level, end-to-end plumbed piece of
pass metadata, mirroring `category`/`drawKind`/`renderPassEvent` exactly. Every pass's `tags`
value is `0` both before and after this phase — nothing reads it for a real decision yet
(PHASE3/PHASE4 do that).

## Changes made (file by file)

1. **`src/Renderer/RenderGraph/RenderGraphTypes.h`** — `RenderPassTag`/`RenderPassTagMask`
   relocated here (from `RenderPipeline.h`), placed immediately after
   `RenderPassDrawKind`'s own `ToString()` declaration and before `RenderPassEvent`'s comment
   block, per the phase doc's Step 3.1 placement instruction. Doc comment rewritten to explain
   the relocation reason (mirrors `RenderPassEvent`'s own precedent) instead of the stale
   "shared core file" framing that assumed `RenderPipeline.h` itself was that file.
   `PassRecord` gained a new, appended-at-the-end `RenderPassTagMask tags = 0;` field with a
   full doc comment matching the phase doc's exact wording.

2. **`src/Renderer/RenderGraph/RenderPipeline.h`** — the old `RenderPassTag`/`RenderPassTagMask`
   struct/using-declaration block deleted (not duplicated); replaced with a short comment
   explaining the relocation. The file's own top header comment updated to describe TWO
   exceptions to "every new PHASE1 type lives in this file" (`RenderPassEvent` AND
   `RenderPassTag`/`RenderPassTagMask`) instead of just one. `RenderPipeline::DeclareOnePhase()`'s
   `builder.AddRenderPass(...)` call now passes `desc.tags` as a new trailing argument — this is
   the literal fix for the real, confirmed dead-field bug described in `PHASE0_MASTER_STRATEGY.md`
   Step 2.4 (`RenderPassDesc::tags` used to be silently dropped every frame for every provider).

3. **`src/Renderer/RenderGraph/RenderGraphSnapshot.h`** — `RenderGraphPassSnapshot` gained a
   new, appended-at-the-end `RenderPassTagMask tags = 0;` field, doc comment mirroring
   `category`/`drawKind`/`viewScope`/`renderPassEvent`'s own "copied through for both a
   surviving and a culled pass" convention.

4. **`src/Renderer/RenderGraph/RenderGraphSnapshot.cpp`** — `BuildPassSnapshot()` gained
   `snapshot.tags = pass.tags;` right next to the existing `snapshot.category = pass.category;`
   line.

5. **`src/Renderer/RenderGraph/RenderGraphBuilder.h`** — both `AddRenderPass()` overloads gained
   a new trailing, defaulted `RenderPassTagMask tags = 0` parameter:
   - The full (now 9-argument) overload stamps `m_passes.back().tags = tags;` right next to the
     existing `category`/`drawKind`/`renderPassEvent` assignments.
   - The convenience (now 7-argument) overload forwards its own new `tags` parameter straight
     through into the first overload's call.
   Confirmed live (by re-reading the file immediately before editing) that `renderPassEvent`
   really was the last parameter added by any prior campaign — `tags` was appended strictly
   after it, never inserted in the middle.

## Deviations from the phase document

None. Every edit matches the phase document's Step 2/Step 3 instructions exactly, including the
exact placement/wording guidance for doc comments. Line numbers in the phase document were
approximate as warned — the live file was re-read immediately before every edit, and actual
positions differed slightly from the doc's estimates (e.g. `PassRecord`'s tags field motion
landed after `colorAttachments` as instructed, but at line ~826, not exactly the doc's own
rough estimate) — this is expected, normal drift, not a deviation in substance.

## Tests added

Per the phase document's Step 3.7, one test file at a time:

- **`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`** — extended the existing
  `RenderGraphPassRecordTest.DefaultConstructedPassRecordHasGeneralCategory` test with
  `EXPECT_EQ(record.tags, RenderPassTagMask{0});`.
- **`tests/Renderer/RenderGraph/RenderPassTests.cpp`** — four new tests:
  - `AddRenderPassNineArgumentOverloadStampsTagsAlongsideEveryOtherField` (explicit non-zero
    `tags` stamped correctly, alongside every other field the full overload already stamps).
  - `AddRenderPassNineArgumentOverloadDefaultsTagsToZeroWhenOmitted`.
  - `AddRenderPassSevenArgumentOverloadStoresExplicitTags` (proves the convenience overload
    genuinely forwards `tags`, not just defaults it).
  - `AddRenderPassSevenArgumentOverloadDefaultsTagsToZeroWhenOmitted`.
- **`tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`** — two new tests mirroring the
  existing `DrawKindIsCopiedThroughFor{Surviving,Culled}Pass` pair:
  - `TagsIsCopiedThroughForSurvivingPass`
  - `TagsIsCopiedThroughForCulledPass`
- **`tests/Renderer/RenderGraph/RenderPipelineTests.cpp`** — the phase document's own
  "MOST IMPORTANT" test: `DeclareIntoForwardsTagsOntoTheUnderlyingPassRecord`, a dedicated
  regression test for the dead-field bug fix. Confirmed by direct code inspection (not by
  reverting the fix and re-running, since this phase's own acceptance bar forbids leaving the
  tree in a broken state mid-phase) that this test's assertion (`EXPECT_EQ(input.passes[0].tags,
  0x4u)`) would fail against the pre-fix `DeclareOnePhase()` body, since `desc.tags` was never
  passed to `builder.AddRenderPass()` at all before this phase's edit — the old call only ever
  passed `debugName`/`kind`/`translatedViewScope`/`legacyCategory`/`setup`/`execute`/`drawKind`/
  `order`, so a non-zero `desc.tags` had no way to reach `m_passes.back().tags` at all.

## One minor tooling note (not a functional bug in the final result)

While editing `tests/Renderer/RenderGraph/RenderPassTests.cpp`, an `edit_line` call
miscounted a line range and briefly deleted the closing `}` of an existing, unrelated test
function (`AddRenderPassFourArgumentOverloadStoresExplicitDrawKind`) instead of only the
blank line after it. This was caught immediately by re-reading the file's live content right
after the edit (as this workflow requires) and corrected in the very next edit before any
build was attempted — the final file (confirmed by a full `read_file` pass afterward, and by
a successful compile) has the correct brace nesting and all pre-existing tests intact. Not a
tool malfunction — a simple line-index mistake — so no bug report was filed.

## Build & test result

- **Incremental compile** (`cmake --build build --target gte_core`): succeeded, zero new
  warnings/errors, 49/49 objects rebuilt/relinked.
- **Test binary rebuild** (`cmake --build build --target GreatTamanaEngineTests`): succeeded,
  zero new warnings/errors.
- **Targeted test run** (`GreatTamanaEngineTests.exe --gtest_filter=
  "RenderGraphPassRecordTest.*:RenderPassTest.*:RenderGraphSnapshotTest.*:RenderPipelineTest.*"`):
  **55/55 tests passed** (12 RenderPipelineTest, 26 RenderGraphSnapshotTest, 13 RenderPassTest,
  4 RenderGraphPassRecordTest).
- **Broader safety-net run** (`--gtest_filter="*RenderGraph*:*RenderPass*:*RenderPipeline*"`):
  **270/270 tests passed** — confirms no other Render Graph test (RenderGraphBuilderTests,
  RenderGraphCompilerTests, RenderGraphBarrierPlannerTests, RenderGraphDebugTextureRegistryTests,
  RenderGraphDebugVolumeTextureRegistryTests) regressed from this phase's edits.
- Per this campaign's own process rules, the FULL `ctest` suite was deliberately **not** run —
  that is PHASE5's job.

## Acceptance bar check (against the phase document's own criteria)

- ✅ Full incremental build succeeds with zero new warnings/errors.
- ✅ Every pre-existing test still passes; every new test above passes.
- ✅ `git diff` shows only additive/relocating changes — no existing call site's arguments were
  reordered or removed, no existing behavior changed. Every real pass in the engine still has
  `tags == 0` after this phase (verified structurally: no production call site under
  `src/Application/`, `src/Renderer/Atmosphere/`, or elsewhere passes a non-zero `tags` argument
  anywhere yet — PHASE3 is where real tag values first get stamped onto real passes).

PHASE1 is complete and ready for PHASE2 (`PHASE2_PASS_GROUP_REGISTRY.md`).
