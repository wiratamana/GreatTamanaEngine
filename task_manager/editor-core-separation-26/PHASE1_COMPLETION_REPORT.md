# PHASE1 COMPLETION REPORT — Gap A: Buffer Roots (`KeepBufferOutput`)

Campaign: `task_manager/editor-core-separation-26/` ("Buffer Roots (KeepBufferOutput) +
Blit/Copy Passes (PassKind::Blit)")
Branch: `feature/editor-core-separation` (unchanged, as required)
Phase file: `PHASE1_BUFFER_ROOTS_KEEPBUFFEROUTPUT.md`

## Status: DONE ✅

## What changed

Closed the exact gap the source design document's Part A describes:
`RenderGraphCompiler::Compile()`'s root-marking scan had exactly one line
that permanently forecloses a `BufferHandle` from ever being a root
(`[&](BufferHandle) { return false; }`), meaning a compute pass whose only
observable effect is a buffer write with no in-frame reader was always
silently culled, no matter what it declared. Fixed by mirroring the
already-shipped `VolumeTextureHandle` root-set precedent
(`finalVolumeTextureOutputs`/`KeepVolumeTextureOutput()`, Atmosphere
Scattering campaign Phase 6) verbatim, for buffers.

### `src/Renderer/RenderGraph/RenderGraphCompiler.cpp`
- Added `ContainsBufferHandle(std::span<const BufferHandle>, const BufferHandle&)`
  — a third sibling of `ContainsTextureHandle()`/`ContainsVolumeTextureHandle()`,
  same anonymous namespace, same linear-scan shape.
- Changed the root-marking scan's `BufferHandle` lambda from
  `[&](BufferHandle) { return false; }` to
  `[&](BufferHandle h) { return ContainsBufferHandle(input.finalBufferOutputs, h); }`.
- Extended (never deleted) the existing "UPDATED (Atmosphere Scattering
  campaign, Phase 6)" doc-comment block above the scan with a new "UPDATED
  AGAIN (editor-core-separation-26 campaign, PHASE1)" paragraph recording
  this fix, and updated the inline lambda comment to match.

### `src/Renderer/RenderGraph/RenderGraphCompiler.h`
- Rewrote the `Compile()` doc comment's "a `BufferHandle` still has no
  equivalent root set and can never be a root" sentence — that claim is now
  false — to instead describe `input.finalBufferOutputs` as a THIRD,
  independent root set, mirroring how the same comment already describes
  `finalVolumeTextureOutputs` as the second one.

### `src/Renderer/RenderGraph/RenderGraphBuilder.h`
- `CompiledGraphInput` gained a new, LAST field:
  `std::vector<BufferHandle> finalBufferOutputs;` (appended after
  `finalVolumeTextureOutputs`, never inserted mid-struct), with a doc comment
  mirroring `finalVolumeTextureOutputs`'s own.
- Added `void KeepBufferOutput(BufferHandle handle);` — public, declared
  immediately after `KeepVolumeTextureOutput()`, doc comment mirrored
  (idempotent, opt-in root marking).
- Added `std::vector<BufferHandle> m_finalBufferOutputs;` as a new private
  member, immediately after `m_finalVolumeTextureOutputs`.

### `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`
- `KeepBufferOutput()`'s body: one line, `m_finalBufferOutputs.push_back(handle);`
  — identical shape to `KeepVolumeTextureOutput()`.
- `Finish()` gained one new line:
  `input.finalBufferOutputs = std::move(m_finalBufferOutputs);`, immediately
  after the `finalVolumeTextureOutputs` line.

### `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`
Added exactly 2 new tests, named to mirror the existing VolumeTexture pair
precisely, both using the shared `NoOpExecute` helper (never redeclared):

1. **`BufferOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput`** — two
   `ImportBuffer()`-declared buffers (`bufferA`/`bufferB`), two
   `AddComputePass()` passes each writing ONE of them via `WriteBuffer()`,
   `builder.KeepBufferOutput(bufferA)` called, `Compile(input, {})` (empty
   `finalOutputs`). Asserts `executionOrder` contains only the pass writing
   `bufferA`, and the two passes' `isCulled` flags are `false`/`true`
   respectively.
2. **`BufferWriteNeverKeptIsCulledEvenWithNoTextureFinalOutputsAtAll`** — one
   `ImportBuffer()`-declared buffer, one pass writing it via `WriteBuffer()`,
   `KeepBufferOutput()` deliberately never called. Asserts
   `compiled.executionOrder` is empty and the pass is culled.

Confirmed (not assumed) that the already-existing transitive-buffer-survival
test, `BufferOnlyWriteSurvivesCullingOnlyWhenAReaderReachesATextureFinalOutput`
(the PassA/PassB/PassC fixture), and both pre-existing VolumeTexture tests,
still pass completely unmodified.

## PHASE2-6 (Gap B) confirmation

**Not touched at all.** This phase is fully self-contained, per the phase
file's own explicit statement — it shares no file, type, or ordering
dependency with the Blit/Copy Passes work (PHASE2-6). `git status` after
this phase's edits shows exactly the 5 files listed above as modified, plus
the untracked `task_manager/editor-core-separation-26/` campaign folder —
nothing under `BlitSpec`/`PassKind`/`WriteTexture`/`AddBlitPass` or any other
Gap-B-related file was touched.

## Verification

1. **`ask_questions`**: not needed — the phase file's own Step 2/Step 3 were
   detailed and precise enough (every line number cited was re-confirmed
   against the actual current files before editing, per PHASE0 Rule 9) that
   no genuine ambiguity arose.
2. **Incremental build**: `cmake --build build` — succeeded, zero errors
   (83/83 build steps, including `GreatTamanaEditor.exe` and
   `GreatTamanaEngineTests.exe`).
3. **Targeted test run**:
   `ctest -C Debug -R RenderGraphCompilerTest --output-on-failure` →
   **36/36 tests passed (100%)**, including both new tests and every
   pre-existing test in `RenderGraphCompilerTests.cpp` (zero regressions).
4. **Independent double-check**: a `dispatch_sub_agent` was used to
   independently re-inspect every one of the 5 changed files against the
   phase spec (checking for leftover duplicate lines from the edit tool's
   splice mechanism, correct placement, correct naming), re-confirm
   `git status` shows only the expected files touched, re-run the
   incremental build, and re-run the same targeted `ctest` filter. It
   reported **SUCCESS — everything checks out, no problems found**.

## Files touched

- `src/Renderer/RenderGraph/RenderGraphCompiler.cpp`
- `src/Renderer/RenderGraph/RenderGraphCompiler.h`
- `src/Renderer/RenderGraph/RenderGraphBuilder.h`
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp`
- `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`
- `task_manager/editor-core-separation-26/PHASE1_COMPLETION_REPORT.md` (this file)

No new source file, and no `CMakeLists.txt`/`tests/CMakeLists.txt` change,
was needed — matching PHASE0 Rule 7's expectation exactly.

## Next phase

PHASE2 (`PHASE2_WRITETEXTURE_ISDEPTHRESOURCE_PARAMETER.md`) starts Gap B
(Blit/Copy Passes) and has zero dependency on this phase's work.
