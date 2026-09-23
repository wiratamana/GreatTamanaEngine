# PHASE1 — Tag Vocabulary Relocation + End-to-End `tags` Threading

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first).

---

## Step 1 — The Goal

Make `RenderPassTagMask` a real, Core-level, end-to-end plumbed piece of pass metadata —
exactly as load-bearing as `category`/`drawKind`/`renderPassEvent` already are — with **zero
behavior change** (every pass's `tags` value is `0` before and after this phase; nothing
reads it for a real decision yet). Concretely:

1. `RenderPassTag`/`RenderPassTagMask` are defined in `RenderGraphTypes.h` (a genuine Core
   file), not `RenderPipeline.h`.
2. `PassRecord` and `RenderGraphPassSnapshot` each carry a real `RenderPassTagMask tags`
   field, copied through the same way `category`/`drawKind`/`renderPassEvent` already are.
3. Both `RenderGraphBuilder::AddRenderPass()` overloads accept an optional, trailing,
   defaulted `tags` argument, and actually stamp it onto the `PassRecord` they build.
4. `RenderPipeline::DeclareOnePhase()` actually forwards `RenderPassDesc::tags` into that
   call — fixing the real, confirmed dead-field bug described in `PHASE0`, Step 2.4.
5. Every pre-existing call site in the entire codebase compiles completely unmodified (this
   phase adds capability, it changes no existing call site's behavior or required syntax).

---

## Step 2 — The Situation

Re-read `PHASE0_MASTER_STRATEGY.md` Steps 2.4 and 2.5 before starting — both are the exact
evidence this phase exists to fix. Summary of the concrete edits needed, file by file (line
numbers are approximate — this codebase's own files may have shifted a few lines by the time
you implement this; always re-read the live file immediately before editing, never trust a
stale line number blindly):

- `RenderGraphTypes.h` — `RenderPassCategory` enum block sits at ~line 387-394. Immediately
  below `RenderPassDrawKind`'s own block (~line 419-430) is where `RenderPassEvent` currently
  starts (~line 470) — this is the established pattern for "a type PHASE1 of render-pass-3
  invented in `RenderPipeline.h`, but had to relocate here because `PassRecord` needs it."
  `RenderPassTag`/`RenderPassTagMask` need the SAME relocation, for the SAME reason.
- `RenderPipeline.h` — currently defines `RenderPassTag`/`RenderPassTagMask` at lines ~121-131
  (comment header: "`RenderPassTag` / `RenderPassTagMask` (design doc Section 7)"). This
  block must be DELETED from this file (the types move out, not duplicated) — `RenderPipeline.h`
  already `#include`s `RenderGraphTypes.h` (line ~50), so every existing use of
  `rg::RenderPassTag`/`rg::RenderPassTagMask` inside `RenderPipeline.h` itself
  (`RenderPassDesc::tags`, line ~192) keeps compiling with zero further change, since the
  fully-qualified name (`gte::rg::RenderPassTag`) and its meaning are completely unchanged —
  only its physical header moved.
- `RenderGraphTypes.h`'s `PassRecord` struct (~line 671-797-ish) — append, at the very END of
  the struct. **Confirmed live: `renderPassEvent` is NOT the current last field** — the
  Multi-Render-Target (MRT) campaign (`task_manager/mrt-1`) later appended
  `std::vector<ColorAttachmentDesc> colorAttachments;` after it (~line 796), which is the true
  current last field as of this writing. Re-read the live file immediately before editing
  regardless (a later, concurrent change could have appended something even newer), but place
  `tags` after `colorAttachments`, not after `renderPassEvent`:
  ```cpp
  // render-pass-7 campaign (task_manager/render-pass-7), PHASE1 - Core Campaign 1 ("De-
  // hardcode RenderPassCategory"). A GENERIC, feature-blind bitmask a Layer-2 module stamps
  // to identify "which conceptual group(s) does this pass belong to" WITHOUT Core ever
  // needing to know what any individual bit means - see RenderPassTag's own doc comment
  // above for the full contract, and RenderPassGroupRegistry.h (PHASE2) for the one real
  // consumer (the Editor Frame Debugger's tree-grouping logic, PHASE4). Read by NOTHING in
  // RenderGraph.cpp/RenderGraphCompiler.cpp/RenderGraphBarrierPlanner.cpp - purely
  // descriptive metadata, mirroring category/drawKind/viewScope's own identical rule.
  // Defaults to 0 (no tags) - every pre-existing AddRenderPass() call site (which never
  // mentions this field at all) keeps its exact prior behavior/meaning unchanged.
  RenderPassTagMask tags = 0;
  ```
- `RenderGraphSnapshot.h`'s `RenderGraphPassSnapshot` struct — append an identical `tags`
  field at the end, mirroring `category`'s own doc comment shape (~line 111 today).
- `RenderGraphSnapshot.cpp`'s `BuildPassSnapshot()` (~line 94) — add
  `snapshot.tags = pass.tags; // render-pass-7 campaign, PHASE1` right next to the existing
  `snapshot.category = pass.category;` line.
- `RenderGraphBuilder.h` — both `AddRenderPass()` overloads (~lines 515-551):
  - The full 8-argument overload gains a NEW trailing defaulted parameter,
    `RenderPassTagMask tags = 0`, positioned AFTER the existing `renderPassEvent` parameter
    (the current last one). Body gains `m_passes.back().tags = tags;` right next to the
    existing `m_passes.back().category = category;`/`drawKind`/`renderPassEvent` assignment
    lines.
  - The convenience (`ViewScope::Shared`/`RenderPassCategory::General`-defaulting) overload
    gains the SAME new trailing `tags` parameter and forwards it straight through to the
    first overload's call.
  - Confirm (by re-reading the live file) that this is truly the LAST parameter added by any
    prior campaign, and append after it — never insert in the middle.
- `RenderPipeline.h`'s `RenderPipeline::DeclareOnePhase()` (~line 570) — the existing
  `builder.AddRenderPass(desc.debugName, desc.kind, translatedViewScope, desc.legacyCategory,
  desc.setup, desc.execute, desc.drawKind, desc.order);` call gains ONE new trailing
  argument, `desc.tags`, i.e. becomes:
  ```cpp
  builder.AddRenderPass(desc.debugName, desc.kind, translatedViewScope, desc.legacyCategory, desc.setup,
      desc.execute, desc.drawKind, desc.order, desc.tags);
  ```
  This is the literal fix for the dead-field bug (`PHASE0` Step 2.4) — `RenderPassDesc::tags`
  (already existing, currently inert) finally reaches a real `PassRecord`.

---

## Step 3 — The Plan

1. **Relocate the type.** Cut `RenderPassTag`/`RenderPassTagMask` (with their existing doc
   comments, lightly adjusted — remove the now-stale "lives in this shared core file" framing
   that assumed `RenderPipeline.h` itself was that "shared core file"; the doc comment should
   now say these live in `RenderGraphTypes.h` specifically because `PassRecord`/
   `RenderGraphPassSnapshot` need them, mirroring `RenderPassEvent`'s own precedent exactly)
   out of `RenderPipeline.h` and into `RenderGraphTypes.h`, placed logically near
   `RenderPassCategory`/`RenderPassDrawKind` (before `RenderPassEvent`, since `RenderPassEvent`
   itself already documents being "the one exception" — `RenderPassTagMask` becomes a SECOND
   such exception, so update that comment block in `RenderPipeline.h`'s own header to mention
   both exceptions, not just `RenderPassEvent`).
2. **Add the `tags` field** to `PassRecord` (`RenderGraphTypes.h`) and to
   `RenderGraphPassSnapshot` (`RenderGraphSnapshot.h`), each at the end of their respective
   structs, each with a real doc comment (see Step 2's exact wording above).
3. **Thread it through `BuildPassSnapshot()`** (`RenderGraphSnapshot.cpp`).
4. **Add the new trailing parameter** to both `RenderGraphBuilder::AddRenderPass()` overloads
   (`RenderGraphBuilder.h`), stamping `m_passes.back().tags = tags;`.
5. **Fix the dead-field bug** in `RenderPipeline::DeclareOnePhase()` (`RenderPipeline.h`) by
   forwarding `desc.tags` into the now-9-argument `AddRenderPass()` call.
6. **Compile-check** (`cmake --build build`, incremental — do not clean-rebuild) to confirm
   every existing call site in `src/` still compiles unmodified (it must — nothing here
   removes or reorders an existing parameter).
7. **Add/extend Tier-1 tests** (this is a Tier-1-testable pure-data change per `AGENTS.md`'s
   own testability rule — every change here needs a matching test change):
   - `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` (~line 507 area, next to the
     existing `EXPECT_EQ(record.category, RenderPassCategory::General);` default-value
     assertion) — add `EXPECT_EQ(record.tags, RenderPassTagMask{0});` to the SAME existing
     test proving every `PassRecord` field's default.
   - `tests/Renderer/RenderGraph/RenderPassTests.cpp` is the ONLY test file that actually
     exercises `RenderGraphBuilder::AddRenderPass()` today (confirmed live: `AddRenderPass` has
     ZERO hits in `RenderGraphBuilderTests.cpp` — that sibling file only tests the lower-level
     `AddPass()`/`AddComputePass()` methods `AddRenderPass()` itself calls internally; do not go
     looking for "AddRenderPass()-focused tests" there, there are none). Add TWO new tests in
     `RenderPassTests.cpp`, one per overload, both alongside the existing
     `AddRenderPassSixArgumentOverloadDefaultsDrawKindToDrawMeshWhenOmitted`/
     `AddRenderPassSixArgumentOverloadStoresExplicitDrawKind` pair (~line 124-154, the
     convenience overload's own existing test group) and the existing
     `AddRenderPassFourArgumentOverloadStampsViewScopeAndCategory` (~line 67, the full overload's
     own existing test group):
     - Full (now 9-argument) overload: a new test, e.g.
       `AddRenderPassNineArgumentOverloadStampsTagsAlongsideEveryOtherField`, mirroring
       `AddRenderPassFourArgumentOverloadStampsViewScopeAndCategory`'s own fixture shape, proving
       an explicit non-zero `tags` argument is stamped onto the resulting `PassRecord`, PLUS a
       companion default-value case (e.g.
       `AddRenderPassNineArgumentOverloadDefaultsTagsToZeroWhenOmitted`) proving `tags` stays `0`
       when the new trailing argument is simply not passed.
     - Convenience (now 7-argument) overload: a new test, e.g.
       `AddRenderPassSevenArgumentOverloadStoresExplicitTags`, mirroring
       `AddRenderPassSixArgumentOverloadStoresExplicitDrawKind`'s own fixture shape, PLUS a
       companion default-value case (e.g.
       `AddRenderPassSevenArgumentOverloadDefaultsTagsToZeroWhenOmitted`) mirroring
       `AddRenderPassSixArgumentOverloadDefaultsDrawKindToDrawMeshWhenOmitted` — this overload
       forwards into the 9-argument one, so it must be proven to actually forward `tags` too, not
       just default it.
   - `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` — add an assertion that
     `RenderGraphPassSnapshot::tags` is copied through for BOTH a surviving and a culled pass
     (mirror the existing `category`/`drawKind` coverage pattern in this same file).
   - `tests/Renderer/RenderGraph/RenderPipelineTests.cpp` — this is the MOST IMPORTANT new
     test for this phase: a dedicated regression test for the dead-field bug fix itself,
     e.g. `RenderPipelineTest.DeclareIntoForwardsTagsOntoTheUnderlyingPassRecord` — register a
     provider whose `RenderPassDesc.tags` is set to some non-zero literal (e.g. `0x4u`), call
     `DeclareInto()`, `Finish()` the builder, and assert
     `EXPECT_EQ(input.passes[0].tags, 0x4u);`. This test MUST FAIL against the pre-PHASE1
     code (proving the bug is real) and PASS after this phase's fix — write it, confirm it
     fails first if you have a way to check against the unmodified `DeclareOnePhase()`, then
     apply the fix and confirm it passes.
8. **Build the test binary and run it** (`GreatTamanaEngineTests`, via `ctest` or the built
   `.exe` directly) — every test in the files touched above, plus a fast, narrow re-run of
   just this phase's own new/changed tests (do not run the FULL suite yet — that is PHASE5's
   job per this campaign's own process rules; a targeted `ctest -R <pattern>` or running the
   test binary directly with a `--gtest_filter` is appropriate here).
9. Write a `PHASE1_COMPLETION_REPORT.md` in this same folder documenting exactly what
   changed, which tests were added, and confirming the compile + targeted-test result. Commit
   via `git_add`/`git_commit`.

### Acceptance bar for this phase

- Full incremental build succeeds with zero new warnings/errors.
- Every pre-existing test still passes; every new test above passes.
- `git diff` shows ONLY additive/relocating changes described above — no existing call site's
  arguments were reordered or removed, no existing behavior changed (every real pass in the
  engine still has `tags == 0` after this phase — that is expected and correct; PHASE3 is
  where real tag VALUES first get stamped onto real passes).
