# PHASE1 Completion Report: RenderPassEvent-vs-Real-Dependency Contradiction Detector

_Child of `PHASE0_MASTER_STRATEGY.md`. Part of the `render-pass-4` campaign.
Branch: `feature/render-pass-impl`._

## Summary

Implemented exactly per `PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md`,
with no deviations from its Step 3 plan or Definition of Done. This phase is
a pure ADDITION: a new, pure, Tier-1-testable diagnostic function,
`DetectRenderPassEventContradictions()`, plus a call site inside
`RenderGraphCompiler::Compile()` that turns a non-empty result into an
unconditional `stderr` report followed by a debug-build `assert()`. Nothing
about `Compile()`'s actual `executionOrder`/`isCulled`/`textureLifetimes`
algorithm changed - confirmed by the entire pre-existing
`RenderGraphCompilerTests.cpp`/`RenderGraphSnapshotTests.cpp`/
`RenderPipelineTests.cpp` suites (224 tests total across the whole
RenderGraph module, filtered run) passing unchanged, byte-for-byte, after
this change.

## What Was Changed

- **`src/Renderer/RenderGraph/RenderGraphCompiler.h`**: added
  `RenderPassEventContradictionKind` (`OrphanReadWithLaterWriter` /
  `DeclaredEventOrderDisagreesWithRealDependency`), `RenderPassEventContradiction`,
  and the `DetectRenderPassEventContradictions(const CompiledGraphInput&,
  std::span<const std::int32_t> processingOrder)` declaration, placed
  directly above `Compile()`'s own declaration, exactly as specified.
- **`src/Renderer/RenderGraph/RenderGraphCompiler.cpp`**: added `#include
  <cassert>`/`#include <cstdio>`; defined
  `DetectRenderPassEventContradictions()` as a fully self-contained, pure
  function (never touches `Compile()`'s own
  `lastTextureWriter`/`lastBufferWriter`/`lastVolumeTextureWriter`
  bookkeeping); wired one call into `Compile()` immediately after the
  `passCount == 0` early return, using the identity permutation (raw
  declaration order `[0, 1, 2, ...]`) as `processingOrder` - matching
  `Compile()`'s own current, completely unchanged algorithm. A non-empty
  result is reported via an unconditional `std::fprintf(stderr, ...)` per
  contradiction, followed by one `assert(contradictions.empty() && ...)`.
- **Doc comments strengthened, with NO enum/field/parameter renamed anywhere**
  (Locked Design Decision 3, verified via `search_in_dir "renderPassEvent"`
  before/after - every original hit is still present, byte-identical, in
  both files):
  - `RenderGraphTypes.h`: `RenderPassEvent`'s own enum comment, and
    `PassRecord::renderPassEvent`'s own field comment.
  - `RenderPipeline.h`: the file's own header comment, and
    `RenderPassDesc::order`'s own field comment.
  - `RenderGraphBuilder.h`: both `AddRenderPass()` overloads' doc comments
    (the 8-argument overload gets the full pointer; the 6-argument
    convenience overload gets a short "same note applies, it just forwards"
    pointer).
- **`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`**: added a new
  `render-pass-4 campaign, PHASE1` test section (6 new tests, all calling
  `DetectRenderPassEventContradictions()` directly - never `Compile()` -
  except the last one, which deliberately DOES call the real `Compile()`):
  - `NoReadsOrWritesProducesNoContradictions`
  - `NormalWriterBeforeReaderProducesNoContradictions`
  - `OrphanReadWithLaterWriterIsDetected` - **the permanent regression test**
    for the exact historical `AtmosphereComposite`/`RenderOpaque` bug pattern
    (Locked Design Decision 5): a reader pass ("Composite",
    `AfterTransparents`) declared BEFORE its writer pass ("Opaque",
    `Opaques`).
  - `EdgeContradictingDeclaredEventOrderIsDetected` - a real RAW edge with
    deliberately mistagged `RenderPassEvent` values on both sides.
  - `SameEventTierNeverProducesAContradiction` - equal-tier passes never
    flag, by definition.
  - `CallingCompileWithAConsistentGraphNeverAborts` - a full, correctly
    ordered/tagged 4-pass diamond graph run through the REAL `Compile()`
    end-to-end, confirming the new wiring never false-positives and
    `executionOrder`/`isCulled` stay exactly as the pre-existing Diamond
    test already asserts.

No changes to `tests/CMakeLists.txt` - everything lives in the
already-registered `RenderGraphCompilerTests.cpp`, per the phase file's own
instruction.

## Deviations From The Plan

None. The implementation follows the phase file's own Step 3.1/3.2 sample
code and Step 3.4 test list essentially verbatim (adapted only to actually
compile - see "Notes" below for the one real adaptation).

## Notes For The Next Phase (PHASE2)

- `DetectRenderPassEventContradictions()`'s signature already takes an
  arbitrary `processingOrder` permutation, exactly as PHASE0/PHASE1 intended
  - PHASE2 does not need to touch this function's own logic at all, only
    what it is called with (a `RenderPassEvent`-sorted "effective order"
    instead of the identity/declaration-order permutation PHASE1 always
    uses).
- One naming clarification worth flagging for PHASE2's own edge/tie-break
  rewire: the phase file's own sample test descriptions call the
  `AddRenderPass()` overload with a trailing
  `(setup, execute, drawKind, renderPassEvent)` argument order matching the
  **convenience** 6-argument overload
  (`AddRenderPass(name, kind, setup, execute, drawKind, renderPassEvent)`),
  not the 8-argument
  `(name, kind, viewScope, category, setup, execute, drawKind,
  renderPassEvent)` overload literally quoted in PHASE1's own Step 3
  preamble text. Both overloads exist and both correctly stamp
  `renderPassEvent` (the 6-arg one simply forwards into the 8-arg one with
  `ViewScope::Shared`/`RenderPassCategory::General`) - this report uses the
  convenience overload throughout its own new tests, which is what actually
  compiles and is the simpler, more common call shape for a test fixture.
  This is a documentation-clarity note only, not a design deviation.
- The existing, pre-`render-pass-4` production pass graph (real
  `Application.cpp`/`RenderPipeline.h` registrations) was NOT audited for
  contradictions in this phase - that is deliberately out of scope: PHASE1
  is "ship the detector, prove it detects the known synthetic case, prove
  it doesn't false-positive on a consistent graph." Whether the detector
  fires against the CURRENT real, live production frame graph (i.e.
  whether today's shipped pass registrations already contain an
  undiscovered contradiction) was not checked here, since doing so would
  require running the live engine with a debug build and watching for
  `stderr` output / an `assert()` firing - that is exactly the kind of
  "live HTTP-driven visual verification" work PHASE3's own instructions
  reserve for itself. If PHASE2 or PHASE3 hits an `assert()` firing against
  a real production pass once the effective-order sort is switched on, that
  is expected, useful, on-scope diagnostic signal from this exact detector
  doing its job for the first time against real data - not a regression in
  this phase's own work.

## Verification Performed

- Incremental build: `cmake --build build --target gte_core` - succeeds
  (54/54 objects, no warnings/errors from the touched files).
- Incremental build: `cmake --build build --target GreatTamanaEngineTests`
  - succeeds (23/23 objects).
- Ran `tests\GreatTamanaEngineTests.exe --gtest_filter=RenderGraph*:RenderPipeline*:RenderPass*`
  - **224 tests, 224 passed, 0 failed** (includes all 21
    `RenderGraphCompilerTest` cases - the 15 pre-existing ones plus this
    phase's own 6 new ones - and every other RenderGraph-module test suite:
    `RenderGraphBuilderTest`, `RenderGraphSnapshotTest`, `RenderPipelineTest`,
    `RenderPassTest`, `RenderGraphBarrierPlannerTest`, etc.). No full `ctest`
    regression run performed, per this phase's own scope (PHASE3 only).
- `search_in_dir "renderPassEvent"` (case-insensitive) before/after this
  change: every original hit from the pre-existing 28-hit/6-file search is
  still present, byte-identical, in the post-change 68-hit/8-file search
  (the delta is purely new doc-comment lines plus the two new
  files/functions this phase added) - confirms nothing was accidentally
  renamed, per Locked Design Decision 3.

## Definition of Done - Checklist

- [x] `RenderGraphCompiler.h` declares `RenderPassEventContradictionKind`,
      `RenderPassEventContradiction`, `DetectRenderPassEventContradictions()`.
- [x] `RenderGraphCompiler.cpp` defines `DetectRenderPassEventContradictions()`
      as a pure function (no `stderr`, no `assert`, no mutation of `input`);
      `Compile()` calls it once at the top, then turns a non-empty result
      into an unconditional `stderr` report followed by a plain `assert()`.
- [x] Every doc comment listed in Step 3.3 updated, no enum/field/parameter
      renamed anywhere.
- [x] Every new test in Step 3.4 passes; the entire pre-existing
      `RenderGraphCompilerTests.cpp`/`RenderGraphSnapshotTests.cpp`/
      `RenderPipelineTests.cpp` suites still pass unchanged.
- [x] Incremental compile of `gte_core` and `GreatTamanaEngineTests`
      succeeds; running the test binary shows every test above passing.
- [x] `search_in_dir` for `RenderPassEvent` still returns every pre-existing
      hit, byte-identical, except for the doc-comment additions.
