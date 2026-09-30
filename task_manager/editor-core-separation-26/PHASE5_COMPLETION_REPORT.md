# PHASE5 COMPLETION REPORT — `RenderGraphBuilder::AddBlitPass()`

Campaign: `task_manager/editor-core-separation-26/` ("Buffer Roots (KeepBufferOutput) +
Blit/Copy Passes (PassKind::Blit)")
Branch: `feature/editor-core-separation` (unchanged, as required)
Phase file: `PHASE5_ADDBLITPASS_BUILDER_ENTRYPOINT.md`

## Status: DONE ✅

## What changed

`RenderGraphBuilder::AddBlitPass()` now exists as a real, official, first-class
pass-declaration entry point — the first thing in this whole campaign that can
produce a genuinely running `PassKind::Blit` pass. It is still pure data as of
this phase: **no `vkCmdBlitImage` call, and no other real Vulkan command, was
added anywhere** — confirmed by a fresh grep (see Verification below). PHASE6
is the phase that adds the actual execution branch.

### `src/Renderer/RenderGraph/RenderGraphBuilder.h`

Added the public, NON-TEMPLATE declaration immediately after `AddRenderPass()`'s
two overloads and before `SetDebugMetadataSink()` (mirroring
`KeepVolumeTextureOutput()`'s own "sibling placement" precedent):

```cpp
void AddBlitPass(const char* name, const BlitSpec& spec,
    RenderPassEvent renderPassEvent = RenderPassEvent::Opaques,
    ViewScope viewScope = ViewScope::Shared,
    RenderPassCategory category = RenderPassCategory::General,
    RenderPassTagMask tags = 0);
```

The doc comment records why `renderPassEvent` gets no special-cased default
beyond the ordinary `RenderPassEvent::Opaques` every other pass-declaring
method already uses — copied, per the phase file's own instruction, from the
source design document's own reasoning ("a blit with no real in-frame reader
has no data dependency to order it by, so it MUST have an explicit way to be
placed at a real `RenderPassEvent` tier").

Every pre-existing `AddPass()`/`AddComputePass()`/`AddRenderPass()` overload is
completely unmodified — confirmed both by direct re-read and by the
`dispatch_sub_agent` double-check (see below).

### `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`

Added the implementation between `KeepBufferOutput()` and `Finish()` — the ONE
new function in this whole campaign that constructs a `PassRecord` directly
rather than going through `AddPass()`/`AddComputePass()` (neither accepts a
`setup`/`execute` callback pair shaped like a blit needs — a blit has no
callback at all):

```cpp
void RenderGraphBuilder::AddBlitPass(const char* name, const BlitSpec& spec, RenderPassEvent renderPassEvent,
    ViewScope viewScope, RenderPassCategory category, RenderPassTagMask tags)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::AddBlitPass requires a non-empty, static-storage-duration pass name");

    m_passes.push_back(PassRecord{});
    PassRecord& pass = m_passes.back();
    pass.name = name;
    pass.kind = PassKind::Blit;
    pass.viewScope = viewScope;
    pass.renderPassEvent = renderPassEvent;
    pass.blitCommand = spec;

    PassBuilder passBuilder(pass);
    passBuilder.ReadTexture(spec.src, ResourceAccess::TransferSrc, spec.srcIsDepth);
    passBuilder.WriteTexture(spec.dst, ResourceAccess::TransferDst, spec.dstIsDepth);

    if (m_debugMetadataSink != nullptr) {
        m_debugMetadataSink->OnPassDeclared(m_passes.size() - 1, category, RenderPassDrawKind::Blit, tags);
    }
}
```

`drawKind` is HARDCODED to `RenderPassDrawKind::Blit` — never a parameter of
`AddBlitPass()` itself, exactly as the phase file requires (a blit pass's
draw-kind is always, definitionally, `Blit`).

### `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`

Added exactly the 6 test cases the phase file's Step 3 enumerates (reusing the
pre-existing `FakeMetadataSink` class — no second one declared):

1. `AddBlitPassProducesCorrectlyPopulatedPassRecord` — `kind == PassKind::Blit`;
   `blitCommand.has_value()`; `blitCommand->src/dst` match; `reads[0]`/`writes[0]`
   carry the correct handle/access (`TransferSrc`/`TransferDst`).
2. `AddBlitPassWithDstIsDepthTrueProducesDepthFlaggedWrite` —
   `writes[0].isDepthResource == true`.
3. `AddBlitPassWithSrcIsDepthTrueProducesDepthFlaggedRead` —
   `reads[0].isDepthResource == true`.
4. `AddBlitPassCallsSinkWithBlitDrawKindAndSuppliedCategoryAndTags` — sink
   receives `drawKind == RenderPassDrawKind::Blit`, plus the caller-supplied
   non-default `category`/`tags`.
5. `AddBlitPassCallerSuppliedRenderPassEventReachesPassRecordUnchanged` —
   `renderPassEvent == RenderPassEvent::AfterEverything` reaches the
   `PassRecord` unchanged.
6. `AddBlitPassDefaultsViewScopeToSharedAndCategoryToGeneral` — every trailing
   parameter left at default: `viewScope == ViewScope::Shared` (direct field),
   sink receives `RenderPassCategory::General`/`RenderPassTagMask{0}`.

Every pre-existing test in this file (45 tests) still passes unmodified.

## Verification

1. **`ask_questions`**: not needed. The phase file's own Step 2 ("IMPORTANT
   CROSS-CAMPAIGN CORRECTION") already resolved the one real ambiguity (the
   source document's pre-BIG-STEP-1 code sample vs. the real, current,
   sink-based `AddRenderPass()` shape) — re-confirmed by direct, fresh reads
   of `RenderGraphBuilder.h`'s current `AddRenderPass()` body, `PassBuilder`'s
   accessibility, and `PassRecord::blitCommand`'s exact placement before
   writing any code. No further genuine ambiguity was found.
2. **Incremental build**: `cmake --build build` — succeeded, zero errors
   (82/82 build steps, including `GreatTamanaEditor.exe` and
   `GreatTamanaEngineTests.exe`).
3. **Targeted test run**:
   `ctest -C Debug -R RenderGraphBuilderTest --output-on-failure` →
   **51/51 tests passed (100%)** — the entire pre-existing 45-test suite plus
   all 6 new `AddBlitPass` tests, zero regressions.
4. **No Vulkan call added**: `search_in_dir` for `vkCmd` across
   `RenderGraphBuilder.h`/`.cpp` found exactly 3 hits, all inside comments (2
   pre-existing, unrelated; 1 new, describing PHASE6's future work) — zero
   hits in `RenderGraphBuilder.cpp` at all. Confirmed no real Vulkan call was
   added anywhere in this phase's diff.
5. **Independent double-check**: a `dispatch_sub_agent` independently
   re-read all 3 changed files against the phase spec (signature, body,
   non-template-ness, direct `PassRecord` construction bypassing
   `AddPass()`/`AddComputePass()`, hardcoded `RenderPassDrawKind::Blit`,
   sink-forwarding null-check), confirmed every pre-existing
   `AddPass()`/`AddComputePass()`/`AddRenderPass()` overload is untouched,
   re-ran `git_status` (confirmed only the 3 expected files modified),
   re-ran the incremental build (no-op, already built) and the same targeted
   `ctest` filter (51/51), and spot-checked all 6 new tests' assertions
   against the phase file's own Step 3 requirements. It reported
   **SUCCESS — everything checks out, no problems found**, with no ambiguity
   requiring `ask_questions` on its own part either.

## `PassKind::Blit` is now producible for the first time in this campaign

`search_in_dir` for `AddBlitPass` across `src/` confirms the only call sites
are this phase's own declaration/implementation and its own test file — no
production code calls it yet (that begins with PHASE6's `BlitValidation`
pass). `FrameDebuggerData.cpp`'s companion audit (PHASE4) already landed
before this phase started, per PHASE0's explicit ordering requirement, so a
real `PassKind::Blit` pass declared from here on is safe for the Frame
Debugger's own tree-building logic to encounter.

## PHASE1/PHASE2/PHASE3/PHASE4/PHASE6/PHASE7 confirmation

**PHASE1 (Gap A), PHASE2 (Gap B prerequisite), PHASE3 (`BlitSpec`/
`PassKind::Blit`/pure helpers), and PHASE4 (Frame Debugger audit) untouched by
this phase** — confirmed by `git status` showing only
`src/Renderer/RenderGraph/RenderGraphBuilder.h`,
`src/Renderer/RenderGraph/RenderGraphBuilder.cpp`, and
`tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` modified (plus this
report). `RenderGraphCompiler.cpp/.h`, `RenderGraphTypes.h/.cpp`, and
`src/Editor/FrameDebuggerData.cpp` were not touched at all by this phase.

**PHASE6 not started** — no `vkCmdBlitImage` call exists anywhere yet; no
`BlitValidation` pass exists yet; `SupportsBlitSrcDst()`/
`VulkanDevice::SupportsDepthBlit()` do not exist yet. This phase's own
required entry point is now landed, satisfying the ordering requirement
PHASE0 imposed before PHASE6 may start adding the real Vulkan execution
branch.

## Files touched

- `src/Renderer/RenderGraph/RenderGraphBuilder.h`
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`
- `task_manager/editor-core-separation-26/PHASE5_COMPLETION_REPORT.md` (this file)

No new source file, and no `CMakeLists.txt`/`tests/CMakeLists.txt` change,
was needed — matching PHASE0 Rule 7's expectation exactly.

## Next phase

PHASE6 (`PHASE6_BLIT_EXECUTION_AND_LIVE_VALIDATION.md`) — the real
`vkCmdBlitImage` execution branch in `RenderGraph.cpp`;
`SupportsBlitSrcDst()` (`FormatCapabilities.h`/`.cpp`) +
`VulkanDevice::SupportsDepthBlit()`; the new, permanent, minimal,
Debug-category `BlitValidation` pass; the live, `GET /get_texture`-driven
color-to-color screenshot proof; the depth-to-depth attempt. This campaign's
own flagged highest-risk phase.
