# PHASE3 — COMPLETION REPORT: `RenderGraphCompiler::Compile()` Root-Marking Fix (the Keep-Alive Guarantee)

Campaign folder: `task_manager/editor-core-separation-27/`
Branch: `feature/editor-core-separation` (unchanged, no new branch created)

## Summary

Closed the exact silent-culling gap `VolumeTextureHandle` and `BufferHandle`
already had before their own fixes shipped (Atmosphere Scattering Phase 6 and
`editor-core-separation-26` PHASE1 respectively), this time for a
`TextureHandle` used as a future persistent-cache entry: a pass whose ONLY
declared write is a `TextureHandle` present in the new
`CompiledGraphInput::persistentCacheTextures` vector (added, unpopulated, by
PHASE2) — with nothing else reading/writing it this frame, and the handle NOT
present in that call's own `finalOutputs` — now survives
`RenderGraphCompiler::Compile()`'s Step 2 backward-reachability/culling scan,
exactly as the campaign's Goal item 4 requires.

## Step 3.1 re-confirmation (done fresh, before editing)

Re-read every citation in this phase's own `.md` file against the actual
current files, before touching anything:

- `RenderGraphCompiler.cpp`'s three anonymous-namespace helpers
  (`ContainsTextureHandle`/`ContainsVolumeTextureHandle`/`ContainsBufferHandle`)
  — confirmed at lines 12, 28, 46 respectively (matching the phase file's
  `~line 12-54` estimate). Confirmed `ContainsTextureHandle()` already takes
  `std::span<const TextureHandle>` — no new helper function was written, per
  the phase file's own explicit instruction.
- `Compile()`'s Step 2 root-marking scan — confirmed the exact block cited in
  the phase file (the `DispatchByKind(usage, [&](TextureHandle h) { return
  ContainsTextureHandle(finalOutputs, h); }, ...)` call) at (fresh) lines
  530-535 — the phase file's own `~515-544` estimate held, drift was
  negligible (PHASE1/PHASE2 do not touch this file, confirmed).
- `CompiledGraphInput::persistentCacheTextures` (`RenderGraphBuilder.h` line
  210) and the `Finish()` move (`RenderGraphBuilder.cpp` line 245) — both
  confirmed present and correctly typed (`std::vector<TextureHandle>`) via
  `search_in_dir`, exactly as PHASE2's own completion report described.
- `search_in_dir` for `finalBufferOutputs` across `tests/` — confirmed
  `RenderGraphCompilerTests.cpp`'s own
  `BufferOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput` /
  `BufferWriteNeverKeptIsCulledEvenWithNoTextureFinalOutputsAtAll` tests as the
  exact shape to mirror for this phase's own four new tests (hand-fabricated
  `CompiledGraphInput`, direct `push_back()` onto the opt-in root vector,
  built through a real `RenderGraphBuilder`/`Finish()`).

No ambiguity was found beyond what `PHASE0_MASTER_STRATEGY.md` and this
phase's own `.md` already resolve — `ask_questions` was not needed for this
phase. No `dispatch_sub_agent` delegation was used either — the change is a
single, small, well-specified edit plus four mirrored tests.

## Changes made

1. **`src/Renderer/RenderGraph/RenderGraphCompiler.cpp`** —
   - Extended the existing "UPDATED AGAIN" comment history above the Step 2
     root-marking scan with a new paragraph documenting this campaign's own
     fix (citing `editor-core-separation-27`, PHASE3, Section 5.2).
   - Changed exactly the `TextureHandle` lambda inside the `DispatchByKind()`
     call from
     `[&](TextureHandle h) { return ContainsTextureHandle(finalOutputs, h); }`
     to
     `[&](TextureHandle h) { return ContainsTextureHandle(finalOutputs, h) || ContainsTextureHandle(input.persistentCacheTextures, h); }`
     — an OR of two vectors, reusing the pre-existing `ContainsTextureHandle()`
     helper for both, with NO new helper function added.
   - No other line in this file was touched — `RenderGraphBarrierPlanner.cpp`
     was not touched at all (TR1, confirmed unaffected by this change), and no
     other part of `Compile()` (edge construction, topological sort, cycle
     detection, `DetectRenderPassEventContradictions()`) was touched.
2. **`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`** — added four
   new Tier-1 tests, inserted immediately after the existing buffer-root-set
   test block (before the `render-pass-4` contradiction-detection section),
   exactly matching this phase's own `.md` Step 4 list:
   - `PersistentCacheTextureWriteSurvivesCullingWithNoFinalOutputsAtAll` —
     basic keep-alive: one pass writes a texture pushed onto
     `persistentCacheTextures`, `finalOutputs` is empty, the pass survives.
   - `TextureWriteNotInPersistentCacheOrFinalOutputsIsStillCulled` — negative
     control: same shape, handle in neither vector, the pass is culled
     (proves the fix is additive, not blanket-permissive).
   - `OrdinaryFinalOutputsRootStillWorksWithEmptyPersistentCacheTextures` —
     OR-combination: the pre-existing `finalOutputs` path still keeps a pass
     alive with an empty `persistentCacheTextures` (proves zero regression to
     the untouched path).
   - `PersistentCacheRootDoesNotCrossContaminateAnUnrelatedPass` — transitive/
     independence check: one pass kept via `persistentCacheTextures`, a
     second, unrelated pass kept via `finalOutputs`, a third, truly dead pass
     stays culled — proves no cross-contamination between independent root
     sets.

No new file was created (per this phase's `.md` — only edits to
`RenderGraphCompiler.cpp` and the already-registered
`RenderGraphCompilerTests.cpp`). No `tests/CMakeLists.txt` change was needed.

## Verification

1. **Incremental compile check**: `cmake --build build` — succeeded with zero
   errors (9/9 steps rebuilt: `gte_core`'s `RenderGraphCompiler.cpp` object,
   `libgte_core.a`, `GreatTamanaEditor.exe` + both Project Assembly `.dll`
   pairs, the test object for `RenderGraphCompilerTests.cpp`, and
   `GreatTamanaEngineTests.exe`).
2. **Targeted `ctest` run**: `ctest -C Debug -R "RenderGraphCompilerTest" --output-on-failure`
   — **40/40 tests passed**, 0 failed, including:
   - All 4 new `RenderGraphCompilerTest.PersistentCache*`/
     `TextureWriteNotInPersistentCacheOrFinalOutputsIsStillCulled`/
     `OrdinaryFinalOutputsRootStillWorksWithEmptyPersistentCacheTextures` cases.
   - Every pre-existing `RenderGraphCompilerTest.*` case (36 of them,
     including `BufferOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput`,
     `VolumeTextureOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput`,
     every `RenderPassEvent`/adjacency-list/MRT/duplicate-edge/large-fan-out
     test), confirming zero regression to any pre-existing compiler behavior.
3. No full clean build, no full `ctest` regression pass was performed
   (correctly deferred to PHASE9, per the campaign's own Rule 3.3.3).

## Honestly-flagged open issues

- `input.persistentCacheTextures` is still only ever populated by hand in a
  test today — the real production populator,
  `RenderGraphBuilder::GetOrCreatePersistentTexture()`, does not exist until
  PHASE8. This is expected and correct per this phase's own `.md` (Step 2's
  own "fully testable and fully mergeable RIGHT NOW" framing) — `Compile()`
  has zero opinion on how the vector got populated, so this fix is fully
  correct and fully tested in isolation regardless.
- No other open issues. Every acceptance point this phase's own `.md` file
  lists is satisfied: the one-line `DispatchByKind` change, the comment-block
  update citing this campaign, the four required test cases, and
  confirmation that `RenderGraphBarrierPlanner.cpp` and every other part of
  `Compile()` remain untouched.

## Git

Changes staged and committed together with this report:
- `src/Renderer/RenderGraph/RenderGraphCompiler.cpp`
- `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`
- `task_manager/editor-core-separation-27/PHASE3_COMPLETION_REPORT.md`
