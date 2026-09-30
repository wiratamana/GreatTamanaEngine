# PHASE2 COMPLETION REPORT — Gap B Prerequisite: `WriteTexture()` gains `isDepthResource`

Campaign: `task_manager/editor-core-separation-26/` ("Buffer Roots (KeepBufferOutput) +
Blit/Copy Passes (PassKind::Blit)")
Branch: `feature/editor-core-separation` (unchanged, as required)
Phase file: `PHASE2_WRITETEXTURE_ISDEPTHRESOURCE_PARAMETER.md`

## Status: DONE ✅

## What changed

Closed the exact prerequisite gap Part B.2 of the source design document
describes: `RenderGraphBuilder::PassBuilder::WriteTexture()` had no way to
mark a write as targeting a texture's DEPTH half, while `ReadTexture()`
already had this exact capability via its own third, trailing, defaulted
`bool isDepthResource = false` parameter. `AddBlitPass()` (a future phase,
PHASE5) needs this to correctly express `BlitSpec::dstIsDepth` when a blit
destination is the depth half of an imported texture — without it,
`RenderGraph.cpp`'s existing, unchanged `ApplyUsageBarrierIfNeeded()` has no
way to learn that a `TransferDst` write targets the depth image rather than
the color image, which would barrier the wrong physical image/aspect mask
while the real `vkCmdBlitImage` call still touches the depth image — a
genuine, silent GPU synchronization hazard this phase forecloses in advance.

### `src/Renderer/RenderGraph/RenderGraphBuilder.h`
- `PassBuilder::WriteTexture()`'s declaration (now at line 299 — drifted from
  the phase file's stale "line 271" citation, exactly as PHASE0 Rule 9
  anticipates; re-confirmed against the actual file before editing) gained a
  new trailing, defaulted `bool isDepthResource = false` parameter, mirroring
  `ReadTexture()`'s existing one exactly.
- Added an 11-line doc-comment paragraph immediately above it (mirroring
  `ReadTexture()`'s own `ResourceUsage::isDepthResource` comment's tone),
  explaining the new parameter, its default, and its Blit-pass motivation —
  appended after the pre-existing Phase-6 compute-shader-campaign comment
  block, which is untouched.

### `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`
- `WriteTexture()`'s definition gained the matching parameter and now threads
  it through as `ResourceUsage::ForTexture(handle, access, isDepthResource)`'s
  third argument (previously omitted, relying on `ForTexture()`'s own
  default). `RenderGraphTypes.h`'s `ForTexture()` needed zero changes — it
  already accepted this third argument (confirmed by direct read, per PHASE0
  Step 2).

### `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`
Added exactly 2 new tests, inserted immediately after the pre-existing
`PassBuilderWriteTextureAppendsWithGivenAccess` test and immediately before
`PassBuilderCanDeclareBothReadAndWriteOfSameTextureForReadModifyWrite`:

1. **`WriteTextureWithIsDepthResourceTrueProducesDepthFlaggedResourceUsage`** —
   declares a depth-format texture, calls
   `pass.WriteTexture(handle, ResourceAccess::TransferDst, /*isDepthResource=*/true)`
   inside `setup`, then asserts (via `Finish()`'s resulting `CompiledGraphInput`)
   that `writes[0].isDepthResource == true` AND `writes[0].access ==
   ResourceAccess::TransferDst`.
2. **`WriteTextureWithoutIsDepthResourceDefaultsToFalse`** — the same shape,
   calling `pass.WriteTexture(handle, ResourceAccess::ComputeShaderWrite)`
   with no third argument at all, asserting `writes[0].isDepthResource ==
   false` — this is the one that actually proves TR3 (backward compatibility)
   for this specific method.

Note: the phase file speculated an existing `ReadTexture(..., isDepthResource=true)`
test already existed in this file to use as a template — a fresh grep found
this was NOT actually the case (only `RenderGraphBarrierPlannerTests.cpp` had
`isDepthResource`-named tests, for an unrelated assertion-guard concern). The
2 new tests above were instead modeled directly on this same file's own
existing `PassBuilderWriteDepthStencilAttachmentAppendsDepthStencilReadWrite`/
`PassBuilderWriteTextureAppendsWithGivenAccess` pattern, which is equivalent
and sufficient.

## Verification

1. **`ask_questions`**: not needed — a fresh grep of every `WriteTexture(`
   call site across all of `src/` (`RenderFeatureCompositor.cpp` x2,
   `ComputeBlurValidation.cpp`, `GBufferValidation.cpp`,
   `AtmosphereLutRenderer.cpp` x5, plus `RenderGraphBuilder.cpp`'s own
   definition) confirmed every one supplies at most 2 arguments today —
   exactly what PHASE0 Step 2/this phase's own Step 2 already anticipated, so
   no genuine ambiguity arose.
2. **Incremental build**: `cmake --build build` — succeeded, zero errors
   (83/83 build steps, including `GreatTamanaEditor.exe` and
   `GreatTamanaEngineTests.exe`).
3. **Targeted test run**:
   `ctest -C Debug -R RenderGraphBuilderTest --output-on-failure` →
   **45/45 tests passed (100%)** — up from 43 pre-existing tests in this
   file, including both new tests and every pre-existing test (zero
   regressions).
4. **Independent double-check**: a `dispatch_sub_agent` independently
   re-inspected `git status` (confirming only the 3 expected files were
   touched, no stray file, no CMakeLists.txt change), re-read all 3 changed
   files' relevant line ranges (confirming correct parameter placement,
   correct default, no leftover duplicate lines, no broken doc comments),
   re-grepped every `WriteTexture(` call site across `src/` independently,
   re-ran the incremental build, and re-ran the same targeted `ctest` filter.
   It reported **SUCCESS — everything checks out, no problems found**
   (45/45 tests passing, including both new tests by name).

## PHASE1/PHASE3-7 confirmation

**PHASE1 (Gap A) untouched** — this phase shares no file with PHASE1's own
change set (`RenderGraphCompiler.cpp/.h`, the `finalBufferOutputs`/
`KeepBufferOutput()` additions), confirmed by `git status` showing zero
overlap.

**PHASE3-6 (the rest of Gap B) not started** — no `BlitSpec`, no
`PassKind::Blit`, no `AddBlitPass()` exists yet; this phase's entire diff is
exactly what its own Step 3 plan specified: 2 lines in
`RenderGraphBuilder.h` (signature + comment block), 2 lines in
`RenderGraphBuilder.cpp` (signature + body), and one new test block in
`RenderGraphBuilderTests.cpp`. No other file was touched.

## Files touched

- `src/Renderer/RenderGraph/RenderGraphBuilder.h`
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`
- `task_manager/editor-core-separation-26/PHASE2_COMPLETION_REPORT.md` (this file)

No new source file, and no `CMakeLists.txt`/`tests/CMakeLists.txt` change,
was needed — matching PHASE0 Rule 7's expectation exactly.

## Next phase

PHASE3 (`PHASE3_BLITSPEC_PASSKIND_AND_PURE_HELPERS.md`) adds `BlitSpec`,
`PassKind::Blit`, `PassRecord::blitCommand`, the `ToString(PassKind)` fix, and
3 new pure, Tier-1-testable helper functions — it depends on THIS phase's
`isDepthResource` parameter existing (confirmed present and correct above),
but on nothing else PHASE1 produced.
