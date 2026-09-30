# PHASE3 COMPLETION REPORT — `BlitSpec`, `PassKind::Blit`, `PassRecord::blitCommand`, and 3 pure helper functions

Campaign: `task_manager/editor-core-separation-26/` ("Buffer Roots (KeepBufferOutput) +
Blit/Copy Passes (PassKind::Blit)")
Branch: `feature/editor-core-separation` (unchanged, as required)
Phase file: `PHASE3_BLITSPEC_PASSKIND_AND_PURE_HELPERS.md`

## Status: DONE ✅

## What changed

Pure vocabulary/data addition, per this phase's own explicit scope: `PassKind`
gained its third, previously-anticipated enumerator (`Blit`), a new small,
typed `BlitSpec` struct plus 3 pure, Tier-1-testable helper functions were
added beside it (Locked Decision 1), `PassRecord` gained an optional
`blitCommand` field, and `ToString(PassKind)` was updated to stay a genuinely
exhaustive, `default:`-less switch. **Nothing anywhere in the engine can
construct a real, running `PassKind::Blit` pass yet** — confirmed by a fresh
grep (see Verification, item 6, below): `PassKind::Blit` appears only inside
`RenderGraphTypes.h`/`.cpp` and their own test file.

### `src/Renderer/RenderGraph/RenderGraphTypes.h`
- `enum class PassKind` gained a third enumerator, `Blit`, after `Compute`.
  Extended (never rewrote) the pre-existing doc comment with a new paragraph
  recording that this is now the "future third kind" that comment already
  anticipated, pointing at `BlitSpec`/`PassRecord::blitCommand`/PHASE4's own
  required companion audit.
- Added `struct BlitSpec` (copied verbatim from the source design document's
  own Part B.2 code block, including its doc comment on the region fields'
  all-zero sentinel rule) — placed immediately after
  `FindMismatchedColorAttachmentExtent()`'s declaration and before
  `struct PassRecord` (since `PassRecord::blitCommand` needs the complete
  type).
- Added the 3 pure helper function declarations immediately after
  `BlitSpec`: `VkFilter ResolveEffectiveBlitFilter(const BlitSpec&) noexcept;`,
  `struct ResolvedBlitRegion { VkOffset3D min{}; VkOffset3D max{}; };`,
  `ResolvedBlitRegion ResolveBlitRegion(VkOffset3D, VkOffset3D, VkExtent2D) noexcept;`,
  `bool IsValidBlitRegion(const ResolvedBlitRegion&, VkExtent2D) noexcept;` —
  each with its own doc comment copied/adapted from the phase file.
- `struct PassRecord` gained `std::optional<BlitSpec> blitCommand;` as its new
  LAST field (appended after the pre-existing `colorAttachments`/tags-
  migration-comment fields, immediately before the struct's closing `};`,
  never inserted mid-struct — matching this file's own "append at the end"
  convention every prior campaign's field addition already follows).

### `src/Renderer/RenderGraph/RenderGraphTypes.cpp`
- `ToString(PassKind)` gained `case PassKind::Blit: return "Blit";` as a third
  case, immediately after `case PassKind::Compute:` — the switch remains a
  clean, `default:`-less, now-genuinely-exhaustive 3-case switch.
- Added the 3 function bodies immediately after
  `FindMismatchedColorAttachmentExtent()`'s own body:
  - `ResolveEffectiveBlitFilter`: `(spec.srcIsDepth || spec.dstIsDepth) ?
    VK_FILTER_NEAREST : spec.filter` — exactly as simple as the phase file
    specified, no over-engineering.
  - `ResolveBlitRegion`: checks the all-6-components-zero sentinel on both
    `regionMin` and `regionMax`; if true, returns the full resolved extent
    (`{0,0,0}` to `{width, height, 1}`); otherwise passes the input through
    completely unchanged.
  - `IsValidBlitRegion`: strict `max > min` on all 3 axes, non-negative
    minimums, and `max` never exceeding the resolved extent (`z <= 1` for a
    2D texture's own resolved extent).

### `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`
- Extended the pre-existing `RenderGraphPassKindTest` fixtures' enumerator
  arrays/assertions to include `PassKind::Blit`/`"Blit"`.
- Added a new test block (mirroring
  `FindMismatchedColorAttachmentExtentTest`'s own precedent — plain data in,
  plain data/bool out, no fixture class needed) with 8 new test cases:
  1. `RenderGraphResolveEffectiveBlitFilterTest.ReturnsSpecFilterWhenNeitherSideIsDepth`
  2. `RenderGraphResolveEffectiveBlitFilterTest.ForcesNearestWhenSrcIsDepth`
  3. `RenderGraphResolveEffectiveBlitFilterTest.ForcesNearestWhenDstIsDepth`
  4. `RenderGraphResolveBlitRegionTest.AllZeroSentinelResolvesToFullExtent`
  5. `RenderGraphResolveBlitRegionTest.NonSentinelPassesThroughUnchanged`
  6. `RenderGraphIsValidBlitRegionTest.RejectsInvertedAxis`
  7. `RenderGraphIsValidBlitRegionTest.RejectsOutOfBoundsMax`
  8. `RenderGraphIsValidBlitRegionTest.AcceptsFullResolvedExtent` (the one
     that proves `ResolveBlitRegion()`'s sentinel-resolved output correctly
     composes with `IsValidBlitRegion()` — the same composition PHASE6's
     execution branch will rely on).

## An honest finding beyond the phase file's own text (Step 3, item 6)

The phase file's own Step 3 required confirming, via `search_in_dir`, that
`RenderGraphTypes.cpp`'s `ToString(PassKind)` is the only exhaustive-switch-
shaped `PassKind` consumer inside `src/Renderer/`, `src/Core/`, or
`src/Application/` outside `FrameDebuggerData.cpp` (PHASE4's own job). A fresh
grep for `PassKind::` and for `\.kind\s*[=!]=`/`switch\s*\(.*kind` across all
of `src/` found **one additional site PHASE0's own Step 2 grep did not
surface**: `RenderGraphBuilder.h`'s generic `AddRenderPass()` template
(line ~573) contains `if (kind == PassKind::Compute) { AddComputePass(...); }
else { AddPass(...); }` — structurally the same "not Compute assumed
Graphics" shape the source document warns about, just written as `if`/`else`
rather than `switch`, which is why PHASE0's own `\.kind\s*[=!]=` regex missed
it (that call uses a bare local parameter `kind`, never a `.kind` member
access).

**This does NOT require a fix, and does NOT trigger this phase's own STOP
condition** (which is scoped to an *exhaustive switch*, not this shape) —
confirmed by inspection, not assumption: `AddRenderPass()`'s own template
signature mandates a `SetupFn`/`ExecuteFn` callback pair
(`template <typename SetupFn, typename ExecuteFn> void AddRenderPass(...)`),
while the source design document's own Part B.2 explicitly states
`AddBlitPass()` (PHASE5) accepts **no** `setup`/`execute` callbacks at all
("No `setup`/`execute` callbacks are needed or accepted — the whole content
of this pass IS the blit"). A method with a fundamentally different, callback-
free signature cannot be implemented by calling this callback-shaped template
— `AddBlitPass()` is therefore structurally guaranteed to be its own,
separate builder method (PHASE5's job), never routed through
`AddRenderPass()`'s `if (kind == PassKind::Compute)` branch. That branch will
therefore never be evaluated with `kind == PassKind::Blit`, by construction,
for as long as this remains true. Recorded here, explicitly, so a future
reader of this same grep does not have to re-derive this reasoning from
scratch — mirroring this campaign's own Locked Decision 2 precedent for
`FrameDebuggerData.cpp`'s two confirmed-safe sites.

No other `PassKind` comparison/switch site was found anywhere in `src/`
outside `FrameDebuggerData.cpp` (PHASE4's own scope) and this phase's own
`RenderGraphTypes.h`/`.cpp`.

## Verification

1. **`ask_questions`**: not needed — the phase file's own Step 2/Step 3 were
   detailed and precise enough (every code shape cited was re-confirmed
   against the actual current files before editing, per PHASE0 Rule 9;
   line numbers had drifted slightly due to PHASE1/PHASE2's own edits, and
   were re-resolved via fresh `search_in_dir`/`read_line` calls, not assumed)
   that no genuine ambiguity arose. The one new finding (the
   `RenderGraphBuilder.h` if/else above) was resolved by direct code
   inspection, not guesswork — see the dedicated section above.
2. **Incremental build**: `cmake --build build` — succeeded, zero errors
   (143/143 build steps, including `GreatTamanaEditor.exe` and
   `GreatTamanaEngineTests.exe`). Full build output was read in full — no new
   `-Wswitch`-class warning appeared anywhere (expected: `PassKind::Blit` is
   not yet consumed by any exhaustive switch outside `RenderGraphTypes.cpp`
   itself, which this phase already updated).
3. **Targeted test run**:
   `ctest -C Debug -R "RenderGraphPassKindTest|RenderGraphResolveEffectiveBlitFilterTest|RenderGraphResolveBlitRegionTest|RenderGraphIsValidBlitRegionTest|RenderGraphFindMismatchedColorAttachmentExtentTest"`
   → **15/15 tests passed (100%)**, including all 8 new tests and every
   pre-existing test these filters cover (zero regressions). A second,
   broader run across every other test suite in this same file
   (`RenderGraphHandleTest`/`RenderGraphDescTest`/`RenderGraphResourceAccessTest`/
   `RenderGraphPassRecordTest`/`RenderGraphResourceUsageTest`/
   `RenderGraphDispatchByKindTest`/`RenderGraphRenderPassCategoryTest`/
   `RenderGraphRenderPassDrawKindTest`/`RenderGraphRenderPassEventTest`)
   confirmed **54/54 additional tests passed (100%)** — the entire
   `RenderGraphTypesTests.cpp` file (69 tests total) is green.
4. **`PassKind::Blit` non-producibility confirmed**: `search_in_dir` for
   `PassKind::Blit` across `src/` found exactly 3 hits, all inside
   `RenderGraphTypes.cpp`/`.h` (the `ToString()` case and 2 doc-comment
   mentions) — zero hits anywhere else in the engine. `PassKind::Blit` is
   real vocabulary today, but genuinely unreachable by any live pass, exactly
   as this phase's own Step 1 requires.
5. **Independent double-check**: a `dispatch_sub_agent` independently
   re-inspected all 3 changed files against the phase spec line-by-line
   (enum shape, `BlitSpec` field-for-field, all 3 helper function
   signatures/bodies, `PassRecord::blitCommand`'s placement at the true end
   of the struct, `ToString(PassKind)`'s exhaustiveness, every new test
   case, and the test file's own end-of-file integrity for leftover
   duplicate lines — a known failure mode of the edit tool used, and
   something this phase's own edits did in fact trip twice, both caught and
   fixed immediately, see below), re-confirmed `git status` shows only the 3
   expected files touched, re-ran the incremental build (no-op, already
   built), re-ran the same targeted `ctest` filter (15/15), and independently
   re-confirmed the `PassKind::Blit` non-producibility grep. It reported
   **SUCCESS — everything checks out, no problems found**.

### A note on the edit tool's own auto-dedup behavior this phase encountered twice

Two of this phase's own `edit_line` calls produced a leftover, exact-duplicate
line immediately after the newly-inserted block (once in
`RenderGraphTypes.h`, a duplicate `const char* ToString(PassKind kind)
noexcept;` declaration; once in `RenderGraphTypes.cpp`, a duplicate
`return "Compute";` inside the `ToString(PassKind)` switch) — both were
caught immediately by direct re-reading of the tool's own returned context
and fixed with a follow-up `edit_line` deleting the stray duplicate line, well
before the build/test/double-check steps above ran. Neither made it into the
final, committed diff — recorded here purely for transparency, per this
project's "brutal honesty" convention, not because it represents an
unresolved problem.

## PHASE1/PHASE2/PHASE4-7 confirmation

**PHASE1 (Gap A) and PHASE2 (Gap B prerequisite) untouched** — this phase
shares no file with either (`RenderGraphCompiler.cpp/.h`,
`RenderGraphBuilder.h/.cpp` were not touched by this phase at all), confirmed
by `git status` showing only the 3 files listed below as modified.

**PHASE4-7 not started** — no `RenderGraphBuilder` method exists yet that can
produce a `PassKind::Blit` pass (that is PHASE5's job); `FrameDebuggerData.cpp`
was not touched by this phase (that is PHASE4's job, required to land BEFORE
PHASE5 per PHASE0's own ordering rationale).

## Files touched

- `src/Renderer/RenderGraph/RenderGraphTypes.h`
- `src/Renderer/RenderGraph/RenderGraphTypes.cpp`
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`
- `task_manager/editor-core-separation-26/PHASE3_COMPLETION_REPORT.md` (this file)

No new source file, and no `CMakeLists.txt`/`tests/CMakeLists.txt` change,
was needed — matching PHASE0 Rule 7's expectation exactly.

## Next phase

PHASE4 (`PHASE4_FRAMEDEBUGGER_PASSKIND_EXHAUSTIVENESS_AUDIT.md`) — the
REQUIRED companion audit converting `FrameDebuggerData.cpp`'s 3 named hazard
sites into a real, exhaustive three-way `PassKind` dispatch, and documenting
(in-file) why its other 2 grep hits are already safe. Must land before PHASE5
(`AddBlitPass()`) per PHASE0's own explicit ordering requirement — this
phase's `PassKind::Blit`/`BlitSpec`/`PassRecord::blitCommand` additions are
exactly what PHASE4 now has real vocabulary to audit against.
