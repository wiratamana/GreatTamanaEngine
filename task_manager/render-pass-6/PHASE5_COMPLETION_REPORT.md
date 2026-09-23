# PHASE5 — Completion Report: Resource Slot Vector Collapse (item 2.1)

## Parent

`PHASE0_MASTER_STRATEGY.md` / `PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md`.
`PHASE4_COMPLETION_REPORT.md` was read first, per that document's own
instructions, along with a fresh re-read of `PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`
itself (in case its own dedicated double-check had overwritten it with a
correction — it had not; the file's content matched the completion report's
own description exactly, including the real, landed `firstTextureWriter`/
`firstBufferWriter`/`firstVolumeTextureWriter` vectors PHASE4 introduced,
sized off `input.textureDescs.size()`/`input.bufferDescs.size()`/
`input.volumeTextureDescs.size()` at the time this phase started).

Per the task's explicit instruction, the REAL, current
`src/Renderer/RenderGraph/RenderGraphBuilder.cpp` (and every other real
consumer file) was read in full before writing any code — several exact
line numbers/call-site shapes the strategy document had already resolved
(having itself been corrected once to read the real file) were re-verified
against the actual, current source rather than trusted blindly, and one
additional real call site PHASE5's own document explicitly flagged as
previously missed (`RenderGraph::ExecuteCompiledGraph()`'s three
`PhysicalX` vector sizing lines) was confirmed present and fixed.

## What was done

Implemented `PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md` exactly as specified,
including the exact `TextureSlot`/`BufferSlot`/`VolumeTextureSlot` shape
fixed by `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 4. No
ambiguity was hit that the strategy document did not already resolve, so
`ask_questions` was not needed during implementation.

### 1. `RenderGraphBuilder.h` — new slot structs, collapsed storage

- Added `TextureSlot { TextureDesc desc; const char* name = nullptr;
  TextureImportInfo importInfo; }`, and the `BufferSlot`/`VolumeTextureSlot`
  mirrors, immediately above `CompiledGraphInput`.
- `CompiledGraphInput` now has exactly 3 resource vectors (`textures`,
  `buffers`, `volumeTextures`) instead of 9, plus the unchanged `passes`/
  `finalVolumeTextureOutputs`.
- `RenderGraphBuilder`'s private members collapsed the same way
  (`m_textures`/`m_buffers`/`m_volumeTextures` replacing the old 9
  `m_`-prefixed vectors).
- Reworded every doc comment on `TextureImportInfo`/`BufferImportInfo`/
  `VolumeTextureImportInfo` and `CompiledGraphInput` itself that used to say
  "Parallel to CompiledGraphInput::textureDescs/textureNames (same index)"
  — now describes "the `importInfo` field of the matching TextureSlot/etc.
  entry" instead, since that vector layout no longer exists.

### 2. `RenderGraphBuilder.cpp` — every Create/Import method rewritten

`CreateTexture()`/`CreateBuffer()`/`ImportTexture()`/`ImportBuffer()`/
`ImportVolumeTexture()`/`Finish()` all now push/move exactly one slot vector
instead of three separate ones kept in lockstep by convention.
**`ImportTexture()`/`ImportBuffer()`/`ImportVolumeTexture()`'s real,
pre-refactor behavior is preserved exactly** — the external target's real
shape (width/height/format/hasDepth for a texture; size for a buffer;
width/height/depth/format for a volume texture) is still mirrored into the
slot's own `desc` field verbatim, never left at a default-constructed
`TextureDesc{}`/`BufferDesc{}`/`VolumeTextureDesc{}` — confirmed both by
direct code inspection and by the unmodified-in-intent
`ImportTextureMirrorsExternalTargetShapeIntoTextureDesc`/
`ImportBufferMirrorsSizeIntoBufferDesc` tests still passing.

### 3. `RenderGraphCompiler.h`/`.cpp` — sizing-call rename only

Per the strategy document's own explicit note ("PHASE4 lands first and
materially rewrites this function's internals... grep the ALREADY-PHASE4-
LANDED `RenderGraphCompiler.cpp` for every remaining `.size()` call reading
one of the 9 old names"), a direct grep of the real, PHASE4-landed file
found **exactly 9** such sizing calls, all fixed:

- `result.textureLifetimes.assign(...)` / `result.bufferLifetimes.assign(...)` /
  `result.volumeTextureLifetimes.assign(...)` (3 calls).
- PHASE4's own `firstTextureWriter`/`firstBufferWriter`/
  `firstVolumeTextureWriter` sizing (3 calls) — confirmed these were
  genuinely landed with the illustrative shape PHASE5's own document
  predicted, and re-sized correctly.
- `lastTextureWriter`/`lastBufferWriter`/`lastVolumeTextureWriter` sizing
  (3 calls).

No other line in this file needed a change — every RAW/WAW edge-scan,
in-degree computation, contradiction fast-path, and the Step 4 lifetime
`touch()` lambda all index `lastXWriter[usage.x.index]`/
`firstXWriter[usage.x.index]`/`result.xLifetimes[...]` directly, never the
old parallel vectors elementwise, exactly as the strategy document
predicted. `RenderGraphCompiler.h`'s own `CompiledGraph` doc comment
("Parallel to CompiledGraphInput::textureDescs/bufferDescs/
volumeTextureDescs") was reworded to "textures/buffers/volumeTextures".

`DetectRenderPassEventContradictions()` — the standalone, independently
tested function PHASE4 left byte-for-byte unchanged — was confirmed to
touch none of the 9 old field names at all (it only ever reads
`input.passes[...].reads/.writes`), so it needed, and received, zero
changes.

### 4. `RenderGraph.cpp` — every real desc/name/importInfo read site

- `EnsureTextureResolved()`/`EnsureBufferResolved()`/
  `EnsureVolumeTextureResolved()`: `input.textureImportInfo[index]` →
  `input.textures[index].importInfo` (and buffer/volume-texture
  equivalents); `input.textureDescs[index]`/`input.textureNames[index]` →
  `input.textures[index].desc`/`.name` (and the buffer equivalent for
  `AcquireBuffer()`).
- `RegisterDebugTextureSnapshots()`/`RegisterDebugVolumeTextureSnapshots()`:
  `input.textureNames[i]`/`input.volumeTextureNames[i]` →
  `input.textures[i].name`/`input.volumeTextures[i].name` (plus a
  descriptive comment above the volume-texture registration loop that
  named the old field, reworded to match).
- **`ExecuteCompiledGraph()`'s own three `std::vector<PhysicalX>` sizing
  lines** — the exact call site this phase's own strategy document flagged
  as missed by an earlier draft — confirmed present, immediately after the
  `Compile()` call, and fixed: `input.textureDescs.size()` /
  `input.bufferDescs.size()` / `input.volumeTextureDescs.size()` →
  `input.textures.size()` / `input.buffers.size()` / `input.volumeTextures.size()`.

### 5. `RenderGraphSnapshot.cpp` — name resolution and resource-table walk

- `ResourceUsageName()`: all three `switch (usage.kind)` branches rewritten
  from `input.textureNames[...]`/etc. to `input.textures[...].name`/etc.,
  bounds checks updated from `.size()` on the old name vector to `.size()`
  on the new slot vector. Doc comment above the function reworded from
  "CompiledGraphInput::textureNames/bufferNames/volumeTextureNames" to
  "CompiledGraphInput::textures/buffers/volumeTextures' own `name` field".
- `BuildRenderGraphSnapshot()`: the `reserve()` call and both resource-table
  `for` loops now iterate `input.textures.size()`/`input.buffers.size()`
  instead of `input.textureDescs.size()`/`input.bufferDescs.size()`; each
  loop body's `resource.name`/`resource.isImported` now read
  `input.textures[i].name`/`.importInfo.isImported` (and the buffer
  equivalent) directly off the slot, rather than three separate bounds-
  checked old-vector reads — the bounds check is no longer needed at all
  since a `TextureSlot`/`BufferSlot` entry structurally always has all
  three fields together (this is exactly the bug class PHASE5 exists to
  make impossible).

### 6. Test file — `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp`

**Confirmed, by direct re-reading, to be the ONLY test file in the whole
repository needing a change** — matching the strategy document's own
finding. Every assertion listed in the strategy document's Step 2/3.7 was
located and rewritten from the 9-field-vector access syntax to the 3-slot-
vector access syntax, preserving the exact same expected values:

- `DifferentNamesSameDescStillCompareEqualDescs` /
  `CreateBufferDifferentNamesSameDescStillCompareEqualDescs`: `.textureDescs`/
  `.bufferDescs` size + equality checks, `.textureNames`/`.bufferNames`
  element checks → `.textures[i].desc`/`.buffers[i].desc` and
  `.textures[i].name`/`.buffers[i].name`.
- `ImportTextureIsTaggedAsImportedInCompiledGraphInput` /
  `ImportBufferIsTaggedAsImportedInCompiledGraphInput`: `.textureImportInfo`/
  `.bufferImportInfo` size + `.isImported` checks → `.textures[i].importInfo
  .isImported`/`.buffers[i].importInfo.isImported`.
- `ImportTextureRecordsExactCurrentLayoutSupplied`: same pattern plus
  `.currentLayout`.
- `ImportTextureStoresExternalTargetVerbatim` /
  `ImportBufferStoresExternalBufferAndSizeVerbatim`: `const
  TextureImportInfo&`/`const BufferImportInfo&` local now bound to
  `input.textures[...].importInfo`/`input.buffers[...].importInfo`.
- `ImportTextureMirrorsExternalTargetShapeIntoTextureDesc` (the exact
  regression test proving the "mirrored desc, not default" behavior) /
  `ImportBufferMirrorsSizeIntoBufferDesc`: `input.textureDescs[...]`/
  `input.bufferDescs[...].size` → `input.textures[...].desc`/
  `input.buffers[...].desc.size`.
- `ImportedAndTransientTexturesShareOneContiguousHandleSpace` /
  `ImportedAndTransientBuffersShareOneContiguousHandleSpace`: the three
  separate `.textureDescs.size()`/`.textureNames.size()`/
  `.textureImportInfo.size()` (and buffer equivalents) assertions all now
  read `.textures.size()`/`.buffers.size()` — three separate `EXPECT_EQ`
  calls kept, all against the one new vector, per the strategy document's
  own explicit instruction.

`RenderGraphCompilerTests.cpp`, `RenderGraphSnapshotTests.cpp`, and
`RenderGraphTypesTests.cpp` were all re-confirmed (by direct reading, not
assumption) to touch none of `CompiledGraphInput`'s resource tables
directly — **zero diff in any of the three**, exactly as predicted. All
three were still re-run as part of this phase's targeted test pass (see
Verification below) as a pure regression check.

## Grep sweep (Step 3.8 — mandatory, performed personally)

Ran a full, repository-wide, regex sweep of `src/` and `tests/` for every
one of the 9 old field names (`textureDescs`, `textureNames`,
`textureImportInfo`, `bufferDescs`, `bufferNames`, `bufferImportInfo`,
`volumeTextureDescs`, `volumeTextureNames`, `volumeTextureImportInfo`) after
finishing every edit above. **Zero remaining hits represent an actual,
unmigrated field access anywhere.** Every surviving hit was checked by hand
and is one of exactly two accepted categories:

1. **Explanatory "old name" references inside a comment describing what was
   just replaced** — e.g. `RenderGraphBuilder.h`'s own new comment
   ("REPLACES the old \"textureDescs/textureNames/textureImportInfo
   trio\"...") and `RenderGraphCompiler.h`'s reworded `CompiledGraph` doc
   comment ("reworded from the old textureDescs/bufferDescs/
   volumeTextureDescs field names, which no longer exist") — mirroring
   PHASE4's own accepted precedent for its `edgeExists` comment.
2. **Genuinely unrelated identifiers that merely share a substring** —
   `RenderGraphTypesTests.cpp`'s `TEST(RenderGraphDescTest,
   ...TextureDescsCompareEqual...)` names (exercising `TextureDesc`/
   `BufferDesc`/`VolumeTextureDesc`'s own `operator==` in isolation — this
   file structurally cannot even include `RenderGraphBuilder.h`),
   `RenderGraphSnapshotTests.cpp`'s `...IncludingVolumeTextureNames` test
   name, `FrameDebuggerSnapshotBuilderTests.cpp`'s unrelated
   `...TextureNamesProduceDistinctEntries...` test name, and
   `ComputeBlurValidation.h`'s comment mentioning the **type** name
   `TextureImportInfo` (unaffected — only the old **field**/vector names
   changed) — confirmed by hand, exactly the case this step's own
   instructions warned about.

A second, separate sweep for any remaining **field-access** use of
`.textureImportInfo`/`.bufferImportInfo`/`.volumeTextureImportInfo` (as
opposed to the still-valid `TextureImportInfo`/`BufferImportInfo`/
`VolumeTextureImportInfo` **type** names) returned **zero matches anywhere**
in `src/` or `tests/`.

## Files touched

- `src/Renderer/RenderGraph/RenderGraphBuilder.h` — new `TextureSlot`/
  `BufferSlot`/`VolumeTextureSlot` structs; `CompiledGraphInput`/
  `RenderGraphBuilder`'s private members collapsed from 9 vectors to 3;
  doc-comment rewording.
- `src/Renderer/RenderGraph/RenderGraphBuilder.cpp` — every Create*/Import*/
  `Finish()` method rewritten against the new slot vectors.
- `src/Renderer/RenderGraph/RenderGraphCompiler.h` — `CompiledGraph`'s doc
  comment reworded (comment-only change).
- `src/Renderer/RenderGraph/RenderGraphCompiler.cpp` — 9 sizing-call renames
  (`result.xLifetimes.assign(...)`, PHASE4's `firstXWriter`, and
  `lastXWriter`), zero other line changed.
- `src/Renderer/RenderGraph/RenderGraph.cpp` — `EnsureTextureResolved()`/
  `EnsureBufferResolved()`/`EnsureVolumeTextureResolved()`,
  `RegisterDebugTextureSnapshots()`/`RegisterDebugVolumeTextureSnapshots()`,
  and `ExecuteCompiledGraph()`'s own 3 `PhysicalX` sizing lines, all
  rewritten against the new slot vectors; one comment reworded.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.cpp` — `ResourceUsageName()`
  and `BuildRenderGraphSnapshot()`'s resource-table loops rewritten against
  the new slot vectors; doc comment reworded.
- `tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` — every
  assertion touching `CompiledGraphInput`'s resource tables rewritten to the
  new slot-vector access syntax, same expected values preserved exactly.

**Confirmed zero changes in**: `RenderGraphCompilerTests.cpp`,
`RenderGraphSnapshotTests.cpp`, `RenderGraphTypesTests.cpp` (re-verified by
diff-free re-run, not just by trusting the strategy document's own
prediction).

## Verification

- **Fast, targeted incremental compile check, whole engine + whole test
  target** (per this phase's own rules — no full `ctest` regression run):
  - `cmake --build build --target gte_core` — succeeds, 0 errors/warnings.
  - `cmake --build build` (default target, builds `GreatTamanaEngine.exe`
    AND `GreatTamanaEngineTests.exe`) — succeeds, 0 errors/warnings.
- **Targeted test run** (this phase's own hard acceptance gate):
  - `tests\GreatTamanaEngineTests.exe --gtest_filter=RenderGraphBuilder*:RenderGraphCompiler*:RenderGraphSnapshot*`
    — **106/106 tests pass** (40 `RenderGraphBuilderTest` + 8
    `RenderGraphBuilderDeathTest` + 33 `RenderGraphCompilerTest` + 1
    `RenderGraphCompilerDeathTest` + 24 `RenderGraphSnapshotTest`), zero
    regressions, zero loosened/deleted assertions.
- **Live sanity check** (`run_app_background` + `gte_send_request`, no
  `std::cout`/raw `fprintf` used anywhere):
  - `GET /get_logs?min_level=Warning` → `{"count":0,...}` both before and
    after the interactions below — zero warnings/errors of any kind.
  - `GET /activate_tab?name=Render Graph` + `GET /get_swapchain` — the
    Editor renders correctly (sky/atmosphere gradient visible in both Scene
    and Game panels), and the "Render Graph" panel shows every real pass
    (Atmosphere Transmittance/Multi-Scattering/SkyView/Aerial-Perspective
    LUTs ×2 views, RenderOpaque, etc.) with correct real GPU timing and
    Reads/Writes columns, in the correct order — confirming the rewritten
    `CompiledGraphInput`/`RenderGraphBuilder` storage shape produces
    byte-identical real production behavior to before this phase.
  - `stop_app_background` — clean shutdown.

## What was NOT touched

- `RenderGraphBuilder`'s PUBLIC method signatures (`CreateTexture()`,
  `CreateBuffer()`, `ImportTexture()`, `ImportBuffer()`,
  `ImportVolumeTexture()`, `KeepVolumeTextureOutput()`, `AddPass()`/
  `AddComputePass()`/`AddRenderPass()`, `PassBuilder::ReadTexture()`/
  `WriteColorAttachment()`/etc., `Finish()`) — every one is byte-for-byte
  unchanged; confirmed by the full engine build succeeding unmodified
  against every real call site in `Application/`, `Renderer/Atmosphere/`,
  `Renderer/Culling/`, and every `Editor/` debug-pass consumer
  (`ComputeBlurValidation.cpp`, `GBufferValidation.cpp`, Atmosphere LUT
  passes, GPU-Skinning, GPU-driven batching, Frame Debugger Replay,
  Present) with zero source changes required at any of them.
- `PassRecord`/`ResourceUsage`/`ColorAttachmentDesc` — unrelated to this
  phase's scope, untouched.
- PHASE6's generic `ResourceKind` dispatch table — explicitly deferred;
  every `switch (usage.kind)` this phase's own changes touch (the sizing
  calls only) remains exactly the same exhaustive, `default:`-less
  three-way switch shape as before.
- `DetectRenderPassEventContradictions()` — zero changes (it never touched
  the old field names to begin with).
- `Compile()`'s determinism contract / `executionOrder` output shape —
  unaffected; confirmed by all 33 pre-existing + 1 new
  `RenderGraphCompilerTest`/`RenderGraphCompilerDeathTest` cases still
  passing unmodified.

## Next phase

Per this phase's own mandatory next step (`PHASE0_MASTER_STRATEGY.md`'s
Locked Design Decision 5), a dedicated `delegate_task` double-check pass for
THIS phase (PHASE5) is being spawned immediately, using
`PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md`'s own "Dedicated Double-Check
Instructions" section, before `PHASE6_RESOURCEKIND_DISPATCH_TABLE.md`
begins.
