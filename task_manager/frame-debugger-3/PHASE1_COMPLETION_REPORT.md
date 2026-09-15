# PHASE1 — Renderer capture instrumentation — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-3/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md`
Parent: `PHASE0_MASTER_STRATEGY.md`

## Summary

Implemented PHASE1's own "Step 3: The Plan" exactly as written, giving the
engine a real, but completely opt-in and zero-overhead-when-disarmed, way to
observe what `RenderSystem::Draw()`/`Renderer::Submit()` actually did for the
Game View this exact frame — a new `FrameDebuggerCaptureContext` type, a
cosmetic `Pipeline` debug-name parameter, and a pure
`DescribeStandardPipelineState()` free function — with the "Editor-only type
threaded through CORE, always-compiled files" discipline from the phase
document's own Step 3.1b applied everywhere it's needed.

### New files

- `src/Editor/FrameDebuggerCapture.h`/`.cpp` — `GTE_ENABLE_EDITOR`-gated
  (compiled only inside the root `CMakeLists.txt`'s existing
  `if(GTE_ENABLE_EDITOR)` `gte_core` `target_sources()` block, right after
  `FrameDebuggerData.cpp`):
  - `FrameDebuggerCaptureContext` — a plain, dumb, per-frame recorder with
    `RecordDraw(pipelineDebugName, materialTextureDebugName, viewProjection)`
    and `Reset()`, plus read accessors `PipelineDebugNames()`/
    `MaterialTextureDebugNames()` (both deduplicated, first-seen order),
    `DrawCallCount()`, and `LastViewProjection()`. `Reset()` restores the
    exact same empty/default state a freshly-constructed instance already
    starts in.
  - `FrameDebuggerStandardPipelineState` + `DescribeStandardPipelineState()`
    — a pure, parameterless free function transcribing `Pipeline.cpp`'s
    real, hardcoded blend/Z/stencil configuration into display-ready
    strings, cross-checked directly against that file's actual
    `VkPipelineColorBlendAttachmentState`/`VkPipelineDepthStencilStateCreateInfo`/
    `VkPipelineRasterizationStateCreateInfo` construction (`blendMode =
    "Opaque (no blend)"`, `zClip = "On"` — `depthClampEnable` is left at its
    zero-initialized `VK_FALSE` default, so real geometry IS clipped against
    the near/far planes — `zTest = "Less"` — confirmed `VK_COMPARE_OP_LESS`,
    not `VK_COMPARE_OP_LESS_OR_EQUAL`/"LEqual" — `zWrite = "On"`, `cull =
    "None"` — confirmed `VK_CULL_MODE_NONE` — and every `stencil*` field
    `"n/a (no stencil test)"`, since `stencilTestEnable = VK_FALSE` and no
    stencil struct is populated anywhere in this engine today).
- `tests/Editor/FrameDebuggerCaptureTests.cpp` — Tier-1 tests (added to
  `tests/CMakeLists.txt`'s existing `if(GTE_ENABLE_EDITOR)` block, right
  after `Editor/FrameDebuggerDataTests.cpp`) covering every case the phase
  document's own Step 3.5 lists: starts empty; recording the same
  pipeline/texture name twice deduplicates to one entry each while
  `DrawCallCount()` still increments for both calls; two distinct names
  produce two distinct entries in first-seen order; an empty material
  texture name (the untextured-draw case) is never added;
  `LastViewProjection()` tracks the most recent `RecordDraw()` call;
  `Reset()` clears everything back to the empty state; and
  `DescribeStandardPipelineState()`'s returned strings both match the real
  values read directly out of `Pipeline.cpp` and are all non-empty.

### Modified files

- `src/Renderer/Pipeline.h`/`.cpp` — added an optional, trailing `const
  char* debugName = nullptr` constructor parameter, stored as an owned
  `std::string m_debugName` (empty when not supplied), plus a `const
  std::string& DebugName() const noexcept` accessor. Never folded into any
  equality-compared struct — purely cosmetic, exactly mirroring
  `RenderGraphTypes.h`'s own "a resource's human-readable name is threaded
  as its OWN separate parameter" precedent the phase document calls out.
  Move constructor/move-assignment updated to carry `m_debugName` across a
  move.
- `src/Renderer/GpuResourceFactory.h`/`.cpp` and `src/Renderer/Renderer.h`/
  `.cpp` — `CreatePipeline()` grew the same trailing `const char* debugName
  = nullptr` parameter at both layers, forwarded straight through
  unmodified to `Pipeline`'s constructor (`Renderer::CreatePipeline()` ->
  `GpuResourceFactory::CreatePipeline()` -> `Pipeline`'s ctor).
- `src/Game/Instantiation/MeshAssetGpuCatalog.cpp` (`EnsureMeshPipeline()`/
  `EnsureTexturedMeshPipeline()`) and `src/Game/Instantiation/
  PrimitiveGpuCatalog.cpp` (`EnsureDefaultPipeline()`) — the three real
  `Renderer::CreatePipeline()` call sites confirmed by reading the code —
  now supply real, hand-authored debug names: `"Mesh.vert/Mesh.frag
  (PositionNormal)"`, `"TexturedMesh.vert/TexturedMesh.frag
  (PositionNormalUv)"`, and `"Triangle.vert/Triangle.frag (PositionColor)"`
  respectively.
- `src/Game/Instantiation/MaterialTextureGpuCache.cpp` — see "Deviations"
  below; the one real call site that builds every `MaterialTexture` now
  passes a genuinely per-instance debug name instead of the old shared
  literal constant.
- `src/Game/RenderSystem.h` — added an unconditional, bare forward
  declaration `class FrameDebuggerCaptureContext;` at namespace scope (never
  an `#include` of `FrameDebuggerCapture.h`), and a new, defaulted, LAST
  parameter `FrameDebuggerCaptureContext* capture = nullptr` on BOTH
  `Draw()` overloads (the float-aspect overload forwards it straight
  through to the explicit-view-projection overload it's implemented in
  terms of).
- `src/Game/RenderSystem.cpp` — wraps the real `#include
  "../Editor/FrameDebuggerCapture.h"` in `#if GTE_ENABLE_EDITOR`/`#endif`,
  and the actual capture call site is guarded the same way, placed exactly
  where each `DrawCommand`'s `Mesh*`/`Pipeline*`/`MaterialTexture*` are
  already resolved from their own `ResourcePool`s (immediately alongside
  the pre-existing `mesh != nullptr && pipeline != nullptr` check), reading
  `pipeline->DebugName()` and, when a real `MaterialTexture` is bound,
  `renderer.GetMemoryDebugName(materialTexture->texture.Handle())` (see
  "Deviations" below for why this — an existing, already-`GTE_ENABLE_EDITOR`-
  gated `Renderer` accessor — was the chosen mechanism rather than a new
  field on `MaterialTexture` itself). When `capture` is `nullptr` (every
  frame until PHASE3, and every ordinary frame afterward), this whole block
  collapses to the one already-taken pointer-null check — no string
  formatting, no vector work, no `GetMemoryDebugName()` lookup happens.
- `CMakeLists.txt` / `tests/CMakeLists.txt` — new files registered as
  described above.

## Deviations from the phase document

1. **`MaterialTexture`'s "genuinely per-instance debug name" requirement
   (Step 2/3.7) was satisfied by fixing the one real call site
   (`MaterialTextureGpuCache::Resolve()`), not by adding a new field to
   `MaterialTexture` itself.** The phase document explicitly offered both
   options ("either give `MaterialTexture` its own genuinely per-instance
   name, or fix that one call site to pass something identifying ..
   whichever is cheaper"). Investigation confirmed `MaterialTexture::texture`
   (a `Texture2D`) already has a working, `GTE_ENABLE_EDITOR`-gated
   debug-name mechanism end-to-end — `Texture2D`'s own constructor already
   forwards its `debugName` parameter into `GpuMemoryTracker::SetDebugName()`
   (confirmed by reading `Texture2D.cpp`), and `Renderer::GetMemoryDebugName()`
   already exists as a public, `#if GTE_ENABLE_EDITOR`-guarded accessor. The
   only actual bug was `MaterialTextureGpuCache::Resolve()` passing the
   literal constant `"MaterialTexture"` for every single texture instead of
   something derived from its own identity. Changing that one call site to
   pass `"MaterialTexture " + textureGuid.ToString()` (using `Guid`'s
   existing `ToString()`) was strictly cheaper than adding a second,
   parallel per-instance-name field/storage mechanism to `MaterialTexture`
   itself, and `RenderSystem::Draw()`'s new capture call site (already
   `#if GTE_ENABLE_EDITOR`-guarded for unrelated reasons — see Step 3.1b)
   simply calls `renderer.GetMemoryDebugName(materialTexture->texture.Handle())`,
   which is now genuinely distinct per texture asset. This satisfies Locked
   Design Decision #6's "every DISTINCT real bound `MaterialTexture` debug
   name" requirement with a strictly smaller, more surgical change than the
   alternative, and required zero change to `MaterialTexture.h`'s own shape.
2. **`DescribeStandardPipelineState()`'s home** — placed in the new
   `FrameDebuggerCapture.h`/`.cpp` (the phase document's own first-listed
   option), alongside `FrameDebuggerCaptureContext`, rather than inside
   `Pipeline.h` — this keeps every Frame-Debugger-specific concern under one
   new Editor-only file pair, and `Pipeline.h`/`.cpp` gained no new
   `#include` or forward declaration of any Editor type at all (they only
   gained the plain, always-legal `debugName` constructor parameter and
   accessor).

No other deviations. Every other struct/function/call-site change matches
the phase document's own Step 3.1/3.1b/3.2/3.3/3.7 exactly, including the
forward-declare-in-the-header / guard-the-`#include`-and-dereference-in-the-
`.cpp` pattern Step 3.1b specifies.

## Compile check (fast, per this phase's own Step 3.6 — not a full rebuild/
regression)

1. Built `gte_core` in the existing `build` directory (`GTE_ENABLE_EDITOR=ON`,
   the configured default):
   ```
   cmake --build build --target gte_core
   ```
   Result: **succeeded**, no warnings/errors from any new or modified file.

2. Built and ran `GreatTamanaEngineTests`, filtered to the new tests:
   ```
   cmake --build build --target GreatTamanaEngineTests
   GreatTamanaEngineTests.exe --gtest_filter=*FrameDebuggerCapture*
   GreatTamanaEngineTests.exe --gtest_filter=*DescribeStandardPipelineState*
   ```
   Result: **all 7 new tests passed** (6
   `FrameDebuggerCaptureContextTest.*` + 1
   `DescribeStandardPipelineStateTest.ReportsRealHardcodedPipelineCppValues`).
   Also spot-checked `*FrameDebuggerData*` (the pre-existing
   `frame-debugger-2` tests, unaffected — 6/6 still pass) and `*RenderSystem*`
   (the pre-existing `RenderSystemTest` suite, unaffected by the new
   defaulted `capture` parameter — 11/11 still pass).

3. **Scoped `GTE_ENABLE_EDITOR=OFF` configure+build of `gte_core` ALONE**, in
   a fresh `build-editor-off` directory (per this phase's own new Step 3.1b/
   3.6 requirement):
   ```
   cmake -S . -B build-editor-off -G Ninja -DGTE_ENABLE_EDITOR=OFF
   cmake --build build-editor-off --target gte_core
   ```
   Result: **succeeded — compiles AND LINKS cleanly.** This confirms the
   forward-declaration-only header (`RenderSystem.h`'s bare `class
   FrameDebuggerCaptureContext;`) plus the `#if GTE_ENABLE_EDITOR`-guarded
   real `#include`/dereference in `RenderSystem.cpp` correctly avoids the
   "compiles but fails to link" trap the phase document specifically warns
   about — `FrameDebuggerCapture.cpp` (which defines
   `FrameDebuggerCaptureContext::RecordDraw()`) is never compiled into this
   configuration at all, and no unconditional call site ever references it.
   Also built the full `GreatTamanaEngine` executable target in this same
   `GTE_ENABLE_EDITOR=OFF` tree as an extra sanity check — succeeded, linked
   cleanly.

4. As an additional sanity check (not required by this phase, but cheap and
   directly relevant since core files changed), rebuilt the full
   `GreatTamanaEngine` executable target in the original, `GTE_ENABLE_EDITOR=ON`
   `build` directory too — succeeded, linked cleanly.

No full clean build and no full `ctest` regression suite were run in this
phase — per both this phase's own Step 3.6 and PHASE0's Step 3.6/"Order of
work", that is reserved for the final PHASE8 step only.

## Next step

PHASE2 (`PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md`) — a new, pure,
Tier-1-tested real snapshot builder reshaping `gte::rg::RenderGraphSnapshot`
+ this phase's `FrameDebuggerCaptureContext` into a real, non-empty
`FrameDebuggerSnapshot`, filtered to ONLY the Game View's own passes.
