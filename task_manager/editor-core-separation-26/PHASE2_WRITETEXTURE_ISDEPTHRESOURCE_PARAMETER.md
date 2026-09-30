# PHASE2 — Gap B Prerequisite: `WriteTexture()` gains `isDepthResource`

Read `PHASE0_MASTER_STRATEGY.md` in full first. Read the source document
Part B.2's "REQUIRED PREREQUISITE, before `AddBlitPass()` can be written at
all" bullet in full (this is the ONE bullet this whole phase implements).
Read `PHASE1_COMPLETION_REPORT.md` first — confirm PHASE1 (Gap A) ended in a
clean, compiling, fully-tested state; this phase does not depend on
PHASE1's content, but it DOES depend on the repository being in a clean
state to build from.

This phase is the first step of Gap B (Blit/Copy Passes). It is small and
mechanical by design — a single trailing, defaulted parameter added to one
existing method.

## Step 1: The Goal

`RenderGraphBuilder::PassBuilder::WriteTexture()` gains a new, trailing,
DEFAULTED `bool isDepthResource = false` parameter, mirroring
`ReadTexture()`'s own existing third parameter of the exact same name and
meaning. Every pre-existing `WriteTexture()` call site in the engine
compiles and behaves completely unmodified (the parameter defaults to
`false`, matching today's implicit behavior exactly). This one parameter is
what will let `AddBlitPass()` (PHASE5) correctly express `BlitSpec::dstIsDepth`
— without it, `RenderGraph.cpp`'s existing, unchanged
`ApplyUsageBarrierIfNeeded()` has no way to learn that a `TransferDst` write
targets a texture's DEPTH half rather than its color half, and a blit into a
depth destination would be barriered against the wrong physical image, with
the wrong aspect mask, while the real `vkCmdBlitImage` call (PHASE6) still
touches the real depth image — a genuine, silent GPU synchronization hazard.

## Step 2: The Situation

Confirmed by direct read (re-confirm current line numbers before editing —
see PHASE0 Rule 9):

- `src/Renderer/RenderGraph/RenderGraphBuilder.h`:
  - Line 213: `void ReadTexture(TextureHandle handle, ResourceAccess access
    = ResourceAccess::ShaderRead, bool isDepthResource = false);` — the
    EXACT signature shape `WriteTexture()` copies.
  - Line 271 (today): `void WriteTexture(TextureHandle handle,
    ResourceAccess access = ResourceAccess::ComputeShaderWrite);` — becomes
    `void WriteTexture(TextureHandle handle, ResourceAccess access =
    ResourceAccess::ComputeShaderWrite, bool isDepthResource = false);`.
    The doc comment immediately above it (lines 252-270) describes this
    method as "a general, NON-ATTACHMENT texture write" for a compute
    pass's `RWTexture` — add one short paragraph noting the new parameter
    and its Blit-pass motivation, mirroring `ReadTexture()`'s own
    `isDepthResource` doc comment on `ResourceUsage` (`RenderGraphTypes.h`
    lines 558-573) for tone.
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`:
  - Line 6: `void RenderGraphBuilder::PassBuilder::ReadTexture(TextureHandle
    handle, ResourceAccess access, bool isDepthResource) { m_pass.reads.
    push_back(ResourceUsage::ForTexture(handle, access, isDepthResource));
    }` — the exact body shape.
  - Lines 56-59 (today):
    ```cpp
    void RenderGraphBuilder::PassBuilder::WriteTexture(TextureHandle handle, ResourceAccess access)
    {
        m_pass.writes.push_back(ResourceUsage::ForTexture(handle, access));
    }
    ```
    becomes
    ```cpp
    void RenderGraphBuilder::PassBuilder::WriteTexture(TextureHandle handle, ResourceAccess access, bool isDepthResource)
    {
        m_pass.writes.push_back(ResourceUsage::ForTexture(handle, access, isDepthResource));
    }
    ```
- `src/Renderer/RenderGraph/RenderGraphTypes.h`, line 575:
  `static ResourceUsage ForTexture(TextureHandle handle, ResourceAccess
  access, bool isDepthResource = false) noexcept` — ALREADY accepts this
  third argument; zero changes needed to `RenderGraphTypes.h` for this
  phase.
- A grep of every real `WriteTexture(` call site across `src/` must be
  performed FRESH by this phase (do not trust a stale count) to confirm
  every one supplies at most 2 arguments today (handle, access) — if any
  call site is found supplying a 3rd argument already, STOP and use
  `ask_questions` (this would mean this phase's own understanding of the
  codebase has a gap).
- Test precedent: `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`
  almost certainly already has a test proving `ReadTexture(handle,
  access, /*isDepthResource=*/true)` produces a `ResourceUsage` with
  `isDepthResource == true` (search for `isDepthResource` in that file) —
  find it and use it as the exact template for this phase's new
  `WriteTexture()` counterpart test.

## Step 3: The Plan

1. **`RenderGraphBuilder.h`** — add the trailing, defaulted
   `bool isDepthResource = false` parameter to `WriteTexture()`'s
   declaration. Update the doc comment.
2. **`RenderGraphBuilder.cpp`** — add the parameter to the definition,
   thread it into `ResourceUsage::ForTexture(handle, access,
   isDepthResource)`'s third argument (currently omitted, relying on ITS OWN
   default).
3. **Tests** (`RenderGraphBuilderTests.cpp`) — add exactly 2 new tests:
   - A direct `PassBuilder::WriteTexture()` test (construct a
     `RenderGraphBuilder`, declare one pass via `AddPass()`, call
     `pass.WriteTexture(handle, ResourceAccess::TransferDst,
     /*isDepthResource=*/true)` inside `setup`, then inspect the resulting
     `CompiledGraphInput` — via `Finish()` — asserting
     `input.passes[0].writes[0].isDepthResource == true` AND
     `input.passes[0].writes[0].access == ResourceAccess::TransferDst`).
     Name it something like
     `WriteTextureWithIsDepthResourceTrueProducesDepthFlaggedResourceUsage`.
   - A second test confirming the DEFAULT is unchanged: the same shape,
     calling `pass.WriteTexture(handle, ResourceAccess::ComputeShaderWrite)`
     with NO third argument at all, asserting
     `input.passes[0].writes[0].isDepthResource == false`. Name it something
     like `WriteTextureWithoutIsDepthResourceDefaultsToFalse` — this is the
     one that actually proves TR3 (backward compatibility) for this
     specific method, so do not skip it even though it looks like it is
     "just testing a default."
4. Do NOT touch any other file. This phase's entire diff should be: 2 lines
   in `RenderGraphBuilder.h` (signature + comment), 2 lines in
   `RenderGraphBuilder.cpp` (signature + body), and one new test block in
   `RenderGraphBuilderTests.cpp`.

## Step 4: Verification

1. `ask_questions` first for any genuine ambiguity found once the files are
   open.
2. Incremental build: `cmake --build build`.
3. Targeted test run:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug -R RenderGraphBuilderTest --output-on-failure`
   — confirm the 2 new tests pass AND every pre-existing test in this file
   (which is large — 41KB — re-read its own test count before/after to
   prove nothing else regressed) still passes.
4. Write `PHASE2_COMPLETION_REPORT.md` into this same folder.
5. `git_add` + `git_commit` covering the code change, the test change, and
   the report, in one commit.
