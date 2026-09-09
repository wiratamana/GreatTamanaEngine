# PHASE3 — Generic GPU Image Readback + Depth Visualization + State-Override Wiring

> **Second-iteration audit note (this revision):** this file was re-checked
> against the live source tree (`Renderer.cpp`/`Renderer.h`,
> `VulkanDevice.cpp`'s `PickDepthFormat()`, `Application.cpp`,
> `FramePresenter.cpp`, `Editor/ComputeBlurValidation.cpp`,
> `RenderGraphBarrierPlanner.h`, `RenderTarget.h`, `src/Encoding/`) byte for
> byte. One MAJOR gap and several smaller correctness/precision gaps were
> found and are fixed in this revision:
> 1. **MAJOR — this file previously only wired TWO of the FOUR call sites
>    `PHASE0_MASTER_STRATEGY.md`'s own Locked Design Decision 7 (and its own
>    audit note) requires.** `PHASE2_RENDERGRAPH_INTEGRATION_AND_AUTO_REGISTRATION.md`'s
>    own audit note already says outright *"...FramePresenter.cpp's own
>    'Swapchain' finalize, and ComputeBlurValidation::FinalizeForSampling()'s
>    'BlurredSceneOutput' finalize - see Phase 3, which corrected an earlier
>    revision's 'only two call sites' undercount"* — but this file, as
>    written before this revision, had NOT actually done that correction: it
>    only ever wired `Application.cpp`'s `"GameView"`/`"SceneView"` call
>    sites (Step 3.4 below), leaving `"Swapchain"` and `"BlurredSceneOutput"`
>    with a stale, never-corrected registry entry — exactly the bug
>    `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done" `?texture_name=Swapchain`
>    regression check exists to catch. **Fixed**: this revision adds a new
>    Step 3.4a (`FramePresenter.cpp`'s own `"Swapchain"` correction, called
>    directly against the `rg::RenderGraph&` that function already receives
>    as a parameter) and extends Step 3.4 with `"BlurredSceneOutput"`'s own
>    correction call (from `Application.cpp`, since
>    `ComputeBlurValidation::FinalizeForSampling()` itself has no
>    `RenderGraph&` of its own to call through — see Step 3.4's own new note
>    on why this one is safe to call UNCONDITIONALLY, unlike the other
>    three). Both new snippets were written directly against the real, live
>    source (confirmed exact `ResourceState` values below), not guessed.
> 2. **`Renderer.h` needs a new `#include` this file previously never
>    mentioned.** The new `CaptureImagePixels()` public method signature
>    takes a `const rg::ResourceState&` parameter, but `Renderer.h` today
>    only forward-declares `gte::rg::RenderGraph`/`gte::rg::RenderGraphBuilder`
>    (see its own top-of-file forward declarations) — `ResourceState` itself
>    is declared in `RenderGraphBarrierPlanner.h`, which `Renderer.h` does
>    NOT include today (only `Renderer.cpp` does). Without adding this
>    include (or an equivalent forward declaration) to `Renderer.h` itself,
>    the new method signature simply does not compile. Confirmed safe to add
>    directly (no circular-include risk: `RenderGraphBarrierPlanner.h` only
>    includes `RenderGraphTypes.h` + `<volk.h>`, both already reachable from
>    `Renderer.h` today) — see the corrected Step 3.1 below.
> 3. **Wording inconsistency: Step 1 previously called `CaptureImagePixels()`
>    a "shared, private primitive" — it must be PUBLIC.** Phase 4's own named-
>    texture capture path calls `m_renderer.CaptureImagePixels(...)` directly
>    from `Application::Run()` — a different class entirely — so this method
>    cannot be `private` on `Renderer`. Confirmed live: every neighboring
>    method in this exact section of `Renderer.h`
>    (`CaptureRenderTexturePixels()`, `RequestSwapchainCapture()`,
>    `CreateBuffer()`, ...) is public; `Renderer.h`'s one and only
>    `private:` label appears much later in the file. Corrected throughout
>    this document to say "shared primitive", never "private".
> 4. **Missing test-registration reminder.** This file's own Step 3.6 always
>    said to add `tests/Encoding/DepthVisualizationTests.cpp`, but never said
>    to register it in `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list —
>    exactly the easy-to-miss gap `PHASE1_DEBUG_TEXTURE_REGISTRY_CORE_DATA_MODEL.md`'s
>    own audit note already called out and fixed for ITS test file. Fixed
>    here too (see Step 3.6).
> 5. **Tightened a hedge that has now actually been checked.** Step 3.4
>    previously said "add `#include`... if not already present — check
>    first" for `RenderGraphBarrierPlanner.h` in `Application.cpp`. Confirmed
>    live: it is genuinely NOT present yet at this phase (Application.cpp
>    only includes `RenderGraphBuilder.h`/`RenderPasses.h`-related headers
>    today, neither of which pulls in `RenderGraphBarrierPlanner.h`) — this
>    revision states plainly that the include MUST be added, rather than
>    leaving it as an open hedge for the implementer to resolve.
> 6. **Two small Tier-1 precision/defensiveness nits in the depth-conversion
>    sample**, both confirmed against this codebase's own established sibling
>    convention (`src/Encoding/PixelConversion.cpp`): (a) the `.cpp` sample
>    now explicitly includes `<cstddef>` for `std::size_t` rather than
>    depending on it arriving transitively via `<cstring>` (`PixelConversion.cpp`
>    already does this explicitly); (b) `ConvertDepthToGrayscaleRgba8()` now
>    also guards against a null `rawDepth`/`outRgba8` pointer when
>    width/height are both `> 0`, mirroring `ConvertBgraToRgbaInPlace()`'s own
>    `pixels == nullptr` guard — harmless in practice given today's one real
>    call site (Phase 4 always passes a real, correctly-sized
>    `CapturedRawPixels::pixels.data()`), but worth matching the established,
>    documented defensive-programming convention rather than silently relying
>    on the one known caller never doing anything else.
>
> Everything else in this file (the exact existing `CaptureRenderTexturePixels()`
> implementation quoted in Step 2, the three `PickDepthFormat()` candidates/
> order, the `"GameView"`/`"SceneView"` literal strings, the exact
> `ResourceState` values `FinalizeRenderTextureForExternalSampling()` already
> computes, every Vulkan function signature quoted, and the "every concrete
> depth format this engine can pick is exactly 4 bytes/texel for a
> depth-aspect-only copy" claim — verified correct against the Vulkan spec's
> own "Copying Data Between Buffers and Images" rules for combined
> depth/stencil formats) was re-checked and found accurate — no further
> changes were needed there.

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 2 (`RenderGraph::
DebugTextureSnapshotFor()`/`NotifyDebugTextureStateOverride()`).

## Step 1: The Goal (Where are we going?)

Four independent-but-related deliverables:

1. Generalize `Renderer::CaptureRenderTexturePixels()`'s existing
   implementation into a shared, PUBLIC primitive (`CaptureImagePixels()`)
   that can read back pixels from ANY `VkImage`/format/extent/`ResourceState`
   — not just a `RenderTexture&` — with ZERO behavior change to the existing
   public method or its existing caller (`Application::Run()`'s Game-view
   capture block). This is what Phase 4's named-texture capture path actually
   calls, from `Application::Run()` (a different class than `Renderer`
   itself), against whatever the registry (Phase 1/2) reports — so this new
   method MUST be public, not private, on `Renderer`.
2. Add `Renderer::WaitForGpuIdle()`, a thin wrapper over `vkDeviceWaitIdle()`
   — the primitive `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 1
   requires.
3. Add a depth-buffer → grayscale-RGBA8 conversion utility
   (`src/Encoding/`) for `?channel=depth` (Locked Design Decision 5).
4. **Wire ALL FOUR graph-external manual-finalize call sites to Phase 2's new
   `RenderGraph::NotifyDebugTextureStateOverride()` (Locked Design Decision
   7)** — not just two. A live grep (re-confirmed as part of this revision,
   see the audit note above) found exactly these four, and all four must be
   closed by this phase:
   - `Application::Run()`'s two `FinalizeRenderTextureForExternalSampling()`
     call sites, for `"GameView"`/`"SceneView"` (Step 3.4).
   - `FramePresenter.cpp`'s own manual `PRESENT_SRC_KHR` transition, for
     `"Swapchain"` (Step 3.4a — **new in this revision**).
   - `ComputeBlurValidation::FinalizeForSampling()`'s own manual transition,
     for `"BlurredSceneOutput"` (Step 3.4's own new note — **new in this
     revision**).
   Without all four, `/get_texture?texture_name=Swapchain` and
   `/get_texture?texture_name=BlurredSceneOutput` would each hand
   `Renderer::CaptureImagePixels()` a stale, wrong `previousState` — a real
   incorrect-image-layout-transition bug (a validation-layer error or a
   garbled/black captured image), not just a cosmetic metadata error. See
   `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done" — its
   `?texture_name=Swapchain` check exists specifically to catch this if it's
   ever missed.

## Step 2: The Situation (Where are we now?)

- `Renderer::CaptureRenderTexturePixels(RenderTexture& texture)`
  (`Renderer.cpp`, confirmed live) is ALREADY, in effect, "the generic
  primitive plus one caller" — it just hardcodes `texture.Image()`/
  `texture.Extent()`/`texture.Format()` and a fixed `ShaderRead` "previous
  state" assumption instead of taking them as parameters. Concretely,
  today's real implementation (re-read directly, byte for byte):
  1. Computes `size = extent.width * extent.height * 4` and creates a
     throwaway `Buffer` (`BufferMemoryUsage::GpuToCpu`, debug name
     `"CaptureReadback"`).
  2. `ImmediateSubmit()`s a lambda that: (a) barriers the image from
     `rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false)` to
     `rg::RequiredStateFor(rg::ResourceAccess::TransferSrc, false)`;
     (b) `vkCmdCopyImageToBuffer`s the WHOLE image into the readback buffer
     (a single `VkBufferImageCopy` with `aspectMask = VK_IMAGE_ASPECT_COLOR_BIT`,
     `layerCount = 1`); (c) emits the REQUIRED buffer transfer-write→host-read
     visibility barrier (`rg::EmitBufferBarrier`, explicit
     `VK_PIPELINE_STAGE_2_TRANSFER_BIT`/`VK_ACCESS_2_TRANSFER_WRITE_BIT` →
     `VK_PIPELINE_STAGE_2_HOST_BIT`/`VK_ACCESS_2_HOST_READ_BIT`) — this step
     has no depth-vs-color distinction, it is identical either way;
     (d) barriers the image back from `TransferSrcOptimal` to its ORIGINAL
     `ShaderRead` state.
  3. `memcpy`s the buffer's mapped data into a `CapturedRawPixels` result
     (`pixels`/`width`/`height`/`format`, `format` taken from
     `texture.Format()`).
  - The ONLY things that differ between "capture a `RenderTexture`'s color
    image" and "capture ANY named texture's color OR depth image" are: (a)
    which `VkImage` handle, (b) which `VkImageAspectFlags` (`COLOR_BIT` vs
    `DEPTH_BIT`), (c) which `VkFormat` (for the caller to later decide how
    to interpret/convert the raw bytes), (d) what the "previous"
    `ResourceState` actually is (today hardcoded; Phase 4 needs it to come
    from the registry instead), (e) whether 4 bytes/pixel is even the
    right assumption (color always is, on this engine's fixed RGBA8/BGRA8
    formats — depth is ALSO 4 bytes/pixel for every concrete depth format
    `VulkanDevice::PickDepthFormat()` can ever return, per the Vulkan spec's
    own "depth-aspect-only copy of a combined depth/stencil format uses that
    format's OWN full-word depth-aspect encoding" rule —
    `VK_FORMAT_D32_SFLOAT`/`VK_FORMAT_D32_SFLOAT_S8_UINT` both copy their
    depth aspect as a tightly-packed 32-bit float, and `VK_FORMAT_D24_UNORM_S8_UINT`
    copies its depth aspect as if it were `VK_FORMAT_X8_D24_UNORM_PACK32` — a
    32-bit word with the 24-bit UNORM value in its low bits, top byte
    undefined/padding — so ALL THREE are exactly 4 bytes/texel for a
    depth-aspect-only copy, confirmed against the Vulkan spec, not just
    assumed; the existing `size = width*height*4` formula already works
    unchanged for depth too — just interpreted differently afterward, see
    3.3 below).
- `VulkanDevice::PickDepthFormat()` (`VulkanDevice.cpp`, confirmed live —
  `src/Renderer/Vulkan/VulkanDevice.cpp`) only ever returns one of exactly
  three candidates, in this preference order (re-read directly): `VK_FORMAT_D32_SFLOAT`
  (pure 32-bit float, no stencil — the common case on desktop GPUs), then
  `VK_FORMAT_D32_SFLOAT_S8_UINT`, then `VK_FORMAT_D24_UNORM_S8_UINT` (a
  packed 24-bit UNORM depth value in the low 24 bits of each 32-bit word,
  matching how this engine's `RenderTarget::depthHasStencil` flag already
  exists to distinguish) — it throws `std::runtime_error` if NONE of the
  three is supported (`vkGetPhysicalDeviceFormatProperties`'s
  `optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT`
  check on each, in order). A depth-visualization conversion function needs
  to handle exactly these three, and ONLY these three — falling back to a
  clearly-flagged "cannot visualize this format" safe default (never a
  crash, never silently wrong colors) for anything else, mirroring
  `Application.cpp`'s own `IsBgraFormat()` "accepted narrow risk, documented"
  precedent.
- `Renderer::GetVulkanContextInfo().device` already exposes the raw
  `VkDevice` publicly — but `Renderer::WaitForGpuIdle()` doesn't need to go
  through that struct at all, since it is a NEW method living inside
  `Renderer.cpp` itself, which already has direct access to its own private
  `m_device` (`VulkanDevice`) member — `m_device.Native()` is the call
  (confirmed live: `Renderer.cpp`'s own destructor and move-assignment
  operator already call `vkDeviceWaitIdle(m_device.Native())` this exact
  way, for an unrelated cleanup reason — `WaitForGpuIdle()` is a separate,
  new, PUBLIC wrapper around the same underlying call, not a reuse of
  either of those two existing private call sites).
- **`Renderer.h` compile-hygiene requirement (new in this revision, see the
  audit note above): the new `CaptureImagePixels()` method's
  `const rg::ResourceState&` parameter requires `Renderer.h` itself to be
  able to see a complete (or at least forward-declared) `gte::rg::ResourceState`.**
  Confirmed live: `Renderer.h` today only forward-declares
  `namespace gte::rg { class RenderGraph; class RenderGraphBuilder; }` — it
  does NOT include `RenderGraph/RenderGraphBarrierPlanner.h` (where
  `ResourceState` is actually declared) at all; only `Renderer.cpp` does.
  `RenderGraphBarrierPlanner.h` itself only includes `RenderGraphTypes.h`
  (already included by `Renderer.h`) and `<volk.h>` (already reachable
  transitively) — so adding `#include "RenderGraph/RenderGraphBarrierPlanner.h"`
  directly to `Renderer.h` is cheap and introduces no circular-include risk.
  See Step 3.1.
- `Application::Run()`'s two `FinalizeRenderTextureForExternalSampling(
  offscreenCmd, *gameTarget)`/`(*sceneTarget)` call sites (`Application.cpp`,
  confirmed live) already know exactly which `ResourceState` the texture ends
  up in afterward — `RenderPasses.cpp`'s `FinalizeRenderTextureForExternalSampling()`
  itself computes, confirmed live:
  ```cpp
  const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false);
  const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
  ```
  — `next` is the EXACT value Phase 2's `NotifyDebugTextureStateOverride()`
  call needs to be handed, for names `"GameView"`/`"SceneView"` respectively
  (matching the exact string literals passed to `ImportTexture()` in
  `Application.cpp` itself — confirmed live, exact lines:
  `b.ImportTexture("GameView", gameTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);`
  and `b.ImportTexture("SceneView", sceneTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);`
  — these are the TEXTURE names; `"Present"` nearby is only ever the PASS
  name a sibling `AddPass()` call happens to use, never a texture name — do
  not confuse the two).
- **`FramePresenter.cpp`'s own manual `"Swapchain"` finalize (confirmed live,
  `PresentViaRenderGraph()`, right before `vkEndCommandBuffer`):**
  ```cpp
  const rg::ResourceState previous = capturedThisFrame
      ? rg::ResourceState{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT }
      : rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false);
  const rg::ResourceState next{
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE
  };
  const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  rg::EmitImageBarrier(cmd, target.image, range, previous, next);
  ```
  `PresentViaRenderGraph(rg::RenderGraph& graph, bool needsSwapchainDepth, ...)`
  (confirmed live — `FramePresenter.h`) already receives the exact
  `rg::RenderGraph&` reference this correction call needs, as a parameter —
  no new plumbing required, only adding the call right alongside this
  existing barrier (see Step 3.4a).
- **`ComputeBlurValidation::FinalizeForSampling(VkCommandBuffer cmd)`'s own
  manual `"BlurredSceneOutput"` finalize (confirmed live,
  `Editor/ComputeBlurValidation.cpp`):**
  ```cpp
  const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);
  const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
  ```
  guarded internally by a private `m_writtenThisFrame` flag (set `true` only
  when `AddPass()` actually declared a pass this call, consumed/reset here) —
  **critically, this function's OWN signature has no `RenderGraph&`
  parameter at all**, and neither does `IEditorLayer::FinalizeBlurValidationForSampling(VkCommandBuffer cmd)`
  (its only caller, from `Application.cpp`) — unlike `FramePresenter.cpp`'s
  `"Swapchain"` case, `Application::Run()` has NO visibility into whether
  the blur-validation pass actually ran this specific call (that is
  deliberately tracked only inside `ComputeBlurValidation` itself — see its
  own header comment on why). Widening `IEditorLayer`'s interface just for
  this correction call is unnecessary churn — see Step 3.4's own new note
  for why calling `NotifyDebugTextureStateOverride("BlurredSceneOutput", ...)`
  UNCONDITIONALLY, every frame, from `Application.cpp`, right after the
  existing `FinalizeBlurValidationForSampling()` call, is simple, safe, and
  sufficient instead.

## Step 3: The Plan

### 3.1 — `Renderer.h` changes

**First, a required include (new in this revision — see the audit note
above):** add, alongside `Renderer.h`'s existing includes (e.g. right next
to `"RenderGraph/RenderGraphTypes.h"`):

```cpp
#include "RenderGraph/RenderGraphBarrierPlanner.h" // gte::rg::ResourceState - needed by CaptureImagePixels() below.
```

(Confirmed safe: `RenderGraphBarrierPlanner.h` only additionally pulls in
`RenderGraphTypes.h`/`<volk.h>`, both already reachable from `Renderer.h`
today — no circular-include risk, no meaningful extra compile cost. An
alternative, slightly more minimal fix — forward-declaring
`namespace gte::rg { struct ResourceState; }` instead of including the whole
header — would also work for a reference parameter, but the direct include
is simpler and matches how `Renderer.h` already includes `RenderGraphTypes.h`
directly rather than forward-declaring pieces of it.)

Then add, near `CapturedRawPixels`/`CaptureRenderTexturePixels()` (this
section of `Renderer.h` is entirely PUBLIC — confirmed live: this class's
one and only `private:` label appears much later in the file, well after
this section — so both new methods below are public members, not private
ones, despite this phase's own goal text in an earlier revision saying
"private"; Phase 4 calls `CaptureImagePixels()` directly from
`Application::Run()`, a different class, so it MUST be public):

```cpp
// network-impl-4 campaign, Phase 3
// (task_manager/network-impl-4/PHASE3_GENERIC_IMAGE_READBACK_AND_DEPTH_VISUALIZATION.md) -
// the generalized, PUBLIC primitive behind BOTH CaptureRenderTexturePixels()
// above (which now just calls this with aspect=COLOR and a ShaderRead
// previous state) and GET /get_texture's own named-texture capture path
// (Phase 4, Application::Run()), which calls this directly with whatever
// RenderGraphDebugTextureRegistry (src/Renderer/RenderGraph/
// RenderGraphDebugTextureRegistry.h) reports for an arbitrary registered
// name's color OR depth half. Public (not private) specifically because
// Phase 4's caller lives in a different class (Application) than Renderer
// itself.
//
// `previousState` MUST be the image's actual, real current ResourceState -
// guessing wrong is a silent correctness bug (see network-impl-2's
// PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md's own analogous
// warning for RenderGraphBuilder::ImportTexture()'s `currentLayout`
// parameter). Restores the image to the SAME `previousState` afterward,
// unconditionally - see this phase's own Step 2 analysis for why this is
// cheap, always-safe insurance even in cases where nothing currently
// depends on the restored value being exactly right.
//
// bytesPerPixel must be exactly 4 for every real caller today (RGBA8/BGRA8
// color, or any of this engine's three possible depth formats copied via
// their DEPTH aspect alone - see PHASE3's own Step 2) - asserted, not
// silently handled for any other value.
CapturedRawPixels CaptureImagePixels(VkImage image, VkImageAspectFlags aspect, VkFormat format, VkExtent2D extent,
    const rg::ResourceState& previousState, int bytesPerPixel = 4) const;

// A full, blocking vkDeviceWaitIdle() - see PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision 1. NEVER call this from any per-frame/
// performance-sensitive path - it is reserved for the rare, explicit,
// human/LLM-triggered GET /get_texture request path (Phase 4) only. Every
// OTHER capture endpoint in this engine (/get_swapchain, /get_game_view)
// deliberately adds ZERO GPU stall and must stay that way - this method
// must never be called from anywhere those two endpoints' own code paths
// reach.
void WaitForGpuIdle() const;
```

`CaptureRenderTexturePixels()`'s own existing doc comment gains one line
noting it is now implemented in terms of `CaptureImagePixels()` — no
signature/behavior change.

### 3.2 — `Renderer.cpp` changes

Refactor the existing `CaptureRenderTexturePixels()` body (Step 2's exact
listing above) into:

```cpp
Renderer::CapturedRawPixels Renderer::CaptureImagePixels(VkImage image, VkImageAspectFlags aspect, VkFormat format,
    VkExtent2D extent, const rg::ResourceState& previousState, int bytesPerPixel) const
{
    assert(bytesPerPixel == 4 && "CaptureImagePixels: every real caller today copies exactly 4 bytes/pixel (RGBA8/BGRA8 color, or any of this engine's 3 possible depth formats via their DEPTH aspect alone) - re-derive this function's own size math before changing it for a genuinely different pixel size.");

    const VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * static_cast<VkDeviceSize>(bytesPerPixel);
    Buffer readback = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferMemoryUsage::GpuToCpu, "CaptureReadback");

    const VkImageSubresourceRange range{ aspect, 0, 1, 0, 1 };
    const rg::ResourceState transferSrcState = rg::RequiredStateFor(rg::ResourceAccess::TransferSrc, false);

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        rg::EmitImageBarrier(cmd, image, range, previousState, transferSrcState);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = aspect;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { extent.width, extent.height, 1 };
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.Native(), 1, &region);

        const rg::ResourceState transferWriteState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT };
        const rg::ResourceState hostReadState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT };
        rg::EmitBufferBarrier(cmd, readback.Native(), 0, size, transferWriteState, hostReadState);

        rg::EmitImageBarrier(cmd, image, range, transferSrcState, previousState);
    });

    CapturedRawPixels result;
    result.pixels.resize(static_cast<std::size_t>(size));
    std::memcpy(result.pixels.data(), readback.MappedData(), static_cast<std::size_t>(size));
    result.width = static_cast<int>(extent.width);
    result.height = static_cast<int>(extent.height);
    result.format = format;
    return result;
}

Renderer::CapturedRawPixels Renderer::CaptureRenderTexturePixels(RenderTexture& texture) const
{
    const rg::ResourceState shaderReadState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    return CaptureImagePixels(
        texture.Image(), VK_IMAGE_ASPECT_COLOR_BIT, texture.Format(), texture.Extent(), shaderReadState);
}

void Renderer::WaitForGpuIdle() const
{
    vkDeviceWaitIdle(m_device.Native());
}
```

`Renderer.cpp` already includes `RenderGraph/RenderGraphBarrierPlanner.h`
and `<cassert>`/`<cstring>` (confirmed live, top of file) — no further
include changes needed there.

### 3.3 — `src/Encoding/DepthVisualization.h`/`.cpp` (new files)

```cpp
// DepthVisualization.h
#pragma once

#include <cstdint>
#include <volk.h>

namespace gte::Encoding {

// network-impl-4 campaign, Phase 3
// (task_manager/network-impl-4/PHASE3_GENERIC_IMAGE_READBACK_AND_DEPTH_VISUALIZATION.md) -
// converts a tightly-packed, row-major depth-aspect-only readback buffer
// (width*height*4 bytes, no row padding - see Renderer::CaptureImagePixels()'s
// own doc comment for why every depth format this engine can ever pick is
// exactly 4 bytes/texel for a depth-aspect-only copy) into a plain grayscale
// RGBA8 image (R==G==B==the depth value remapped to [0,255], A==255) for
// PNG encoding - exactly the same "ready for Encoding::EncodeRgba8ToPng()"
// output shape ConvertBgraToRgbaInPlace() already produces for color.
//
// Supports EXACTLY the three concrete depth formats
// VulkanDevice::PickDepthFormat() (src/Renderer/Vulkan/VulkanDevice.cpp)
// can ever actually return - VK_FORMAT_D32_SFLOAT (pure float32, values
// already in [0,1] - just multiply by 255), VK_FORMAT_D32_SFLOAT_S8_UINT
// (identical to the above for a DEPTH-aspect-only copy - the stencil byte
// is a separate aspect, never present in this buffer at all), and
// VK_FORMAT_D24_UNORM_S8_UINT (a 24-bit UNORM value packed into the low 24
// bits of each 32-bit little-endian word - divide by 0x00FFFFFF). Returns
// `false` (leaving `outRgba8` UNTOUCHED) for any other format - the caller
// (Phase 4/5) must treat that as a clean, explicit failure ("depth format
// not recognized"), never silently emit a wrong/blank image. This is the
// exact "accepted narrow risk, explicitly documented, never a crash or a
// silently-wrong result" precedent Application.cpp's own IsBgraFormat()
// already established for the color/BGRA case.
//
// `rawDepth` is exactly width*height*4 bytes (see caller). `outRgba8` must
// already be sized to width*height*4 bytes before calling - this function
// only ever WRITES into it, never resizes it (mirrors
// ConvertBgraToRgbaInPlace()'s own "caller owns the buffer" convention,
// though this one is an OUT buffer, not in-place, since the source/dest
// pixel encodings are structurally different, not just channel-swapped).
// A 0-sized buffer (width <= 0 or height <= 0) is a safe no-op; a NULL
// `rawDepth`/`outRgba8` with a genuinely positive width/height is also a
// safe no-op returning `false` (mirrors ConvertBgraToRgbaInPlace()'s own
// `pixels == nullptr` guard - never dereferenced blindly).
bool ConvertDepthToGrayscaleRgba8(
    const std::uint8_t* rawDepth, VkFormat depthFormat, int width, int height, std::uint8_t* outRgba8);

} // namespace gte::Encoding
```

```cpp
// DepthVisualization.cpp
#include "DepthVisualization.h"

#include <cstddef>
#include <cstring>

namespace gte::Encoding {

namespace {

std::uint8_t ClampToByte(float value)
{
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
}

} // namespace

bool ConvertDepthToGrayscaleRgba8(
    const std::uint8_t* rawDepth, VkFormat depthFormat, int width, int height, std::uint8_t* outRgba8)
{
    if (width <= 0 || height <= 0) {
        return true; // Nothing to do - a 0-sized buffer is a safe no-op, mirroring ConvertBgraToRgbaInPlace()'s own convention.
    }
    if (rawDepth == nullptr || outRgba8 == nullptr) {
        return false; // Defensive - mirrors ConvertBgraToRgbaInPlace()'s own null-pointer guard; should never happen given today's one real caller (Phase 4), which always passes a real, correctly-sized buffer.
    }
    if (depthFormat != VK_FORMAT_D32_SFLOAT && depthFormat != VK_FORMAT_D32_SFLOAT_S8_UINT
        && depthFormat != VK_FORMAT_D24_UNORM_S8_UINT) {
        return false; // Unrecognized format - see this function's own header doc comment.
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint32_t word = 0;
        std::memcpy(&word, rawDepth + i * 4, 4);

        std::uint8_t gray = 0;
        if (depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
            const std::uint32_t depth24 = word & 0x00FFFFFFu;
            gray = ClampToByte(static_cast<float>(depth24) / static_cast<float>(0x00FFFFFFu));
        } else {
            float depthFloat = 0.0f;
            std::memcpy(&depthFloat, &word, 4);
            gray = ClampToByte(depthFloat);
        }

        std::uint8_t* out = outRgba8 + i * 4;
        out[0] = gray;
        out[1] = gray;
        out[2] = gray;
        out[3] = 255;
    }
    return true;
}

} // namespace gte::Encoding
```

Add both files to `CMakeLists.txt`'s existing `src/Encoding/*` source list
(alongside `Base64.cpp`/`PixelConversion.cpp`/`PngEncoder.cpp` — confirmed
live, exact neighboring lines in the repository root `CMakeLists.txt`).

### 3.4 — `Application.cpp` state-override wiring (`"GameView"`/`"SceneView"`/`"BlurredSceneOutput"`)

Immediately after each of the two existing `FinalizeRenderTextureForExternalSampling()`
calls (re-read live to confirm they are still at these relative positions
before editing):

```cpp
if (gameTarget != nullptr) {
    FinalizeRenderTextureForExternalSampling(offscreenCmd, *gameTarget);
    // network-impl-4 campaign, Phase 3 - keeps the debug-texture registry's
    // OWN idea of "GameView"'s current color state correct across this
    // graph-external manual transition - see PHASE0_MASTER_STRATEGY.md's
    // Locked Design Decision 7. Must use the EXACT same ResourceState
    // FinalizeRenderTextureForExternalSampling() itself just transitioned
    // to (ShaderRead) - re-derive via the same rg::RequiredStateFor() call,
    // never hand-guessed.
    m_renderGraph.NotifyDebugTextureStateOverride(
        "GameView", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
}
if (sceneTarget != nullptr) {
    FinalizeRenderTextureForExternalSampling(offscreenCmd, *sceneTarget);
    m_renderGraph.NotifyDebugTextureStateOverride(
        "SceneView", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
}
```

Then, right after the existing `m_editorLayer->FinalizeBlurValidationForSampling(offscreenCmd);`
call (still inside the same enclosing block, before `m_renderer.EndOffscreenRenderGraphRecording();`):

```cpp
m_editorLayer->FinalizeBlurValidationForSampling(offscreenCmd);
// network-impl-4 campaign, Phase 3 - the "BlurredSceneOutput" correction
// (Locked Design Decision 7's third of four call sites). Unlike GameView/
// SceneView above, Application.cpp has NO visibility into whether
// ComputeBlurValidation::FinalizeForSampling() actually did anything this
// call (that is tracked purely internally, via its own private
// m_writtenThisFrame flag - see ComputeBlurValidation.h's own doc comment
// on why) - so this call is made UNCONDITIONALLY, every frame, rather than
// guarded by an `if`. This is safe: RenderGraphDebugTextureRegistry::
// ApplyColorStateOverride() (Phase 1) is a documented no-op when
// "BlurredSceneOutput" isn't a currently-known name (the debug-blur toggle
// has never been turned on this session), and idempotent (harmless) when
// it's already correctly ShaderRead from an earlier frame's correction -
// there is no code path where calling this "too often" produces a wrong
// result.
m_renderGraph.NotifyDebugTextureStateOverride(
    "BlurredSceneOutput", rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));
```

**`#include` requirement (confirmed live, not just a hedge — see this
file's own audit note above): `Application.cpp` does NOT yet include
`RenderGraph/RenderGraphBarrierPlanner.h`** (only `RenderGraph/RenderGraphBuilder.h`,
via which `rg::TextureHandle`/`rg::RenderGraphBuilder` are already visible —
`rg::ResourceState`/`rg::RequiredStateFor`/`rg::ResourceAccess` are NOT).
Add:

```cpp
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
```

alongside `Application.cpp`'s existing `#include "../Renderer/RenderGraph/RenderGraphBuilder.h"`.

### 3.4a — `FramePresenter.cpp` state-override wiring (`"Swapchain"`) — new in this revision

`FramePresenter::PresentViaRenderGraph(rg::RenderGraph& graph, ...)` already
receives the `rg::RenderGraph&` reference this correction needs, as a
parameter — this is the ONE call site (of the four) that can, and should,
call `NotifyDebugTextureStateOverride()` directly against its own function
parameter, rather than needing help from `Application.cpp`. Add, immediately
after the existing manual `PRESENT_SRC_KHR` barrier block (confirmed live —
right before `if (vkEndCommandBuffer(cmd) != VK_SUCCESS) { ... }`):

```cpp
{
    const rg::ResourceState previous = capturedThisFrame
        ? rg::ResourceState{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
              VK_ACCESS_2_TRANSFER_READ_BIT }
        : rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false);
    const rg::ResourceState next{
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE
    };
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    rg::EmitImageBarrier(cmd, target.image, range, previous, next);

    // network-impl-4 campaign, Phase 3 - Locked Design Decision 7's fourth
    // (of four) call site. Called unconditionally, every real Present -
    // this manual PRESENT_SRC_KHR transition ALWAYS runs here (unlike
    // ComputeBlurValidation's own conditional finalize above), so there is
    // no "did this actually run this frame" ambiguity to handle. Must use
    // the EXACT same `next` value just computed above - never re-derived
    // separately, so the two can never silently drift apart.
    graph.NotifyDebugTextureStateOverride("Swapchain", next);
}
```

`FramePresenter.cpp` already includes `RenderGraph/RenderGraph.h` (confirmed
live), which is where Phase 2 added `NotifyDebugTextureStateOverride()` — no
new include is needed here.

### 3.5 — What this phase deliberately does NOT do

- Does not touch `FrameCaptureBridge`/`Network*` — Phases 4/5.
- Does not change `CaptureRenderTexturePixels()`'s public signature,
  return type, or observable behavior for its existing caller in any way —
  confirm via a byte-for-byte diff of `/get_game_view`'s manual test
  output before vs. after this phase, per Phase 6's own regression check.
- Does not add depth-channel support to `/get_swapchain`/`/get_game_view` —
  those two endpoints are unaffected; only the new `/get_texture` (Phase 5)
  ever calls `ConvertDepthToGrayscaleRgba8()`.
- Does not widen `IEditorLayer`'s interface (e.g. to expose whether
  `ComputeBlurValidation` wrote this frame) — the unconditional
  `NotifyDebugTextureStateOverride("BlurredSceneOutput", ...)` call (Step
  3.4) avoids needing that.

### 3.6 — Tests

- `tests/Encoding/DepthVisualizationTests.cpp` (Tier 1 — no live device
  needed, exactly like the existing `PixelConversionTests.cpp`): hand-craft
  a small 2x2 (or 1x1) buffer of known raw bytes for EACH of the three
  supported formats and assert the exact expected grayscale output byte-
  for-byte (e.g. a `VK_FORMAT_D32_SFLOAT` value of `1.0f` → `{255,255,255,255}`;
  `0.0f` → `{0,0,0,255}`; a `VK_FORMAT_D24_UNORM_S8_UINT` word of
  `0x00FFFFFF` (ignoring the top stencil byte, which for a depth-only copy
  is never present in the buffer at all) → `{255,255,255,255}`). Also
  assert `ConvertDepthToGrayscaleRgba8()` returns `false` (and leaves the
  output buffer untouched — assert via a sentinel fill pattern beforehand)
  for an unrecognized format, e.g. `VK_FORMAT_R8G8B8A8_UNORM`, and for a
  null `rawDepth`/`outRgba8` pointer with a positive width/height (new
  case, see this file's own audit note above).
- **Register the new test file in `tests/CMakeLists.txt`'s own
  `GTE_TEST_SOURCES` list** (a real, easy-to-miss second step — see
  `PHASE1_DEBUG_TEXTURE_REGISTRY_CORE_DATA_MODEL.md`'s own audit note for
  why this exact omission is worth calling out explicitly rather than
  assuming it's obvious): grep for `Encoding/PixelConversionTests.cpp`
  there to find the exact list to extend (confirmed live: a plain, flat
  list of relative test-source paths). Confirm this by checking the test
  binary's own `--gtest_list_tests` output includes
  `DepthVisualizationTests.*` before considering this phase done.
- `CaptureImagePixels()` itself stays Tier 2 (needs a live device) — no new
  automated test; covered by Phase 6's manual end-to-end verification
  (which must explicitly confirm `/get_game_view`'s output is UNCHANGED
  after this refactor, plus a NEW manual check of `/get_texture`'s own
  depth-channel output once Phase 5 exists, AND
  `/get_texture?texture_name=Swapchain`/`?texture_name=BlurredSceneOutput`
  specifically, per this phase's own corrected Locked Design Decision 7
  scope).
- **Fast compile check**: `cmake --build build` must succeed before moving
  to Phase 4.
