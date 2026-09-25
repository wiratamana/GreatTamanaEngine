# editor-core-separation-9 — PHASE1: Resource Vocabulary & ABI Foundation

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it FIRST, in full. This phase
implements Locked Architecture Decisions #8 and #9 (partial — only the pure
translation helpers; the real adapter is PHASE2).

**Use `ask_questions`** whenever a real design ambiguity comes up that
`PHASE0_MASTER_STRATEGY.md` or this file does not already resolve. If you
delegate any further work, that work must also be told to use
`ask_questions`.

## Step 1: The Goal

Ship the pure, ABI-safe VOCABULARY every later phase builds on — new
`plugins/gte_plugin_abi/` headers PLUS a `gte_core`-internal, pure,
Tier-1-tested translation layer — with **zero observable behavior change**.
Nothing calls or implements any of this yet; `_v2` continues to work
byte-for-byte unmodified. This mirrors every prior campaign's own PHASE1
precedent (e.g. `render-pass-4` PHASE1's `DetectRenderPassEventContradictions()`,
`editor-core-separation-6` PHASE1's `IPluginRenderPassBuilder_v2.h`/
`RenderFeatureDescriptor.h`) — pure data/interface first, wiring later.

## Step 2: The Situation

Read in full before starting: `plugins/gte_plugin_abi/PublicSurface.md`
(the ABI rule this phase must obey to the letter), `plugins/gte_plugin_abi/IPluginRenderPassBuilder_v2.h`
(the exact style/doc-comment density to mirror), `plugins/gte_plugin_abi/RenderFeatureDescriptor.h`
(the exact "fixed-size POD, `char[64]` name, never `std::string`" style),
`src/Renderer/RenderGraph/RenderGraphTypes.h` (the REAL vocabulary this
phase's own types are a curated ABI-safe mirror of — `TextureHandle`/
`BufferHandle`/`ResourceAccess`/`TextureDesc`/`BufferDesc`, see
`PHASE0_MASTER_STRATEGY.md` Step 2.2/2.4 for the exact citations), and
`plugins/gte_plugin_abi/IRenderFeatureModule.h` (where `IRenderFeatureModule_v3`
is appended).

Nothing under `plugins/gte_plugin_abi/` may `#include` any real
`gte_core`/`gte_editor` header — confirmed by `PHASE1_COMPLETION_REPORT.md`'s
own precedent in `editor-core-separation-3`: building this folder's headers
with an include path limited to exactly `plugins/gte_plugin_abi/` plus its
CMake-generated-headers folder must still succeed. Re-run that same style of
isolated compile check for this phase's own new headers (see Verification
below).

## Step 3: The Plan

### 3.1 — `plugins/gte_plugin_abi/PluginRenderResource.h` (NEW file)

Transcribe the Design Doc's §4.1 PSEUDOCODE essentially verbatim (it was
written at header-comment density matching this codebase's own house style
specifically so it could be transcribed directly) — with these two
corrections already locked by `PHASE0_MASTER_STRATEGY.md`:

- `PluginResourceAccess` stays EXACTLY the curated 4-value subset the Design
  Doc proposes (`ColorAttachmentWrite`/`ShaderRead`/`ComputeShaderRead`/
  `ComputeShaderWrite`) — do not add more values speculatively; a future
  campaign adds one only when a real consumer needs it (mirrors this
  engine's own repeated "don't expose a name/value with zero real consumer"
  discipline, e.g. `editor-core-separation-6`'s Locked Design Decision #3).
- `PluginTextureDesc::Format` stays the curated 3-value enum
  (`Rgba8Unorm`/`Rgba16Float`/`R32Float`) proposed in the Design Doc — grown
  additively later only when a real plugin use case needs a new format.

Every field must be a plain built-in type or fixed-size POD, per
`PublicSurface.md`'s rule — confirmed already true of the Design Doc's own
pseudocode; do not add anything beyond it (no `debugName` field on either
desc struct — mirrors `rg::TextureDesc`/`rg::BufferDesc`'s own "standing
rule", `RenderGraphTypes.h` ~line 259, and this exact same discipline
restated in `PHASE0_MASTER_STRATEGY.md` Step 2.1).

### 3.2 — `plugins/gte_plugin_abi/IPluginRenderPassBuilder_v3.h` (NEW file)

Declare, in this ONE file (mirrors `IPluginRenderPassBuilder_v2.h`'s own
"one small file, one cohesive ABI surface" convention):

1. `IPluginPassSetupContext` — `ReadTexture`/`WriteTexture`/`ReadBuffer`/
   `WriteBuffer`/`WriteColorAttachment` (Design Doc §4.2), using
   `PluginTextureHandle`/`PluginBufferHandle`/`PluginResourceAccess` from
   `PluginRenderResource.h`.
2. `IPluginCommandRecorder` — `BindTexture`/`BindBuffer`/`Dispatch`/
   `DrawFullscreenTriangle` (Design Doc §4.4), with `Dispatch`'s
   `groupsX/Y/Z` doc comment explicitly stating the 64×64×1 cap (Locked
   Product Decision #2, `PHASE0_MASTER_STRATEGY.md`) and what happens on a
   cap violation (refused, `false` returned, loud host-side log — the actual
   enforcement is PHASE2's job; this phase only documents the contract).
   `paramBytes`/`paramSize` doc comment states the 128-byte cap (Locked
   Architecture Decision #12).
3. `PluginBlackboardValueKind`/`PluginBlackboardValue`/`IPluginBlackboard`
   (Design Doc §4.5) — a plain tagged struct, never `std::any`/`std::variant`
   crossing the ABI, mirroring `rg::ResourceUsage`'s own documented
   "explicit struct over template-heavy machinery" convention
   (`RenderGraphTypes.h`).
4. The top-level `IPluginRenderPassBuilder_v3` interface (Design Doc §4.6),
   WITH the two corrections locked by `PHASE0_MASTER_STRATEGY.md`:
   - Add `virtual PluginTextureHandle GetPrivateOutputTarget() = 0;` (Locked
     Product Decision #5) — doc comment: "Returns this plugin's own
     already-allocated, per-(plugin, view) private compositing target for
     THIS frame — the SAME target `RenderFeatureCompositor` will later blend
     into the final image via this plugin's own declared `blendMode`. This
     plugin's own pass graph must `WriteColorAttachment()`/`WriteTexture()`
     into this exact handle at least once per frame for its work to be
     visible at all — there is no implicit/auto-detected output."
   - `TryGetNamedTexture(const char* semanticName, PluginTextureHandle& outHandle)`
     doc comment: "Currently recognizes exactly one name: `\"SceneColor\"`
     (the already-composited scene color for this view, read-only, BEFORE
     this plugin's own stage runs). Returns `false` for any other name —
     never a garbage handle. See `PHASE0_MASTER_STRATEGY.md` Locked Product
     Decision #3 for why the catalog is deliberately this small for now."
   - `SetupFn`/`ExecuteFn` stay plain function pointers + `void* userData`
     (never `std::function`) — mirrors `PFN_GTE_CreatePluginModule`
     (`PluginExports.h`), per the Design Doc's own §4.6 closing note.

### 3.3 — `plugins/gte_plugin_abi/IRenderFeatureModule.h` (EDIT — additive only)

Append (never touch `IRenderFeatureModule_v1`/`_v2`):

```cpp
class IPluginRenderPassBuilder_v3;

// editor-core-separation-9 campaign, PHASE1 - ADDITIVE new interface, _v1/_v2
// completely untouched. See PluginRenderResource.h and
// IPluginRenderPassBuilder_v3.h. This is the RECOMMENDED path for new plugin
// authors going forward (PHASE0_MASTER_STRATEGY.md Locked Product Decision #1)
// - _v2 remains fully supported, forever, for backward compatibility.
class IRenderFeatureModule_v3 {
public:
    virtual ~IRenderFeatureModule_v3() = default;
    virtual GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const = 0;
    virtual void AddRenderGraphPasses(IPluginRenderPassBuilder_v3& builder) = 0;
};

inline constexpr const char* kIRenderFeatureModule_v3_Name = "IRenderFeatureModule_v3";
```

Forward-declare `IPluginRenderPassBuilder_v3` in this file (never
`#include "IPluginRenderPassBuilder_v3.h"` here — mirrors how this same file
already forward-declares `IPluginRenderPassBuilder_v2` for `_v2`'s own
identical reason: keep this header light for anything that only needs
`IRenderFeatureModule_v1`).

### 3.4 — `src/Core/Plugins/PluginRenderResourceTranslation.h/.cpp` (NEW, `gte_core`-internal)

Pure, Tier-1-testable functions, no live `VkDevice`/`Renderer&` involved:

```cpp
rg::ResourceAccess ToRgAccess(PluginResourceAccess access) noexcept;
rg::TextureDesc ToRgTextureDesc(const PluginTextureDesc& desc) noexcept;
rg::BufferDesc ToRgBufferDesc(const PluginBufferDesc& desc) noexcept;
```

- `ToRgAccess` is an exhaustive switch, no `default:` case (mirrors
  `IsWriteAccess()`/`ToString(ResourceAccess)`'s own established convention,
  `RenderGraphTypes.h`) — `ColorAttachmentWrite → rg::ResourceAccess::ColorAttachmentWrite`,
  `ShaderRead → rg::ResourceAccess::ShaderRead`,
  `ComputeShaderRead → rg::ResourceAccess::ComputeShaderRead`,
  `ComputeShaderWrite → rg::ResourceAccess::ComputeShaderWrite`.
- `ToRgTextureDesc`: `Rgba8Unorm → VK_FORMAT_R8G8B8A8_UNORM`,
  `Rgba16Float → VK_FORMAT_R16G16B16A16_SFLOAT`, `R32Float → VK_FORMAT_R32_SFLOAT`
  (confirm these exact `VkFormat` values are what `Renderer::CreateRenderTexture()`/
  existing GPU code already uses for equivalent formats elsewhere in this
  codebase before hardcoding them — `search_in_dir` for `VK_FORMAT_R16G16B16A16_SFLOAT`
  to find precedent, e.g. Atmosphere LUT textures). `hasDepth` always `false`
  (a plugin-created transient texture never carries a companion depth
  buffer this campaign — not exposed on `PluginTextureDesc` at all).
- `ToRgBufferDesc`: `sizeBytes → size`; `usage` set to a single, fixed,
  documented flag combination sufficient for both `ComputeShaderRead` and
  `ComputeShaderWrite` (a plugin-created buffer is always
  storage-buffer-capable — confirm the exact `VkBufferUsageFlags` value an
  internal compute-consumer buffer already uses, e.g. GPU Skinning's own
  output buffer creation, and reuse that same combination here rather than
  inventing a new one).

### 3.5 — Tests

`tests/Core/Plugins/PluginRenderResourceTranslationTests.cpp` (new file,
mirrors `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`'s own
plain-data-in/plain-data-out style) — one test per `PluginResourceAccess`
enumerator confirming the exact `rg::ResourceAccess` it maps to, one test per
`PluginTextureDesc::Format` enumerator confirming the exact `VkFormat`, and
one test confirming `ToRgBufferDesc` round-trips `sizeBytes` exactly and
always sets the same fixed usage flags. Add this new test file to
`tests/CMakeLists.txt`'s existing test-source list (mirror how any other
`tests/Core/` test file is already registered there — `search_in_dir` for
an existing `tests/Core/` entry to copy the exact line shape).

## Verification

- A standalone compile check of `plugins/gte_plugin_abi/`'s new headers with
  an include path limited to exactly that folder (plus its CMake-generated
  headers) — mirrors `editor-core-separation-3` PHASE1's own precedent (see
  Step 2 above). If no existing CMake target does this in isolation, a
  simple `gcc`/`g++` tool invocation with `-I plugins/gte_plugin_abi -I
  <build>/plugins/gte_plugin_abi` compiling a throwaway `.cpp` that
  `#include`s both new headers (and nothing else) is sufficient — delete the
  throwaway `.cpp` afterward, it is not committed.
- `cmake --build build` (incremental) — confirm `gte_core`/tests still
  compile with the new translation file + its test file added.
- Build and run `GreatTamanaEngineTests` (or the specific new test binary
  target) — confirm every new `PluginRenderResourceTranslationTests.cpp`
  case passes, and confirm the FULL pre-existing suite's pass COUNT is
  unchanged except for the new cases added (no regression) — this is a fast,
  targeted run, not the full Phase 5 `ctest` pass (Workflow Rule 1).
- `git_status` before and after, confirming only the files this phase's plan
  names were touched (Workflow Rule 9).

## Non-Goals for this phase

- No implementation of `IPluginRenderPassBuilder_v3`/`IPluginCommandRecorder`/
  `IPluginBlackboard` yet (PHASE2/PHASE4).
- No `PluginRenderOperationRegistry` yet (PHASE2).
- No demo plugin yet (PHASE3).
- `RenderFeatureCompositor`/`Core.h` are NOT touched this phase.

## Completion

Write `PHASE1_COMPLETION_REPORT.md` in this folder: exact files
added/edited, the exact translation-table values chosen (formats/usage
flags) with their citation to the existing precedent copied, test results,
and the isolated-ABI-compile-check evidence. `git_add` + `git_commit`
("editor-core-separation-9 PHASE1: resource vocabulary & ABI foundation").
