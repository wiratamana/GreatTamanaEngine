# PHASE3 — `RenderGraphCompiler::Compile()` Root-Marking Fix (the Keep-Alive Guarantee)

Campaign folder: `task_manager/editor-core-separation-27/`

## Parent — MUST READ
`PHASE0_MASTER_STRATEGY.md`. Also re-read `PHASE2_COMPLETION_REPORT.md` —
this phase depends directly on `CompiledGraphInput::persistentCacheTextures`
existing (PHASE2's own addition).

## Step 1: The Goal (Where are we going?)

Close the exact same silent-culling gap `VolumeTextureHandle` and
`BufferHandle` already had before their own fixes shipped
(`finalVolumeTextureOutputs` / `finalBufferOutputs`), for a persistent
cache texture: a pass whose ONLY declared write is a persistent-cache
`TextureHandle`, with nothing else reading or writing it this frame, and
the handle NOT present in that call's own `finalOutputs`, must still
survive `RenderGraphCompiler::Compile()`'s culling pass, every frame — with
ZERO action needed from the pass author beyond having called
`GetOrCreatePersistentTexture()` (PHASE8) at all.

This is, per the source document's own Section 13 framing, one of the two
or three single most safety-critical lines in this whole campaign — a
persistent texture that is silently culled produces NO crash, NO error,
NO log anywhere; it simply never gets written, forever, and the bug is
invisible until someone notices a history buffer never actually updates.

## Step 2: The Situation (Where are we now?)

Confirmed by direct, fresh read of `src/Renderer/RenderGraph/RenderGraphCompiler.cpp`:

- Three small, private, anonymous-namespace helper functions already exist
  at the top of the file (~line 12-54):
  `ContainsTextureHandle(std::span<const TextureHandle>, const TextureHandle&)`,
  `ContainsVolumeTextureHandle(...)`, `ContainsBufferHandle(...)` — all
  three IDENTICAL in shape (a linear scan, `==` comparison, `return true`
  on match). **This phase adds a FOURTH, `ContainsPersistentCacheTextureHandle()`
  is unnecessary — `persistentCacheTextures` is ALSO a
  `std::vector<TextureHandle>`, so the EXISTING `ContainsTextureHandle()`
  helper (which already takes `std::span<const TextureHandle>`) can be
  reused directly, with NO new helper function needed at all.** Confirm
  this by direct inspection before writing any new helper — do not
  duplicate `ContainsTextureHandle()`'s body under a new name.
- `Compile()`'s Step 2 root-marking scan (~line 515-544) — the EXACT
  block to edit:
  ```cpp
  for (std::int32_t i = 0; i < passCount; ++i) {
      const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];
      for (const ResourceUsage& usage : pass.writes) {
          const bool isRoot = DispatchByKind(usage,
              [&](TextureHandle h) { return ContainsTextureHandle(finalOutputs, h); },
              [&](BufferHandle h) { return ContainsBufferHandle(input.finalBufferOutputs, h); },
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
  The `TextureHandle` lambda (`[&](TextureHandle h) { return
  ContainsTextureHandle(finalOutputs, h); }`) is the ONE line this phase
  changes — it must ALSO check `input.persistentCacheTextures`, mirroring
  EXACTLY how the `BufferHandle` lambda already checks BOTH nothing-else
  (buffers have no `finalOutputs`-equivalent at all, only their own opt-in
  vector) — for `TextureHandle` specifically, the check becomes an OR of
  TWO vectors, since ordinary textures already have `finalOutputs` as
  their root set and this campaign ADDS a second, independent way for a
  `TextureHandle` write to qualify as a root.
- This is the ONLY line in `RenderGraphCompiler.cpp` this whole campaign
  ever touches (confirmed: `RenderGraphBarrierPlanner.cpp` needs ZERO
  changes — TR1 — and no other part of `Compile()`, including edge
  construction, topological sort, or cycle detection, is affected).
- **This phase is fully testable and fully mergeable RIGHT NOW, even
  though `GetOrCreatePersistentTexture()` does not exist until PHASE8** —
  `Compile()` only ever READS `input.persistentCacheTextures`; it has
  zero opinion about how that vector got populated. A Tier-1 test can
  hand-fabricate a `CompiledGraphInput` with a manually-`push_back()`'d
  `persistentCacheTextures` entry (exactly like
  `editor-core-separation-26`'s own PHASE1 test for `finalBufferOutputs`
  did) with NO dependency on PHASE8 landing first.

## Step 3: The Plan

1. Re-confirm the exact current line numbers/body of `Compile()`'s Step 2
   scan (they may have drifted from PHASE1/PHASE2's own edits elsewhere in
   the file — though neither phase touches `RenderGraphCompiler.cpp`, so
   this should be unchanged; confirm anyway per PHASE0 Rule 9).
2. Change exactly the `TextureHandle` lambda inside the `DispatchByKind`
   call:
   ```cpp
   [&](TextureHandle h) {
       return ContainsTextureHandle(finalOutputs, h)
           || ContainsTextureHandle(input.persistentCacheTextures, h);
   },
   ```
3. Update the surrounding comment block (the one currently explaining the
   `VolumeTextureHandle`/`BufferHandle` history) to ALSO document this
   third addition — mirroring the existing comment's own style exactly
   (cite this campaign, `editor-core-separation-27`, BIG STEP 3, Section
   5.2).
4. Do not touch anything else in this file.

## Step 4: Required Tests

Tier-1 (pure, no GPU) — add to `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`,
mirroring `editor-core-separation-26` PHASE1's own `finalBufferOutputs`
test shape exactly (a real `RenderGraphBuilder`, one pass with a
write-only usage, no reader, handle NOT in `finalOutputs`):

1. **Keep-alive, basic**: a pass declares
   `pass.WriteTexture(handle)` (or an equivalent write usage) against a
   `TextureHandle` that is present in a hand-fabricated
   `input.persistentCacheTextures` (since `GetOrCreatePersistentTexture()`
   does not exist yet, build the `CompiledGraphInput` via `Finish()` then
   directly `push_back()` onto its own `persistentCacheTextures` field
   before calling `Compile()`, exactly like `editor-core-separation-26`'s
   own PHASE1 test did for `finalBufferOutputs`), with `finalOutputs`
   passed as EMPTY. Confirm: `compiled.executionOrder` contains this pass
   (NOT culled).
2. **Negative control**: the SAME shape, but the handle is NOT pushed onto
   `persistentCacheTextures` and NOT in `finalOutputs` either — confirm
   the pass IS culled (this proves the fix is additive, not accidentally
   permissive for every texture write).
3. **OR-combination**: a texture present in `finalOutputs` (the ordinary
   path) still keeps a pass alive with an EMPTY `persistentCacheTextures`
   — confirms the pre-existing path is untouched.
4. **Transitive keep-alive**: a pass P1 writes a persistent-cache texture
   with no in-frame reader; a SEPARATE, unrelated pass P2 reads/writes
   something else entirely — confirm P1 survives and P2's own
   culled/kept status is unaffected by P1's presence (proves no
   cross-contamination between independent root sets).

## Step 5: Rules recap

See `PHASE0_MASTER_STRATEGY.md`, Section 3.3, in full. This phase adds NO
new file — only edits to `RenderGraphCompiler.cpp` and
`RenderGraphCompilerTests.cpp` (already registered). Incremental build +
`ctest -R RenderGraphCompilerTest`. End with `PHASE3_COMPLETION_REPORT.md`
+ git commit.
