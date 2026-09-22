# PHASE1 — Completion Report: Setup Layer, Ordered Color Attachments

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document: `PHASE1_SETUP_LAYER_ORDERED_COLOR_ATTACHMENTS.md`.

## Status: DONE

No prior-phase completion report existed to read (PHASE1 is the first
implementation phase of this campaign, as expected — confirmed no
`PHASE0_COMPLETION_REPORT.md`/similar file was present in `task_manager/mrt-1/`
before this phase started).

## What was done

### 3.1 — `RenderGraphTypes.h`

- Added `inline constexpr std::uint32_t kMaxColorAttachments = 8;`, placed
  immediately after `kInvalidIndex` (matching the phase document's "near
  `kInvalidIndex`" placement instruction).
- Added `struct ColorAttachmentDesc { TextureHandle handle; std::optional<std::array<float, 4>> clearColor; };`,
  placed immediately before `struct PassRecord` (after the `PassContext`
  forward-declaration), with the exact doc comment the phase document
  specifies.
- Added `std::vector<ColorAttachmentDesc> colorAttachments;` to `PassRecord`,
  appended at the very end of the struct (after `renderPassEvent`), per this
  file's own established "append at the end, never insert in the middle"
  convention.
- `colorClearValue`/`depthClearValue` were **NOT** removed — see the Design
  Decision below.

### 3.2 — `RenderGraphBuilder.h`/`.cpp`

- `RenderGraphBuilder::PassBuilder::WriteColorAttachment()` now:
  1. Asserts `m_pass.colorAttachments.size() < kMaxColorAttachments` before
     doing anything else (the 8-attachment cap, Locked Design Decision 2).
  2. Still unconditionally pushes a `ColorAttachmentWrite` `ResourceUsage`
     onto `pass.writes`, byte-for-byte identical to the pre-existing
     behavior (this is what keeps `RenderGraphCompiler`/barrier planning
     working per-target with zero changes to either).
  3. Still populates `pass.colorClearValue` when a clear color is supplied
     (unchanged pre-existing behavior — see Design Decision below).
  4. **New**: also appends a `ColorAttachmentDesc{ handle, clearColor }` to
     the new, ordered `pass.colorAttachments` list.
- `RenderGraphBuilder.h`'s doc comment on `WriteColorAttachment()` was
  rewritten to describe the new append behavior and reference
  `ColorAttachmentDesc`/`PassRecord::colorAttachments`, replacing the old
  "the Phases 1-8 MVP never declares more than one color/depth write per
  pass anyway" sentence that is no longer true. `WriteDepthStencilAttachment()`
  is explicitly noted as unchanged/still single-slot by design.

### 3.3 — Tier-1 tests added

- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`:
  - `WriteColorAttachmentCalledTwiceAppendsBothInOrder` — asserts
    `pass.colorAttachments` has 2 entries in call order, each with its own
    correct handle/clear color, AND `pass.writes` has 2 matching
    `ColorAttachmentWrite` entries.
  - `WriteColorAttachmentExceedingCapAssertsInDebug` (a
    `RenderGraphBuilderDeathTest`, `#ifndef NDEBUG`-guarded, mirroring
    `RenderGraphBarrierPlannerDeathTest`'s exact convention) — calls
    `WriteColorAttachment()` `kMaxColorAttachments + 1` times on one pass and
    expects a death via `EXPECT_DEATH`.
  - The 3 pre-existing tests named in the phase document
    (`PassBuilderWriteColorAttachmentAppendsColorAttachmentWrite`,
    `WriteColorAttachmentWithNoClearColorLeavesColorClearValueEmpty`,
    `WriteColorAttachmentWithClearColorRecordsItOnThePass`) were **not
    modified** and still pass unchanged.
- `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`:
  - `PassWithThreeColorWritesSurvivesCompileAndIsNotCulled` — a single pass
    declares 3 `WriteColorAttachment()` calls against 3 distinct handles,
    kept alive via a second pass that reads all three and reaches a real
    `finalOutputs` root. Asserts neither pass is culled and the execution
    order is `{0, 1}`. This directly confirms `RenderGraphCompiler.cpp`
    needed zero changes for a real 3-write pass.
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`:
  - `SnapshotOfThreeColorWritePassListsAllThreeWriteNames` — same 3-write
    pass shape, asserting `RenderGraphPassSnapshot::writeNames`/`writeKinds`
    have exactly 3 entries, matching the 3 declared handles' real names, in
    declaration order. Confirms `RenderGraphSnapshot.cpp` needed zero
    changes.

**No discrepancy was found** with `PHASE0`'s claim that
`RenderGraphCompiler.cpp`/`RenderGraphSnapshot.cpp` need zero changes for
N-ary color writes — both new tests pass against the completely unmodified
files, confirming the claim exactly as predicted. Neither file was touched.

## Design decision made (not fully pinned down by PHASE0/PHASE1 verbatim)

**`colorClearValue` disposition**: PHASE1's own Step 3.1 explicitly flagged
this as a decision point and offered a "recommended" option (stop populating
`colorClearValue` from new code, treat it as legacy dead weight) alongside a
literal illustrative code sample in Step 3.2 that omitted the
`m_pass.colorClearValue = clearColor;` line entirely.

That literal code sample directly conflicts with a separate, explicit
requirement in the very same phase document (Step 2's "Situation" section
and the Definition of Done): the 3 pre-existing `RenderGraphBuilderTests.cpp`
tests — `WriteColorAttachmentWithNoClearColorLeavesColorClearValueEmpty` and
`WriteColorAttachmentWithClearColorRecordsItOnThePass` in particular — assert
directly on `PassRecord::colorClearValue` being set (or not set) by a single
`WriteColorAttachment()` call, and the phase document says these "must keep
passing UNCHANGED" and must not be modified. It also would have silently
changed real runtime behavior: `RenderGraph::ExecuteCompiledGraph()`
(`RenderGraph.cpp`) is explicitly **out of scope for this phase** (Step 3.4 —
PHASE2's job) and still reads `colorClearValue` exclusively — if this phase
stopped populating it, every existing single-color-attachment pass in the
engine would silently stop clearing its color attachment at runtime, with no
compile error and no way to notice except a visual regression, until PHASE2
happens to land.

**Decision**: `WriteColorAttachment()` populates **both** fields on every
call — `pass.writes`/`pass.colorClearValue` exactly as before (byte-for-byte
unchanged), plus the new `pass.colorAttachments` list. This is a deliberate,
documented deviation from the phase document's own illustrative code sample
in Step 3.2, but it is the only choice consistent with every other explicit
requirement in the same document (the "keep the 3 tests passing unmodified"
requirement, and the "PHASE1 must not touch `RenderGraph.cpp`" scope
boundary). `colorClearValue` remains exactly what
`RenderGraph::ExecuteCompiledGraph()` reads until PHASE2 switches that read
over to `colorAttachments[i].clearColor` instead, at which point
`colorClearValue` becomes genuinely dead weight PHASE2 (or a later cleanup)
is free to remove, per the phase document's own already-anticipated
follow-up note. This did not require `ask_questions` — the phase document
itself already correctly identified the tension and offered enough
constraints (the "must-not-modify" test list, the "PHASE1 does not touch
`RenderGraph.cpp`" boundary) to resolve it unambiguously without further
input.

No other genuine ambiguity was hit during this phase.

## Deviations from the plan

- The one documented deviation above (`colorClearValue` still populated by
  `WriteColorAttachment()`, rather than the illustrative code sample's
  literal omission of that line). No other deviation.

## Verification

- Fast, targeted incremental compile check: `cmake --build build --target GreatTamanaEngineTests`
  (which transitively builds `gte_core`, including the modified
  `RenderGraphTypes.h`/`RenderGraphBuilder.h`/`.cpp`) — **succeeded**, no
  warnings promoted to errors. (Per PHASE0's Locked Design Decision 8, no
  full build/full `ctest` was run — that is PHASE5's job only.)
- Ran the affected test binary directly, filtered to the touched suites:
  `GreatTamanaEngineTests.exe --gtest_filter=RenderGraphBuilder*:RenderGraphCompiler*:RenderGraphSnapshot*`
  — **96/96 tests passed**, including:
  - All 3 pre-existing `WriteColorAttachment*`/clear-value tests, unmodified
    and still passing.
  - The 2 new `RenderGraphBuilderTests.cpp` tests (append order + cap death
    test).
  - The new `RenderGraphCompilerTests.cpp` 3-write survival test.
  - The new `RenderGraphSnapshotTests.cpp` 3-write-names test.

## Files changed

- `src/Renderer/RenderGraph/RenderGraphTypes.h`
- `src/Renderer/RenderGraph/RenderGraphBuilder.h`
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`
- `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`
- `tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp`
- `task_manager/mrt-1/PHASE1_COMPLETION_REPORT.md` (this file)

## Handoff notes for PHASE2

- `RenderGraph::ExecuteCompiledGraph()` (`RenderGraph.cpp`) still reads only
  the old single-`colorHandle`/`pass.colorClearValue` shape — untouched by
  this phase, exactly as planned. PHASE2 should switch its attachment-array
  construction over to `pass.colorAttachments` (now populated, in order, by
  every real call site already) and build `colorAttachmentCount` /
  `pColorAttachmentFormats` from its size.
- Once PHASE2 confirms it reads clear colors exclusively from
  `pass.colorAttachments[i].clearColor`, `PassRecord::colorClearValue` and
  the two now-parallel-writing lines in `WriteColorAttachment()` become dead
  weight safe to remove in that same phase (or a later cleanup) — see the
  Design Decision above.
