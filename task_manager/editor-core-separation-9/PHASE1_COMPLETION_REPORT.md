# editor-core-separation-9 — PHASE1 COMPLETION REPORT

**Phase:** `PHASE1_RESOURCE_VOCABULARY_AND_ABI_FOUNDATION.md`
**Status:** DONE. Zero observable behavior change confirmed — `_v2` untouched,
nothing calls or implements any new type yet.

## What changed

### New files

- `plugins/gte_plugin_abi/PluginRenderResource.h` (NEW) — `PluginTextureHandle`/
  `PluginBufferHandle` (POD index+generation, `IsValid()`, `operator==`),
  `PluginResourceAccess` (curated 4-value enum: `ColorAttachmentWrite`/
  `ShaderRead`/`ComputeShaderRead`/`ComputeShaderWrite`), `PluginTextureDesc`
  (`width`/`height`/nested `Format` enum: `Rgba8Unorm`/`Rgba16Float`/
  `R32Float`), `PluginBufferDesc` (`sizeBytes` only). No `debugName` field on
  either desc struct, per the Design Doc's/`RenderGraphTypes.h`'s "standing
  rule". Includes only `<cstdint>` — confirmed no real `gte_core`/`gte_editor`
  header is reachable from this file.
- `plugins/gte_plugin_abi/IPluginRenderPassBuilder_v3.h` (NEW) — declares, in
  this one file: `IPluginPassSetupContext` (`ReadTexture`/`WriteTexture`/
  `ReadBuffer`/`WriteBuffer`/`WriteColorAttachment`), the two named caps
  `kPluginComputeDispatchMaxGroupsPerDimension = 64` and
  `kPluginMaxOperationParamBytes = 128` (single-definition constants, per
  Locked Design Decision LD2/Locked Architecture Decision #12 — enforcement
  itself is PHASE2's job, this phase only documents the contract),
  `IPluginCommandRecorder` (`BindTexture`/`BindBuffer`/`Dispatch`/
  `DrawFullscreenTriangle`), `PluginBlackboardValueKind`/
  `PluginBlackboardValue`/`IPluginBlackboard` (`Publish`/`Fetch`), and the
  top-level `IPluginRenderPassBuilder_v3` interface itself
  (`CreateTexture`/`CreateBuffer`/`TryGetNamedTexture`/
  `GetPrivateOutputTarget`/`AddGraphicsPass`/`AddComputePass`/`Blackboard`).
  `SetupFn`/`ExecuteFn` are plain function pointers + `void* userData`
  (mirrors `PFN_GTE_CreatePluginModule`), never `std::function`. Includes
  only `<cstddef>`/`<cstdint>`/`PluginRenderResource.h`.
- `src/Core/Plugins/PluginRenderResourceTranslation.h/.cpp` (NEW,
  `gte_core`-internal) — pure, Tier-1-testable `ToRgAccess(PluginResourceAccess)`,
  `ToRgTextureDesc(const PluginTextureDesc&)`, `ToRgBufferDesc(const PluginBufferDesc&)`.
  No live `VkDevice`/`Renderer&` involved anywhere in this file.
- `tests/Core/Plugins/PluginRenderResourceTranslationTests.cpp` (NEW) — 9
  tests, one per `PluginResourceAccess` enumerator (4), one per
  `PluginTextureDesc::Format` enumerator (3) confirming the exact `VkFormat`
  plus `hasDepth == false`, and 2 for `ToRgBufferDesc`'s exact size round-trip
  + fixed usage flags.

### Edited files (additive only)

- `plugins/gte_plugin_abi/IRenderFeatureModule.h` — appended
  `IRenderFeatureModule_v3` (forward-declares `IPluginRenderPassBuilder_v3`,
  never `#include`s it, mirroring the existing `_v2` forward-declare
  pattern) + `kIRenderFeatureModule_v3_Name`. `IRenderFeatureModule_v1`/`_v2`
  byte-for-byte unchanged.
- `CMakeLists.txt` — added
  `src/Core/Plugins/PluginRenderResourceTranslation.h/.cpp` to `gte_core`'s
  source list, immediately after `RenderFeatureCompositor.cpp`.
- `tests/CMakeLists.txt` — added
  `Core/Plugins/PluginRenderResourceTranslationTests.cpp` to the test-source
  list, immediately after `Core/Plugins/PluginHostFailurePathTests.cpp`.

### NOT touched (confirmed by `git_status`, matches this phase's Non-Goals)

- `plugins/gte_plugin_abi/PublicSurface.md` — explicitly PHASE5's job per
  `PHASE0_MASTER_STRATEGY.md` Locked Architecture Decision #8 ("`PublicSurface.md`
  gets a new 'Added by later phases' bullet for this campaign (PHASE5)").
- `RenderFeatureCompositor`/`Core.h`/`RenderFeatureDescriptor.h`/
  `IPluginRenderPassBuilder_v2.h` — all byte-for-byte unchanged.
- No `PluginRenderOperationRegistry`, no `PluginRenderPassBuilderAdapter_v3`,
  no demo plugin — all PHASE2/PHASE3, as planned.

## Exact translation-table values chosen (with precedent citations)

- `PluginResourceAccess::ColorAttachmentWrite/ShaderRead/ComputeShaderRead/ComputeShaderWrite`
  map 1:1 by name to the identically-named `rg::ResourceAccess` enumerators
  (`RenderGraphTypes.h`) — no ambiguity, exhaustive switch, no `default:`.
- `PluginTextureDesc::Format::Rgba8Unorm` → `VK_FORMAT_R8G8B8A8_UNORM` —
  matches `Texture2D.cpp`'s own fixed format and dozens of other call sites
  (`RenderFeatureCompositor.cpp` line 267, `ComputeBlurValidation.cpp` line
  80, `GBufferValidation.cpp` line 23, etc.).
- `PluginTextureDesc::Format::Rgba16Float` → `VK_FORMAT_R16G16B16A16_SFLOAT` —
  matches every existing HDR render-texture call site in this codebase (e.g.
  `AtmosphereLutRenderer.cpp` lines 331/460/603/937 — the Multi-Scattering
  LUT, Sky-View LUT, and Aerial Perspective volume outputs).
- `PluginTextureDesc::Format::R32Float` → `VK_FORMAT_R32_SFLOAT` — **no
  internal-engine precedent exists for this exact format anywhere in
  `src/`** (confirmed via `search_in_dir` — zero matches). This is a
  brand-new-to-this-engine format with no real host consumer yet (the
  Design Doc's own curated 3-value proposal includes it speculatively for a
  future single-channel plugin use case). `VK_FORMAT_R32_SFLOAT` is simply
  the one, unambiguous, canonical Vulkan format for a single 32-bit float
  channel — there is no other reasonable value it could map to, so this is
  a direct, uncontroversial literal choice, not a guess requiring precedent
  matching. Flagged here for visibility, not as an open question.
- `ToRgBufferDesc`: `sizeBytes` → `rg::BufferDesc::size` (exact round-trip,
  no scaling/rounding). `usage` is always the single fixed
  `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` flag — matches
  `src/Game/Animation/GpuSkinningRigCache.cpp`'s own `GpuSkinningBindPose`/
  `GpuSkinningWeights`/`GpuSkinningUv` buffers (lines 89/94/109), which use
  exactly this one flag, unconditionally, for a compute-shader-readable
  storage buffer — the same shape a plugin-created buffer needs to support
  both `ComputeShaderRead` and `ComputeShaderWrite`. `TransferDst`/vertex-
  buffer flags were deliberately NOT added, since `PluginResourceAccess` has
  no upload/vertex-input value to justify them.
- `hasDepth` is unconditionally `false` on every `rg::TextureDesc` this
  function produces — `PluginTextureDesc` carries no such field at all, per
  this phase's own plan (a plugin-created transient texture never carries a
  companion depth buffer this campaign).

## Verification evidence

1. **Isolated ABI compile check** — a throwaway `.cpp`
   (`build/_phase1_abi_isolation_check.cpp`, deleted immediately after) that
   `#include`s `PluginRenderResource.h`, `IPluginRenderPassBuilder_v3.h`, and
   `IRenderFeatureModule.h`, compiled via:
   ```
   g++ -std=c++20 -I "plugins/gte_plugin_abi" -c _phase1_abi_isolation_check.cpp -o _phase1_abi_isolation_check.o
   ```
   with an include path limited to EXACTLY `plugins/gte_plugin_abi/` (no
   generated-headers folder needed — none of the 3 new/edited files require
   `GtePluginAbiFingerprintGenerated.h`). **Compiled with zero errors, zero
   warnings.** Confirms no real `gte_core`/`gte_editor` header is reachable
   from any file this phase added/edited.
2. **Incremental build** (`cmake --build build`) — full success, 22 build
   steps, `gte_core.a`/`gte_editor.a`/`GreatTamanaEditor.exe`/
   `GreatTamanaEngineTests.exe` all relink cleanly. Only pre-existing,
   unrelated `MingwRuntime.cmake` shared-CRT no-op warnings in stderr (this
   machine's toolchain has no shared libstdc++ — a documented, pre-existing,
   unrelated condition, not caused by this phase).
3. **Targeted test run** —
   `tests\GreatTamanaEngineTests.exe --gtest_filter=PluginRenderResourceTranslationTest.*`:
   all 9 new tests pass.
4. **Full test binary run** (fast, not the Phase-5-only `ctest` pass) —
   `tests\GreatTamanaEngineTests.exe` (no filter): **1857 tests from 261
   suites, 1855 PASSED, 2 SKIPPED (both pre-existing, environment-gated:
   `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
   `CoreHeadlessConstructionTest...WithNoEditorLayerHookSet`), 0 FAILED.**
   The only newly-added tests are the 9 `PluginRenderResourceTranslationTest`
   cases — no other suite's pass/fail/skip count changed, confirming zero
   regression.
5. **`git_status` before/after** — before: only `task_manager/editor-core-separation-9/`
   untracked (the campaign's own strategy docs, pre-existing from planning).
   After this phase's work: exactly the files this phase's own plan named —
   3 modified (`CMakeLists.txt`, `plugins/gte_plugin_abi/IRenderFeatureModule.h`,
   `tests/CMakeLists.txt`) + 6 new (the 2 new ABI headers, the 2 new
   translation files, the 1 new test file, plus this same
   `task_manager/editor-core-separation-9/` folder). Nothing outside this
   phase's declared scope was touched.

## Deviations from the plan

None. Every file named in `PHASE1_RESOURCE_VOCABULARY_AND_ABI_FOUNDATION.md`'s
Step 3 was produced exactly as specified; the one genuinely new judgment call
(the `R32Float` → `VK_FORMAT_R32_SFLOAT` mapping having no internal
precedent to cite) is documented above rather than silently glossed over, but
did not require an `ask_questions` round — the mapping is unambiguous (there
is only one Vulkan format this curated enumerator could reasonably mean).

## Next phase

PHASE2 (`PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md`) builds
`PluginRenderOperationRegistry`, migrates the `RenderFeatureOps.comp`/
`RenderFeatureBlend.comp` pipeline ownership, registers `gte.builtin.box_blur`,
and implements the real `PluginRenderPassBuilderAdapter_v3` — the first real
consumer of every type this phase shipped.
