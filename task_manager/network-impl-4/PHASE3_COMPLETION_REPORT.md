# PHASE3 — Completion Report (`network-impl-4`)

Implements `PHASE3_GENERIC_IMAGE_READBACK_AND_DEPTH_VISUALIZATION.md` in full
(the revised, 4-call-site version) — `Renderer::CaptureRenderTexturePixels()`
is refactored into a shared, PUBLIC `Renderer::CaptureImagePixels()` primitive
with zero behavior change for its existing caller, `Renderer::WaitForGpuIdle()`
is added, a new `gte::Encoding::ConvertDepthToGrayscaleRgba8()` utility is
added under `src/Encoding/`, and all FOUR graph-external manual-finalize call
sites (`"GameView"`/`"SceneView"`/`"Swapchain"`/`"BlurredSceneOutput"`) are
wired to `RenderGraph::NotifyDebugTextureStateOverride()`. This is a real,
buildable code change, not a plan/description.

## 1. Pre-implementation re-verification (as required by the task brief)

Before editing anything, the real, live shape of everything this phase
touches was re-read directly from the source tree:

- `src/Renderer/Renderer.cpp`'s real `CaptureRenderTexturePixels()`
  implementation was re-read byte-for-byte and confirmed to match the phase
  document's own quoted listing exactly: throwaway `GpuToCpu` readback buffer
  named `"CaptureReadback"`, `ImmediateSubmit()` with the
  ShaderRead→TransferSrc→(copy)→(buffer barrier)→TransferSrc→ShaderRead
  barrier sequence, then a `memcpy` into `CapturedRawPixels`. No drift found.
- `src/Renderer/Renderer.h` — confirmed the `CaptureRenderTexturePixels()`/
  `CapturedRawPixels` section is entirely PUBLIC (the class's one `private:`
  label is much later in the file) and that it only forward-declares
  `gte::rg::RenderGraph`/`RenderGraphBuilder`, NOT `ResourceState` (declared
  in `RenderGraphBarrierPlanner.h`, not previously included).
- `src/Application/Application.cpp` — confirmed the two
  `FinalizeRenderTextureForExternalSampling()` call sites (`"GameView"`/
  `"SceneView"`) and the `FinalizeBlurValidationForSampling()` call site are
  at the exact relative positions the phase document describes, and that
  `RenderGraphBarrierPlanner.h` was genuinely not yet included.
- `src/Renderer/FramePresenter.cpp`'s `PresentViaRenderGraph()` — confirmed
  the exact live `"Swapchain"` manual `PRESENT_SRC_KHR` barrier block
  (`previous`/`next`/`range`/`EmitImageBarrier` call), confirmed `graph` is
  already a named parameter of this function, and confirmed `RenderGraph.h`
  (where `NotifyDebugTextureStateOverride()` lives) was already included.
- `src/Editor/ComputeBlurValidation.cpp`'s `FinalizeForSampling()` — confirmed
  its exact `previous`/`next` `ResourceState` values and its
  `m_writtenThisFrame`-guarded, no-`RenderGraph&`-parameter shape, confirming
  the phase document's own reasoning for why its own correction call must be
  made unconditionally from `Application.cpp` instead.
- `tests/CMakeLists.txt`/root `CMakeLists.txt` — confirmed the exact
  `Encoding/PixelConversionTests.cpp` / `src/Encoding/PixelConversion.cpp`
  list entries to extend.

No deviation from the phase document's own (already twice-audited) findings
was discovered — every "confirmed live" claim held up exactly as written.

## 2. Files edited/created

- `src/Renderer/Renderer.h`:
  - Added `#include "RenderGraph/RenderGraphBarrierPlanner.h"` (for
    `gte::rg::ResourceState`).
  - Added `Renderer::CaptureImagePixels(VkImage, VkImageAspectFlags, VkFormat,
    VkExtent2D, const rg::ResourceState&, int bytesPerPixel = 4)` — PUBLIC,
    declared immediately after `CaptureRenderTexturePixels()`.
  - Added `Renderer::WaitForGpuIdle() const` — PUBLIC, right after
    `CaptureImagePixels()`.
  - `CaptureRenderTexturePixels()`'s own doc comment gained one line noting
    it's now implemented in terms of `CaptureImagePixels()` — its own
    signature is completely unchanged.
- `src/Renderer/Renderer.cpp`:
  - Added `Renderer::CaptureImagePixels()` — the generalized primitive,
    parameterizing image/aspect/format/extent/previousState/bytesPerPixel
    (asserted `== 4`), otherwise identical barrier/copy/readback sequence to
    the original `CaptureRenderTexturePixels()` body.
  - `Renderer::CaptureRenderTexturePixels(RenderTexture&)` now just resolves
    the `ShaderRead` `ResourceState` and forwards to `CaptureImagePixels()`
    with `VK_IMAGE_ASPECT_COLOR_BIT` — same public signature, same return
    value shape, same barrier sequence under the hood.
  - Added `Renderer::WaitForGpuIdle()` — a one-line `vkDeviceWaitIdle(m_device.Native())`
    wrapper.
- `src/Encoding/DepthVisualization.h`/`.cpp` (new files) —
  `gte::Encoding::ConvertDepthToGrayscaleRgba8()`, handling exactly the three
  concrete depth formats `VulkanDevice::PickDepthFormat()` can return
  (`VK_FORMAT_D32_SFLOAT`/`VK_FORMAT_D32_SFLOAT_S8_UINT`/
  `VK_FORMAT_D24_UNORM_S8_UINT`), returning `false` (untouched output) for any
  other format or a null pointer with positive dimensions, and `true`
  (no-op) for a 0-sized buffer — verbatim per the phase document's Step 3.3.
- `CMakeLists.txt` (repo root) — added
  `src/Encoding/DepthVisualization.h`/`.cpp` to the existing `src/Encoding/*`
  source list, right after `PngEncoder.h`/`.cpp`.
- `src/Application/Application.cpp`:
  - Added `#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"`
    alongside the existing `RenderGraphBuilder.h` include.
  - Added `m_renderGraph.NotifyDebugTextureStateOverride("GameView", ...)`/
    `("SceneView", ...)` immediately after their respective
    `FinalizeRenderTextureForExternalSampling()` calls.
  - Added `m_renderGraph.NotifyDebugTextureStateOverride("BlurredSceneOutput", ...)`
    immediately after `m_editorLayer->FinalizeBlurValidationForSampling(offscreenCmd);`,
    called UNCONDITIONALLY (no visibility into whether that call actually
    wrote anything this frame — see the phase document's own reasoning).
- `src/Renderer/FramePresenter.cpp` — added
  `graph.NotifyDebugTextureStateOverride("Swapchain", next);` immediately
  after the existing manual `PRESENT_SRC_KHR` `EmitImageBarrier()` call inside
  `PresentViaRenderGraph()`, reusing the exact same `next` value already
  computed there (never re-derived separately).
- `tests/Encoding/DepthVisualizationTests.cpp` (new file) — Tier-1 tests
  covering all three supported depth formats (including a `D24_UNORM_S8_UINT`
  case that deliberately puts garbage in the ignored top/stencil byte to
  prove the `0x00FFFFFF` mask is actually applied), the unrecognized-format
  case, null-pointer-with-positive-dimensions cases for both `rawDepth` and
  `outRgba8`, and the zero-sized-buffer safe-no-op case.
- `tests/CMakeLists.txt` — registered `Encoding/DepthVisualizationTests.cpp`
  in `GTE_TEST_SOURCES`, right after `Encoding/PixelConversionTests.cpp`.

## 3. Deviations from the strategy document

**None.** Every method signature, barrier sequence, `ResourceState` value,
call-site position, and include path matches the phase document's own
(already twice-audited) Step 3.1–3.6 verbatim. One incidental note: while
editing `Renderer.h`, an off-by-one `edit_line` call briefly duplicated the
`CaptureRenderTexturePixels()` declaration; this was caught immediately by
re-reading the file back and corrected before moving on — the final file
content matches the plan exactly (verified again below).

## 4. Manual diff-check: `CaptureRenderTexturePixels()` unchanged

- **Signature**: `CapturedRawPixels CaptureRenderTexturePixels(RenderTexture& texture) const;`
  — byte-for-byte identical to before this phase.
- **Behavior**: now `{ resolve ShaderRead ResourceState; return
  CaptureImagePixels(texture.Image(), VK_IMAGE_ASPECT_COLOR_BIT,
  texture.Format(), texture.Extent(), shaderReadState); }` — and
  `CaptureImagePixels()` itself (with `aspect=COLOR`, `bytesPerPixel=4`,
  `previousState=ShaderRead`) performs the EXACT same sequence the original
  body did: same buffer name (`"CaptureReadback"`), same barrier states
  (ShaderRead→TransferSrc→[copy]→[[transfer-write→host-read buffer
  barrier]]→TransferSrc→ShaderRead), same `VkBufferImageCopy` shape (whole
  image, `aspectMask=COLOR_BIT`, `layerCount=1`), same `memcpy`/`width`/
  `height`/`format` result construction. The only textual differences are
  variable names (`colorRange`→`range` local to the shared function) — zero
  observable behavior change for `Application::Run()`'s existing Game-view
  capture call site.
- The only remaining caller in the whole engine,
  `Application::Run()`'s `m_renderer.CaptureRenderTexturePixels(*gameTarget)`
  success-path capture block, is completely untouched by this phase.

## 5. Compile check

Ran `cmake --build build --target GreatTamanaEngineTests` (incremental):

```
[23/28] Building CXX object tests/CMakeFiles/GreatTamanaEngineTests.dir/Encoding/DepthVisualizationTests.cpp.obj
[26/28] Linking CXX static library libgte_core.a
[27/28] Linking CXX executable tests\GreatTamanaEngineTests.exe; Copying SDL3.dll next to GreatTamanaEngineTests
```

**Clean build, zero errors/warnings.** Also ran `cmake --build build` (the
default `GreatTamanaEngine.exe` target) afterward to confirm the main engine
executable — which transitively includes every file this phase touched
(`Renderer.h`/`.cpp`, `Application.cpp`, `FramePresenter.cpp`,
`ComputeBlurValidation.cpp`) — still builds cleanly end to end:

```
[2/2] Linking CXX executable GreatTamanaEngine.exe; Staging ... .spv next to GreatTamanaEngine; Copying SDL3.dll next to GreatTamanaEngine
```

## 6. Regression check

Per this task's own instructions, the full `ctest` regression suite was NOT
run (reserved for a later phase). As a fast, targeted sanity check beyond
"it compiles", the new Tier-1 test file plus Phase 1/2's own registry tests
were run together:

```
GreatTamanaEngineTests.exe --gtest_filter=DepthVisualizationTest.*:RenderGraphDebugTextureRegistryTest.*
```

**14/14 tests pass** (7 new `DepthVisualizationTest` cases + 7 pre-existing
`RenderGraphDebugTextureRegistryTest` cases, confirming zero regression in
the module this phase's own `Application.cpp`/`FramePresenter.cpp` changes
call into).

## 7. What this phase deliberately does NOT do (unchanged from the plan)

- Does not touch `FrameCaptureBridge`/`Network*` — Phases 4/5.
- Does not add depth-channel support to `/get_swapchain`/`/get_game_view` —
  unaffected; only the future `/get_texture` (Phase 5) will ever call
  `ConvertDepthToGrayscaleRgba8()`.
- Does not widen `IEditorLayer`'s interface — the unconditional
  `NotifyDebugTextureStateOverride("BlurredSceneOutput", ...)` call avoids
  needing that, per the plan.
- Does not add automated coverage for `CaptureImagePixels()` itself (stays
  Tier 2, needs a live device) — covered by Phase 6's later manual
  end-to-end verification, including the specific `?texture_name=Swapchain`/
  `?texture_name=BlurredSceneOutput` regression checks
  `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done" calls for.
- Does not run the full `ctest` regression suite — reserved for a later
  phase per this task's own instructions.

## Summary

- Branch: `feature/network-impl` (unchanged, as required).
- Edited files: `src/Renderer/Renderer.h`, `src/Renderer/Renderer.cpp`,
  `src/Application/Application.cpp`, `src/Renderer/FramePresenter.cpp`,
  `CMakeLists.txt`, `tests/CMakeLists.txt`.
- New files: `src/Encoding/DepthVisualization.h`,
  `src/Encoding/DepthVisualization.cpp`,
  `tests/Encoding/DepthVisualizationTests.cpp`.
- Compile check: clean, zero errors/warnings (both the test target and the
  main `GreatTamanaEngine.exe` target).
- Regression check: 14/14 targeted tests passing (new
  `DepthVisualizationTest` suite + pre-existing
  `RenderGraphDebugTextureRegistryTest` suite).
- `CaptureRenderTexturePixels()`'s public signature/behavior confirmed
  byte-for-byte unchanged for its existing caller (Section 4).
- All FOUR of Locked Design Decision 7's graph-external manual-finalize call
  sites (`"GameView"`/`"SceneView"`/`"Swapchain"`/`"BlurredSceneOutput"`) are
  now wired to `RenderGraph::NotifyDebugTextureStateOverride()`.
- No deviations from the strategy document.
- Ready for Phase 4 (`PHASE4_FRAMECAPTUREBRIDGE_NAMED_TEXTURE_SUPPORT`).
