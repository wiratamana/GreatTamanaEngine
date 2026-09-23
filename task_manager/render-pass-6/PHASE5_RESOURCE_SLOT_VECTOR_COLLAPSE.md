# PHASE5 — Collapse 9 Parallel Vectors into 3 Per-Kind Slot Vectors (item 2.1)

⚠️ **This phase gets its own dedicated `delegate_task` double-check pass
immediately after it lands, BEFORE the whole-campaign second-iteration
double-check** — see `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 5.
The dedicated double-check must overwrite THIS file in place if it finds
something worth fixing — never create a new numbered file.

**Recursive `ask_questions` rule**: whoever executes this phase (and anything
it further delegates via `delegate_task`) must use the `ask_questions` tool
whenever it hits a genuine ambiguity or a design choice this document doesn't
already pin down, and must repeat this exact same instruction to anything it
delegates further down the chain.

## Parent

`PHASE0_MASTER_STRATEGY.md` (read it first, in particular Locked Design
Decision 4 — the exact `TextureSlot`/`BufferSlot`/`VolumeTextureSlot` shape is
FIXED, confirmed via `ask_questions`, do not deviate without re-confirming).
Also read `PHASE4_COMPLETION_REPORT.md` — PHASE4's adjacency-list rewrite
lands inside `RenderGraphCompiler.cpp`'s `Compile()` before this phase runs,
and (per `PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md`'s own Step 3.4) it
introduces **three brand-new vectors this phase must also update**:
`firstTextureWriter`/`firstBufferWriter`/`firstVolumeTextureWriter`, each
sized off `input.textureDescs.size()`/`input.bufferDescs.size()`/
`input.volumeTextureDescs.size()` in PHASE4's own illustrative code. Do not
assume PHASE4 landed with exactly that illustrative code verbatim — re-grep
`RenderGraphCompiler.cpp` for every remaining `.size()` call sized off the OLD
9-vector names once PHASE4's real diff is in, using the OLD-name grep as the
actual proof (see Step 3.8), not this document's own guess as to what PHASE4
produced.

## Step 1: The Goal (Where are we going?)

Replace the "9 parallel vectors kept in lockstep purely by convention" data
shape with 3 real struct-of-3-fields vectors, eliminating an entire class of
"forgot to push to array #3" silent-misalignment bug:

```cpp
struct TextureSlot {
    TextureDesc desc;
    const char* name = nullptr;
    TextureImportInfo importInfo;
};
struct BufferSlot {
    BufferDesc desc;
    const char* name = nullptr;
    BufferImportInfo importInfo;
};
struct VolumeTextureSlot {
    VolumeTextureDesc desc;
    const char* name = nullptr;
    VolumeTextureImportInfo importInfo;
};
```

`CompiledGraphInput` and `RenderGraphBuilder` both replace their 9 parallel
vectors (`textureDescs`/`textureNames`/`textureImportInfo`, etc.) with
exactly 3: `std::vector<TextureSlot> textures`, `std::vector<BufferSlot>
buffers`, `std::vector<VolumeTextureSlot> volumeTextures`. Every real call
site that indexes the old parallel arrays (by the SAME index into each of
the three) is rewritten to index one slot vector's fields instead. **Zero
change to `RenderGraphBuilder`'s PUBLIC API** (`CreateTexture()`,
`ImportTexture()`, `PassBuilder::ReadTexture()`, etc. all keep their exact
existing signatures) — this is purely an internal storage-shape change.

## Step 2: The Situation (Where are we now?)

Everything below was confirmed by directly reading the REAL, current source —
`RenderGraphBuilder.cpp` in particular was NOT available when an earlier
draft of this document was written; it has now been read in full and every
placeholder that earlier draft left as "verify against the real file" is
resolved concretely below, with the real function bodies quoted.

**`CompiledGraphInput`** (`src/Renderer/RenderGraph/RenderGraphBuilder.h`,
lines 119-152):
```cpp
struct CompiledGraphInput {
    std::vector<PassRecord> passes;
    std::vector<TextureDesc> textureDescs;
    std::vector<const char*> textureNames;
    std::vector<TextureImportInfo> textureImportInfo;
    std::vector<BufferDesc> bufferDescs;
    std::vector<const char*> bufferNames;
    std::vector<BufferImportInfo> bufferImportInfo;
    std::vector<VolumeTextureDesc> volumeTextureDescs;
    std::vector<const char*> volumeTextureNames;
    std::vector<VolumeTextureImportInfo> volumeTextureImportInfo;
    std::vector<VolumeTextureHandle> finalVolumeTextureOutputs; // UNCHANGED - not a per-kind-3 array.
};
```

**`RenderGraphBuilder`'s private members** (same file, lines 534-552):
identical shape, `m_`-prefixed, plus `m_finalVolumeTextureOutputs`
(unchanged).

**`RenderGraphBuilder.cpp` (now fully read) — every one of the 5
create/import methods, confirmed verbatim, with the exact real behavior the
new code must faithfully reproduce**:

- `CreateTexture(name, desc)`: `index = m_textureDescs.size()`; pushes `desc`
  onto `m_textureDescs`, `name` onto `m_textureNames`, and a
  **default-constructed** `TextureImportInfo{}` (i.e. `isImported == false`)
  onto `m_textureImportInfo`. Returns `TextureHandle{ index, 1 }`
  (generation is always the literal `1` — nothing else mints/recycles a
  `TextureHandle` generation today).
- `CreateBuffer(name, desc)`: identical shape, buffer-flavored
  (`BufferImportInfo{}` default).
- `ImportTexture(name, externalTarget, currentLayout)`: **does NOT leave
  `TextureDesc` default-constructed** — it explicitly MIRRORS the external
  target's own real shape into a freshly-built `TextureDesc` purely for
  informational/debug-display purposes (`RenderGraphSnapshot`'s resource
  list, Phase 8): `desc.width = externalTarget.extent.width`, `desc.height =
  externalTarget.extent.height`, `desc.format = externalTarget.format`,
  `desc.hasDepth = (externalTarget.depthImage != VK_NULL_HANDLE)`. It then
  builds a `TextureImportInfo` with `isImported = true`, `externalTarget`,
  and `currentLayout` set from the caller-supplied arguments. **This mirrored
  desc, never a default `TextureDesc{}`, is the real, current, pre-refactor
  behavior — confirmed directly by reading the function body, and directly
  regression-tested by `RenderGraphBuilderTests.cpp`'s own
  `ImportTextureMirrorsExternalTargetShapeIntoTextureDesc` test (line 720) —
  the rewritten code must preserve this exactly, not simplify it away to a
  default-constructed desc.**
- `ImportBuffer(name, externalBuffer, size)`: same pattern — `BufferDesc desc;
  desc.size = size; desc.usage = 0;` (a live `VkBuffer` cannot expose its own
  creation-time usage flags back — harmless, since an imported entry's desc
  is never read for pool-matching), then a `BufferImportInfo` with
  `isImported = true`, `externalBuffer`, `size`.
- `ImportVolumeTexture(name, externalVolumeTarget, currentLayout)`: same
  pattern — `VolumeTextureDesc desc; desc.width = ...; desc.height = ...;
  desc.depth = ...; desc.format = ...;` mirrored from `externalVolumeTarget`,
  then a `VolumeTextureImportInfo` with `isImported = true`,
  `externalTarget`, `currentLayout`.
- `KeepVolumeTextureOutput(handle)`: unaffected by this phase —
  `m_finalVolumeTextureOutputs.push_back(handle)`, unchanged.
- `Finish()`: moves all 9 (now 3) resource vectors plus `passes` and
  `finalVolumeTextureOutputs` into a fresh `CompiledGraphInput` and returns
  it by value.

**`RenderGraphCompiler.cpp`/`.h` (fully read; PHASE4 will already have
rewritten large parts of `Compile()` by the time this phase runs — see the
Parent section above)**:
- `Compile()`'s sizing calls, TODAY (before PHASE4): `result.textureLifetimes
  .assign(input.textureDescs.size(), ...)` / `.bufferLifetimes...` /
  `.volumeTextureLifetimes...` (lines 163-167), and `lastTextureWriter(
  input.textureDescs.size(), -1)` / `lastBufferWriter(...)` /
  `lastVolumeTextureWriter(...)` (lines 258-267). **After PHASE4 lands, there
  will ALSO be `firstTextureWriter`/`firstBufferWriter`/
  `firstVolumeTextureWriter`, each sized the same way** (PHASE4's own Step
  3.4) — all six of these sizing calls become `input.textures.size()` /
  `input.buffers.size()` / `input.volumeTextures.size()`, matched to the
  correct kind. None of these lines READ `textureDescs`/`textureNames`
  elementwise — they only ever call `.size()` for construction — so this is
  a pure sizing-call rename, not a semantic change.
- The RAW/WAW edge-construction scan and the (post-PHASE4) inline
  contradiction-detection fast path both index `lastXWriter[usage.x.index]`/
  `firstXWriter[usage.x.index]` directly — never `textureDescs`/
  `textureNames`/`textureImportInfo` elementwise — so their BODIES need zero
  change, only the `.size()` calls that size these arrays (covered above).
- The Step 4 lifetime `touch()` lambda indexes `result.textureLifetimes`/etc.
  (the compiler's own OUTPUT vectors, unaffected by this phase) — no change
  needed there.
- `DetectRenderPassEventContradictions()` (the standalone, independently
  tested function — PHASE4 leaves its own body untouched) does NOT touch
  `input.textureDescs`/`textureNames`/etc. at all; it only reads
  `input.passes[...].reads`/`.writes`, comparing `ResourceUsage` handles
  directly — **zero change needed in this function's body**.
- **`RenderGraphCompiler.h`'s own doc comment on `CompiledGraph` (lines
  71-72) reads `// Parallel to CompiledGraphInput::textureDescs/bufferDescs/
  // volumeTextureDescs (same index)` — this is a real hit this phase must
  fix too** (comment-only, but a genuine source-tree hit the Step 3.8 sweep
  will catch if missed): reword to `// Parallel to CompiledGraphInput::
  textures/buffers/volumeTextures (same index)`.

**`RenderGraph.cpp` (fully read)**:
- `EnsureTextureResolved()` (lines 33-87): `input.textureImportInfo[index]` →
  `input.textures[index].importInfo`; `input.textureDescs[index]` →
  `input.textures[index].desc`; `input.textureNames[index]` (passed to
  `m_resourcePool.AcquireTexture(...)`) → `input.textures[index].name`. Same
  pattern for `EnsureBufferResolved()` (lines 89-125) /
  `EnsureVolumeTextureResolved()` (lines 135-154).
- **A call site the earlier draft of this document missed entirely — a real
  gap, not a guess**: `ExecuteCompiledGraph()` sizes its three physical-
  resource vectors directly off the OLD desc-vector names, immediately after
  compiling the graph (today, lines 291-294):
  ```cpp
  std::vector<PhysicalTexture> physicalTextures(input.textureDescs.size());
  std::vector<PhysicalBuffer> physicalBuffers(input.bufferDescs.size());
  std::vector<PhysicalVolumeTexture> physicalVolumeTextures(input.volumeTextureDescs.size());
  ```
  These three lines become `input.textures.size()` / `input.buffers.size()` /
  `input.volumeTextures.size()`. (PHASE2's extraction may relocate these
  three lines into a different method by the time this phase runs — find
  them by this exact "three `std::vector<PhysicalX>` constructions
  immediately after `Compile()` is called" landmark, not by line number.)
- The two debug-registry registration loops (`RegisterDebugTextureSnapshots()`/
  `RegisterDebugVolumeTextureSnapshots()` after PHASE2's extraction; today,
  inline in `ExecuteCompiledGraph()`, lines ~593-648): `input.textureNames[i]`
  → `input.textures[i].name` (and the volume-texture equivalent,
  `input.volumeTextureNames[i]` → `input.volumeTextures[i].name`).

**`RenderGraphSnapshot.cpp` (fully read)**: `ResourceUsageName()` (lines
61-83): `input.textureNames[usage.texture.index]` → `input.textures[
usage.texture.index].name` (and the existing bounds check
`usage.texture.index < input.textureNames.size()` → `usage.texture.index <
input.textures.size()`). Same pattern for the `Buffer`/`VolumeTexture`
branches. `BuildRenderGraphSnapshot()` (lines 147-174): `input.textureDescs
.size()` / `input.bufferDescs.size()` (the `snapshot.resources.reserve(...)`
call and both `for` loop bounds) → `input.textures.size()` /
`input.buffers.size()`; `input.textureNames[i]`/`input.textureImportInfo[i]
.isImported` → `input.textures[i].name`/`input.textures[i].importInfo
.isImported` (and the buffer equivalent).

**Tests — confirmed by directly reading all four files named in the task
prerequisites (not guessed)**:
- **`tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` is the ONLY test
  file in the whole repository that needs any change for this phase.** It
  builds every fixture through a real `RenderGraphBuilder` and then asserts
  directly on `builder.Finish()`'s resulting `CompiledGraphInput`'s
  resource-table fields — the exact spots needing a field-access rewrite,
  confirmed by direct line-by-line reading (line numbers below are 0-based,
  matching this file's current, pre-refactor state):
  - Line 91: `ASSERT_EQ(input.textureDescs.size(), 2u);` → `input.textures
    .size()`.
  - Line 92: `EXPECT_TRUE(input.textureDescs[a.index] == input.textureDescs[
    b.index]);` → `input.textures[a.index].desc == input.textures[b.index]
    .desc` (`TextureDesc::operator==` is unaffected and still applies to the
    `.desc` sub-field).
  - Lines 95-96: `input.textureNames[a.index]`/`[b.index]` → `input.textures[
    a.index].name`/`[b.index].name`.
  - Line 108: `ASSERT_EQ(input.bufferDescs.size(), 2u);` → `input.buffers
    .size()`.
  - Line 109: `input.bufferDescs[a.index] == input.bufferDescs[b.index]` →
    `input.buffers[a.index].desc == input.buffers[b.index].desc`.
  - Lines 110-111: `input.bufferNames[a.index]`/`[b.index]` → `input.buffers[
    a.index].name`/`[b.index].name`.
  - Lines 683-685 (`ImportTextureIsTaggedAsImportedInCompiledGraphInput`):
    `input.textureImportInfo.size()`/`[imported.index].isImported`/
    `[transient.index].isImported` → `input.textures.size()`/`[imported
    .index].importInfo.isImported`/`[transient.index].importInfo.isImported`.
  - Lines 699-701 (`ImportTextureRecordsExactCurrentLayoutSupplied`): same
    pattern, plus `.currentLayout` → `input.textures[imported.index]
    .importInfo.currentLayout`.
  - Line 711 (`ImportTextureStoresExternalTargetVerbatim`): `const
    TextureImportInfo& info = input.textureImportInfo[imported.index];` →
    `input.textures[imported.index].importInfo`.
  - Line 727 (`ImportTextureMirrorsExternalTargetShapeIntoTextureDesc` — the
    exact regression test proving the "mirrored desc, not default" behavior
    documented above): `const TextureDesc& desc = input.textureDescs[
    imported.index];` → `input.textures[imported.index].desc`.
  - Lines 746-748 (`ImportedAndTransientTexturesShareOneContiguousHandleSpace`):
    `input.textureDescs.size()`/`input.textureNames.size()`/`input
    .textureImportInfo.size()` → all become `input.textures.size()` (three
    separate assertions against the SAME new vector's size — still three
    separate `EXPECT_EQ` calls, just all reading the one new vector).
  - Lines 783-785 (`ImportBufferIsTaggedAsImportedInCompiledGraphInput`):
    same pattern as 683-685, buffer-flavored.
  - Line 795 (`ImportBufferStoresExternalBufferAndSizeVerbatim`): `const
    BufferImportInfo& info = input.bufferImportInfo[imported.index];` →
    `input.buffers[imported.index].importInfo`.
  - Line 807 (`ImportBufferMirrorsSizeIntoBufferDesc`): `input.bufferDescs[
    imported.index].size` → `input.buffers[imported.index].desc.size`.
  - Lines 823-825 (`ImportedAndTransientBuffersShareOneContiguousHandleSpace`):
    same pattern as 746-748, buffer-flavored.
  - Every OTHER assertion in this file (e.g. `input.passes[0].reads[...]`,
    `input.passes[0].colorAttachments[...]`, `input.passes[0].kind`,
    `input.passes[0].viewScope`) touches `PassRecord`, not the resource
    tables this phase collapses — **do not touch these; `PassRecord` is
    explicitly out of scope (Step 3.9)**.
- **`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp` needs ZERO
  changes.** Confirmed by reading the whole file: its own header comment
  states the convention explicitly ("Every fixture below is built through a
  real RenderGraphBuilder... rather than hand-poking CompiledGraphInput's
  fields directly"), and a full-file read confirms it — every single test
  builds `input` via `RenderGraphBuilder`/`Compile()` and asserts only on
  `compiled.executionOrder`/`compiled.textureLifetimes`/`input.passes[...]
  .isCulled`/`DetectRenderPassEventContradictions()`'s own return value —
  never on `input.textureDescs`/`textureNames`/etc. directly. (This
  CORRECTS an earlier draft of this document, which guessed this file
  "likely hand-builds CompiledGraphInput instances directly" — confirmed, by
  actually reading the file, that it does not.)
- **`tests/Renderer/RenderGraph/RenderGraphSnapshotTests.cpp` needs ZERO
  changes**, for the identical reason, confirmed by a full-file read — every
  fixture goes through `RenderGraphBuilder`/`Compile()`/
  `BuildRenderGraphSnapshot()`, and every assertion reads
  `RenderGraphSnapshot`/`RenderGraphPassSnapshot`/`RenderGraphResourceSnapshot`
  fields (`snapshot.resources[...]`, `pass.readNames[...]`, etc.), never
  `CompiledGraphInput`'s resource tables directly. (Also corrects the same
  over-cautious guess in the earlier draft.)
- **`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` needs ZERO
  changes, definitively** — confirmed by reading its `#include` list: it
  includes only `Renderer/RenderGraph/RenderGraphTypes.h`, never
  `RenderGraphBuilder.h`, so it is structurally incapable of referencing
  `CompiledGraphInput` at all. Its several `TEST(RenderGraphDescTest,
  ...TextureDescs...)`/`...VolumeTextureDescs...`/`...BufferDescs...` names
  are exercising `TextureDesc`/`BufferDesc`/`VolumeTextureDesc`'s own
  `operator==` in isolation (an unrelated identifier that merely shares a
  substring with the old field names — exactly the "genuinely unrelated hit"
  case Step 3.8's sweep must correctly leave alone).

## Step 3: The Plan (How do we get there?)

### 3.1 — Add the three new slot structs to `RenderGraphBuilder.h`

Place them immediately above `CompiledGraphInput`'s own definition (they are
conceptually part of the same "raw material handed to the compiler"
vocabulary):

```cpp
// render-pass-6 campaign, PHASE5 (item 2.1) - REPLACES the old
// textureDescs/textureNames/textureImportInfo trio (three parallel vectors
// kept in lockstep PURELY BY CONVENTION - "index i always means the same
// resource in all three") with one struct-of-3-fields per declared
// resource. Nothing enforced the old parallelism at compile time - a future
// edit that pushed to one array without the other two would have been a
// silent, very-hard-to-debug misalignment bug, not a compile error. This
// struct makes that bug class structurally impossible: there is only ONE
// vector to push onto, and its three fields can never independently
// desynchronize. Mirrors BufferSlot/VolumeTextureSlot below exactly.
struct TextureSlot {
    TextureDesc desc;
    const char* name = nullptr;
    TextureImportInfo importInfo;
};

struct BufferSlot {
    BufferDesc desc;
    const char* name = nullptr;
    BufferImportInfo importInfo;
};

struct VolumeTextureSlot {
    VolumeTextureDesc desc;
    const char* name = nullptr;
    VolumeTextureImportInfo importInfo;
};
```

(`TextureImportInfo`/`BufferImportInfo`/`VolumeTextureImportInfo` are
already defined earlier in this same file — no new dependency.)

Also reword `TextureImportInfo`/`BufferImportInfo`/`VolumeTextureImportInfo`'s
own existing doc comments (they currently say things like "Parallel to
CompiledGraphInput::textureDescs/textureNames (same index)") to instead say
"the `importInfo` field of the matching `TextureSlot`/`BufferSlot`/
`VolumeTextureSlot` entry" — small, but these comments sit directly above
code a future reader will actually look at, and left stale they describe a
vector layout that no longer exists.

### 3.2 — Update `CompiledGraphInput` and `RenderGraphBuilder`'s private
members

```cpp
struct CompiledGraphInput {
    std::vector<PassRecord> passes;
    std::vector<TextureSlot> textures;
    std::vector<BufferSlot> buffers;
    std::vector<VolumeTextureSlot> volumeTextures;
    std::vector<VolumeTextureHandle> finalVolumeTextureOutputs; // UNCHANGED.
};
```
```cpp
private:
    std::vector<PassRecord> m_passes;
    std::vector<TextureSlot> m_textures;
    std::vector<BufferSlot> m_buffers;
    std::vector<VolumeTextureSlot> m_volumeTextures;
    std::vector<VolumeTextureHandle> m_finalVolumeTextureOutputs; // UNCHANGED.
```

### 3.3 — Update `RenderGraphBuilder.cpp`'s create/import methods

The exact real bodies to transform (confirmed above in Step 2 — this is no
longer illustrative guesswork, it is the real, current behavior):

```cpp
TextureHandle RenderGraphBuilder::CreateTexture(const char* name, const TextureDesc& desc)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::CreateTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_textures.size());
    m_textures.push_back(TextureSlot{ desc, name, TextureImportInfo{} });
    return TextureHandle{ index, 1 };
}

BufferHandle RenderGraphBuilder::CreateBuffer(const char* name, const BufferDesc& desc)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::CreateBuffer requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_buffers.size());
    m_buffers.push_back(BufferSlot{ desc, name, BufferImportInfo{} });
    return BufferHandle{ index, 1 };
}

TextureHandle RenderGraphBuilder::ImportTexture(const char* name, const RenderTarget& externalTarget, VkImageLayout currentLayout)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_textures.size());

    // Mirror the external target's own real shape into a TextureDesc purely
    // for informational/debug-display purposes (Phase 8) - Phase 4 never
    // pool-matches against an imported resource's desc, since an imported
    // resource is never allocated/freed by the graph in the first place.
    // CONFIRMED real, pre-refactor behavior - never a default TextureDesc{}.
    TextureDesc desc;
    desc.width = externalTarget.extent.width;
    desc.height = externalTarget.extent.height;
    desc.format = externalTarget.format;
    desc.hasDepth = externalTarget.depthImage != VK_NULL_HANDLE;

    TextureImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalTarget = externalTarget;
    importInfo.currentLayout = currentLayout;

    m_textures.push_back(TextureSlot{ desc, name, importInfo });
    return TextureHandle{ index, 1 };
}

BufferHandle RenderGraphBuilder::ImportBuffer(const char* name, VkBuffer externalBuffer, VkDeviceSize size)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportBuffer requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_buffers.size());

    BufferDesc desc;
    desc.size = size;
    desc.usage = 0; // A live VkBuffer cannot expose its own creation-time usage flags back - harmless (never read for pool-matching against an imported entry).

    BufferImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalBuffer = externalBuffer;
    importInfo.size = size;

    m_buffers.push_back(BufferSlot{ desc, name, importInfo });
    return BufferHandle{ index, 1 };
}

VolumeTextureHandle RenderGraphBuilder::ImportVolumeTexture(
    const char* name, const VolumeTarget& externalVolumeTarget, VkImageLayout currentLayout)
{
    assert(name != nullptr && name[0] != '\0' &&
        "RenderGraphBuilder::ImportVolumeTexture requires a non-empty, static-storage-duration name");

    const std::uint32_t index = static_cast<std::uint32_t>(m_volumeTextures.size());

    VolumeTextureDesc desc;
    desc.width = externalVolumeTarget.extent.width;
    desc.height = externalVolumeTarget.extent.height;
    desc.depth = externalVolumeTarget.extent.depth;
    desc.format = externalVolumeTarget.format;

    VolumeTextureImportInfo importInfo;
    importInfo.isImported = true;
    importInfo.externalTarget = externalVolumeTarget;
    importInfo.currentLayout = currentLayout;

    m_volumeTextures.push_back(VolumeTextureSlot{ desc, name, importInfo });
    return VolumeTextureHandle{ index, 1 };
}

void RenderGraphBuilder::KeepVolumeTextureOutput(VolumeTextureHandle handle)
{
    m_finalVolumeTextureOutputs.push_back(handle); // Unchanged.
}

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

### 3.4 — Update `RenderGraphCompiler.cpp`/`.h`

Only the sizing calls used to size `lastXWriter`/`firstXWriter` (the latter
introduced by PHASE4 — see the Parent section above)/`result.xLifetimes`
change — every `input.textureDescs.size()` becomes `input.textures.size()`
(and the buffer/volume-texture equivalents). **No other line's BODY in this
file changes** — the RAW/WAW scan, the root-marking scan, the inline
contradiction fast path, and the lifetime `touch()` lambda all index
`lastXWriter[usage.x.index]`/`firstXWriter[usage.x.index]`/
`result.xLifetimes[...]` directly, never `textureDescs`/`textureNames`
elementwise. Since PHASE4 lands first and materially rewrites this
function's internals, do not trust this document's own pre-PHASE4 line
numbers for this file — instead grep the ALREADY-PHASE4-LANDED
`RenderGraphCompiler.cpp` for every remaining `.size()` call reading one of
the 9 old names and rename each one; this is both faster and safer than
trying to map stale line numbers onto a rewritten function body. Also fix
`RenderGraphCompiler.h`'s own doc comment (today at lines 71-72): `//
Parallel to CompiledGraphInput::textureDescs/bufferDescs/
volumeTextureDescs (same index)` → `// Parallel to CompiledGraphInput::
textures/buffers/volumeTextures (same index)`.

### 3.5 — Update `RenderGraph.cpp`

- `EnsureTextureResolved()`/`EnsureBufferResolved()`/
  `EnsureVolumeTextureResolved()`: replace `input.textureImportInfo[index]`
  → `input.textures[index].importInfo`; `input.textureDescs[index]` →
  `input.textures[index].desc`; `input.textureNames[index]` →
  `input.textures[index].name` (and buffer/volume-texture equivalents).
- **`ExecuteCompiledGraph()`'s own physical-resource-vector sizing** (the
  three `std::vector<PhysicalX>` constructions immediately after `Compile()`
  is called — see Step 2 above for why this was missed by an earlier draft):
  `input.textureDescs.size()` → `input.textures.size()`; same for
  `input.bufferDescs.size()`/`input.volumeTextureDescs.size()`.
- `RegisterDebugTextureSnapshots()`/`RegisterDebugVolumeTextureSnapshots()`
  (PHASE2's extracted methods, or the still-inline equivalent if this phase
  somehow runs before PHASE2 lands — it will not, per the master strategy's
  fixed ordering, but the transform is identical either way):
  `input.textureNames[i]` → `input.textures[i].name` (and the
  volume-texture equivalent).

### 3.6 — Update `RenderGraphSnapshot.cpp`

`ResourceUsageName()`: `input.textureNames.size()`/`input.textureNames[
usage.texture.index]` → `input.textures.size()`/`input.textures[
usage.texture.index].name` (and buffer/volume-texture equivalents).
`BuildRenderGraphSnapshot()`: `input.textureDescs.size()`/
`input.bufferDescs.size()` (the `reserve()` call and both `for` loop bounds)
→ `input.textures.size()`/`input.buffers.size()`; `input.textureNames[i]`/
`input.textureImportInfo[i].isImported` → `input.textures[i].name`/
`input.textures[i].importInfo.isImported` (and the buffer equivalent).

### 3.7 — Update the one test file that touches `CompiledGraphInput`'s shape
directly

**Only `RenderGraphBuilderTests.cpp` needs editing** — see Step 2 above for
the exact, confirmed, line-by-line list of every assertion that needs a
field-access rewrite (not a guess: every line was located by directly
reading the file). Rewrite each one from the 9-field-vector access syntax to
the 3-slot-vector access syntax, preserving the exact same expected VALUES
being asserted — only the syntax changes (e.g. `input.textureDescs[0]` →
`input.textures[0].desc`). Do not touch any other assertion in this file
(anything reading `input.passes[...]`'s own fields is unrelated to this
phase's scope).

**`RenderGraphCompilerTests.cpp`, `RenderGraphSnapshotTests.cpp`, and
`RenderGraphTypesTests.cpp` require ZERO changes for this phase** — see Step
2 above for the confirmed reasoning (all three either never touch
`CompiledGraphInput`'s resource tables directly, or cannot include
`RenderGraphBuilder.h` at all). Still run all three test binaries once this
phase's changes compile, purely as a regression check that nothing else in
those files was accidentally disturbed — but expect, and do not manufacture,
zero diffs in those three files.

### 3.8 — Full-repository grep sweep (mandatory, before considering this
phase done)

Grep the WHOLE `src/` and `tests/` trees for `textureDescs`, `textureNames`,
`textureImportInfo`, `bufferDescs`, `bufferNames`, `bufferImportInfo`,
`volumeTextureDescs`, `volumeTextureNames`, `volumeTextureImportInfo` —
every single hit must either be gone (renamed to the new slot-vector shape)
or be a genuinely unrelated identifier that merely shares a substring (check
each hit by hand, don't assume). **This sweep is the actual proof this phase
is complete, not the file list in Step 2** (which — now that
`RenderGraphBuilder.cpp` has actually been read — is believed complete, but
the sweep is what actually proves it, especially against whatever
PHASE1-PHASE4 changed in the meantime). Confirmed by a pre-implementation
sweep of the CURRENT (pre-refactor) tree for this document's own review: the
9 old names appear ONLY inside `src/Renderer/RenderGraph/`
(`RenderGraph.cpp`, `RenderGraphBuilder.cpp`/`.h`, `RenderGraphCompiler.cpp`/
`.h`, `RenderGraphSnapshot.cpp`) and inside
`tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp` — no file under
`src/Application/`, `src/Editor/`, `src/Renderer/Atmosphere/`,
`src/Renderer/Culling/`, or any GPU-skinning/GPU-driven-batching file
touches them at all (every one of those consumers only ever calls
`RenderGraphBuilder`'s PUBLIC API — `CreateTexture()`/`ImportTexture()`/
`PassBuilder::ReadTexture()`/etc. — never reaches into `CompiledGraphInput`'s
resource tables directly). One additional, easy-to-miss hit found during
this review: `Editor/ComputeBlurValidation.h` contains the SUBSTRING
`TextureImportInfo` inside a comment ("TextureImportInfo carries no sampler
of its own") — this is the TYPE name, unaffected by this phase (only the
`textureImportInfo` VECTOR/field name changes), and is exactly the
"genuinely unrelated hit, confirmed by hand" case this step's own
instructions warn about; do not touch it.

### 3.9 — What NOT to do in this phase

- Do NOT change `RenderGraphBuilder`'s PUBLIC method signatures
  (`CreateTexture`, `ImportTexture`, `PassBuilder::ReadTexture`, etc.) — only
  private/internal storage shape changes.
- Do NOT touch `PassRecord`/`ResourceUsage`/`ColorAttachmentDesc` — unrelated
  to this phase's scope (those are pass-metadata types, not the
  desc/name/importInfo resource tables this phase collapses).
- Do NOT implement PHASE6's generic dispatch table yet — this phase only
  changes DATA SHAPE; PHASE6 changes the SWITCH STATEMENTS that consume
  `ResourceUsage.kind`, which is a separate concern that naturally follows
  once this phase's slot vectors exist.

## Definition of Done for this phase

- `TextureSlot`/`BufferSlot`/`VolumeTextureSlot` exist exactly as specified
  in `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 4.
- `CompiledGraphInput`/`RenderGraphBuilder` each expose exactly 3 resource
  vectors (plus `passes`/`finalVolumeTextureOutputs`, unchanged) instead of
  9.
- `ImportTexture()`/`ImportBuffer()`/`ImportVolumeTexture()` still mirror the
  external resource's real shape into their slot's `desc` field exactly as
  before (never regressed to a default-constructed desc) — confirmed by
  `RenderGraphBuilderTests.cpp`'s `ImportTextureMirrorsExternalTargetShapeIntoTextureDesc`
  (and the buffer-side `ImportBufferMirrorsSizeIntoBufferDesc`) still passing
  unmodified in INTENT.
- The full-repository grep sweep (3.8) confirms zero remaining references
  to the old 9 field names anywhere in `src/`/`tests/`, including inside
  comments/doc comments (`RenderGraphBuilder.h`'s `TextureImportInfo`/etc.
  doc comments, `RenderGraphCompiler.h`'s `CompiledGraph` doc comment).
- `RenderGraphBuilder`'s public API is byte-for-byte unchanged — every
  pass-declaration call site anywhere in the engine (Atmosphere, GPU
  Skinning, GPU-driven batching, Present, Frame Debugger Replay, Compute
  Blur Validation, GBuffer Validation) compiles completely unmodified.
- `RenderGraphBuilderTests.cpp` is updated per the exact list in Step 2/3.7,
  with every original assertion's INTENT preserved exactly (same expected
  values, new field-access syntax only); `RenderGraphCompilerTests.cpp`,
  `RenderGraphSnapshotTests.cpp`, and `RenderGraphTypesTests.cpp` show zero
  diff.
- A fast, targeted incremental compile check passes for the whole engine
  target AND the whole test target.
- A targeted run of `RenderGraphBuilderTests.cpp`/`RenderGraphCompilerTests.cpp`/
  `RenderGraphSnapshotTests.cpp` passes (this is a scoped, fast test-binary
  run, not the full-suite `ctest` PHASE0 reserves for PHASE7 alone).
- `PHASE5_COMPLETION_REPORT.md` is written, explicitly listing every file
  touched (including `RenderGraphCompiler.h`'s comment-only fix) and
  confirming the grep sweep found zero stragglers.
- Changes are committed via `git_add`/`git_commit`.

## Dedicated Double-Check Instructions (for the `delegate_task` this phase
spawns immediately after landing)

1. Re-run the full-repository grep sweep (3.8) independently — do not trust
   the completion report's claim; verify it directly.
2. Confirm `RenderGraphBuilder.h`'s public method signatures are
   byte-for-byte identical to before this phase (diff the header's public
   section specifically).
3. Spot-check at least 2-3 call sites outside `src/Renderer/RenderGraph/`
   itself (e.g. one Atmosphere pass declaration, one GPU-skinning pass
   declaration) to confirm they still compile completely unmodified.
4. Re-run the three targeted test binaries
   (`RenderGraphBuilderTests`/`RenderGraphCompilerTests`/
   `RenderGraphSnapshotTests`, or however this repo's `ctest` labels/filters
   expose them) and confirm they pass.
5. Confirm `ImportTexture()`/`ImportBuffer()`/`ImportVolumeTexture()` still
   mirror the external resource's real shape into their slot's `desc` field
   (width/height/format/hasDepth for a texture; size for a buffer;
   width/height/depth/format for a volume texture) — exactly matching the
   pre-refactor source quoted in this document's own Step 2/3.3, never a
   default-constructed desc.
6. Confirm PHASE4's own `firstTextureWriter`/`firstBufferWriter`/
   `firstVolumeTextureWriter` vectors (if PHASE4 landed with that exact
   shape) were also re-sized to the new slot-vector shape — this is the one
   spot most likely to be missed, since it did not exist when most of this
   document was originally drafted.
7. Overwrite this file (`PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md`) in place
   with any correction found — never create a new numbered file.
