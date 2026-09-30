# PHASE1 — Gap A: Buffer Roots (`KeepBufferOutput`)

Read `PHASE0_MASTER_STRATEGY.md` in full first (Rules, Locked Decisions,
Step 2 "Situation"). Read the source document
(`BIG_STEP_2_BUFFER_ROOTS_AND_BLIT_PASSES_2026-09-29.txt`) Part A (lines
26-91) in full. This is the first phase of the campaign — there is no
`PHASE0_COMPLETION_REPORT.md` to read (PHASE0 is the orchestrator, not an
implementation phase).

This phase is fully self-contained. It shares no file, type, or ordering
dependency with PHASE2-6 (Gap B) — it may be implemented, tested, and
committed in complete isolation.

## Step 1: The Goal

`RenderGraphBuilder::KeepBufferOutput(BufferHandle)` exists, is idempotent
(safe to call more than once for the same handle), and a buffer handle
passed to it survives `RenderGraphCompiler::Compile()`'s culling pass even
when nothing in-frame ever reads it — closing the exact gap the source
document's Part A describes: "a compute pass that fills a buffer this frame
purely for a LATER frame's own `ImportBuffer()` to read" is no longer
silently culled. A buffer never passed to it, and never read by anything
else that IS kept alive, is still correctly culled — this is an ADDITIVE,
opt-in root set, never a "every buffer write always survives" regression.

## Step 2: The Situation

Confirmed by direct read, immediately before writing this phase file (re-
confirm current line numbers before editing — see PHASE0 Rule 9):

- `src/Renderer/RenderGraph/RenderGraphCompiler.cpp`:
  - Lines 22-36: the `namespace { ... }` anonymous block already has
    `ContainsTextureHandle()` (lines 12-20) and `ContainsVolumeTextureHandle()`
    (lines 28-36) — two, near-identical, linear `for` loops over a
    `std::span<const XHandle>` doing an `operator==` comparison. This is the
    EXACT shape `ContainsBufferHandle()` copies — same file, same anonymous
    namespace, added as a THIRD sibling function immediately after
    `ContainsVolumeTextureHandle()`.
  - Lines 485-509: the root-marking scan, inside `Compile()`. The loop reads:
    ```cpp
    std::vector<bool> kept(static_cast<std::size_t>(passCount), false);
    std::vector<std::int32_t> stack;

    for (std::int32_t i = 0; i < passCount; ++i) {
        const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];
        for (const ResourceUsage& usage : pass.writes) {
            const bool isRoot = DispatchByKind(usage,
                [&](TextureHandle h) { return ContainsTextureHandle(finalOutputs, h); },
                [&](BufferHandle) { return false; },
                [&](VolumeTextureHandle h) {
                    return ContainsVolumeTextureHandle(input.finalVolumeTextureOutputs, h);
                });
            if (isRoot) {
                if (!kept[static_cast<std::size_t>(i)]) {
                    kept[static_cast<std::size_t>(i)] = true;
                    stack.push_back(i);
                }
                break;
            }
        }
    }
    ```
    The ONE line this phase changes is
    `[&](BufferHandle) { return false; },` →
    `[&](BufferHandle h) { return ContainsBufferHandle(input.finalBufferOutputs, h); },`
    — nothing else in this function changes.
- `src/Renderer/RenderGraph/RenderGraphBuilder.h`:
  - Lines 153-179: `struct CompiledGraphInput` — already has
    `finalVolumeTextureOutputs` (line 178) as the precedent. Add
    `std::vector<BufferHandle> finalBufferOutputs;` as a new, LAST field
    (mirrors how `finalVolumeTextureOutputs` was itself appended after
    `volumeTextures` when IT was added — never inserted in the middle of an
    existing struct).
  - Line 384: `void KeepVolumeTextureOutput(VolumeTextureHandle handle);` —
    the exact declaration shape `KeepBufferOutput(BufferHandle handle);`
    copies, added immediately after it (same public section of
    `RenderGraphBuilder`).
  - Line 593-607 (private members): `m_finalVolumeTextureOutputs` (line 607)
    — add `std::vector<BufferHandle> m_finalBufferOutputs;` immediately
    after it.
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`:
  - Lines 194-197:
    ```cpp
    void RenderGraphBuilder::KeepVolumeTextureOutput(VolumeTextureHandle handle)
    {
        m_finalVolumeTextureOutputs.push_back(handle);
    }
    ```
    `KeepBufferOutput()`'s body is the identical one-line shape, added
    immediately after this function.
  - Lines 199-208, `Finish()`:
    ```cpp
    CompiledGraphInput RenderGraphBuilder::Finish()
    {
        CompiledGraphInput input;
        input.passes = std::move(m_passes);
        input.textures = std::move(m_textures);
        input.buffers = std::move(m_buffers);
        input.volumeTextures = std::move(m_volumeTextures);
        input.finalVolumeTextureOutputs = std::move(m_finalVolumeTextureOutputs);
        return input;
    }
    ```
    Add `input.finalBufferOutputs = std::move(m_finalBufferOutputs);`
    immediately after the `finalVolumeTextureOutputs` line.
- `src/Renderer/RenderGraph/RenderGraphCompiler.h`, lines 143-155 — the
  `Compile()` doc comment already says "a `BufferHandle` still has no
  equivalent root set and can never be a root." This sentence becomes FALSE
  once this phase lands and MUST be rewritten (see Step 3 below) — a stale
  doc comment contradicting the real behavior is worse than no comment at
  all, and this exact sentence is the one the source document itself quotes
  as "the headline finding."
- Test precedent (`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`,
  lines 459-528): two existing tests,
  `VolumeTextureOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput` and
  `VolumeTextureWriteNeverKeptIsCulledEvenWithNoTextureFinalOutputsAtAll`,
  are the EXACT shape this phase's own 2 new tests copy, substituting
  `BufferHandle`/`WriteBuffer()`/`KeepBufferOutput()`/`ImportBuffer()` for
  `VolumeTextureHandle`/`WriteVolumeTexture()`/`KeepVolumeTextureOutput()`/
  `ImportVolumeTexture()` throughout. There is ALSO an existing test proving
  a buffer write can survive TRANSITIVELY today (lines ~420-457, the
  "PassA/PassB/PassC" 3-pass fixture) — re-read it first so the two NEW
  tests this phase adds are clearly testing the NEW direct-root case, not
  re-proving the already-covered transitive case.

## Step 3: The Plan

1. **`RenderGraphCompiler.cpp`** — add `ContainsBufferHandle()` (mirrors
   `ContainsVolumeTextureHandle()` exactly, `BufferHandle` in place of
   `VolumeTextureHandle`), immediately after `ContainsVolumeTextureHandle()`,
   inside the same anonymous namespace. Change the one `BufferHandle` lambda
   inside the Step 2 root-marking scan to
   `[&](BufferHandle h) { return ContainsBufferHandle(input.finalBufferOutputs, h); },`.
   Update the doc comment immediately above the root-marking scan (the
   "UPDATED (Atmosphere Scattering campaign, Phase 6)" block, lines 467-484)
   with a new paragraph recording THIS fix, mirroring that same paragraph's
   own style — do not delete the Phase 6 history, append after it.
2. **`RenderGraphCompiler.h`** — rewrite the "a `BufferHandle` still has no
   equivalent root set and can never be a root" sentence (inside the
   `Compile()` doc comment, lines ~143-155) to instead describe the NEW,
   THIRD independent root set (`finalBufferOutputs`/`KeepBufferOutput()`),
   mirroring how the existing sentence already describes
   `finalVolumeTextureOutputs` as the second one.
3. **`RenderGraphBuilder.h`** — add `finalBufferOutputs` to
   `CompiledGraphInput` (with a doc comment mirroring
   `finalVolumeTextureOutputs`'s own, adapted for buffers); add
   `KeepBufferOutput(BufferHandle handle);`'s declaration (with a doc
   comment mirroring `KeepVolumeTextureOutput()`'s own); add
   `m_finalBufferOutputs` as a new private member.
4. **`RenderGraphBuilder.cpp`** — add `KeepBufferOutput()`'s one-line body;
   add the one new `input.finalBufferOutputs = ...` line inside `Finish()`.
5. **Tests** (`RenderGraphCompilerTests.cpp`) — add exactly 2 new tests,
   named to mirror the existing VolumeTexture pair precisely:
   - `BufferOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput` — two
     `ImportBuffer()`-declared buffers (`bufferA`/`bufferB`), two
     `AddComputePass()` passes each writing ONE of them via `WriteBuffer()`,
     `builder.KeepBufferOutput(bufferA)` called, `Compile(input, {})` (empty
     `finalOutputs`). Assert `executionOrder` contains only the pass writing
     `bufferA`, and `input.passes[...].isCulled` is `false`/`true`
     respectively for the two passes.
   - `BufferWriteNeverKeptIsCulledEvenWithNoTextureFinalOutputsAtAll` — one
     `ImportBuffer()`-declared buffer, one pass writing it via
     `WriteBuffer()`, `KeepBufferOutput()` deliberately never called. Assert
     `compiled.executionOrder` is empty and the pass is culled.
   Both tests must use `NoOpExecute` (already defined at the top of this
   test file — reuse it, do not redeclare it) for each pass's `execute`
   callback, matching every other test in this file.
6. Confirm (do not assume) that the ALREADY-EXISTING transitive-buffer-
   survival test (the "PassA/PassB/PassC" fixture, lines ~420-457) still
   passes completely unmodified — it must, since this phase adds a new,
   independent root set and touches no other logic.

## Step 4: Verification

1. Use `ask_questions` first if anything above is unclear once you have the
   files open (per PHASE0 Rule 1).
2. Incremental build: `cmake --build build` (working directory: project
   root). Fix anything that fails to compile before moving on.
3. Targeted test run:
   `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug -R RenderGraphCompilerTest --output-on-failure`
   — confirm the 2 new tests pass AND that the full
   `RenderGraphCompilerTests.cpp` suite (every pre-existing test in that
   file) still passes, zero regressions.
4. Write `PHASE1_COMPLETION_REPORT.md` into this same folder: what changed,
   exact test names added, `ctest` output summary, and an explicit
   confirmation line that PHASE2-6 (Gap B) were NOT touched by this phase.
5. `git_add` + `git_commit` covering the code change, the test change, and
   the report, in one commit.
