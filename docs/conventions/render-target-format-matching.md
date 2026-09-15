# Render Target Format Matching

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

Vulkan pipelines are built against an exact color format
(`VkPipelineRenderingCreateInfo::pColorAttachmentFormats`, since this engine
uses dynamic rendering - no `VkRenderPass`/`VkFramebuffer`) - binding a
pipeline built for one format to a target that actually has a different
format is invalid per the spec, and can silently misrender or crash
depending on the driver instead of failing loudly. Follow these rules
whenever adding a real graphics pipeline or a new render target:

- **`Renderer::ColorFormat()`** (`src/Renderer/Renderer.h/.cpp`) is the
  single source of truth for "the" color format this engine renders with -
  whatever `VulkanSwapchain` actually negotiated at runtime (see
  `ChooseSurfaceFormat` in `VulkanSwapchain.cpp`), which can legitimately
  differ across GPUs/drivers. Never hardcode a `VkFormat` literal (e.g.
  `VK_FORMAT_B8G8R8A8_UNORM`) into a pipeline's
  `VkPipelineRenderingCreateInfo` or into a `RenderTexture` you expect to
  share a pipeline with the swapchain - read it from `Renderer::ColorFormat()`
  instead.
- **`Renderer::CreateRenderTexture()`'s `format` parameter defaults to
  `VK_FORMAT_UNDEFINED`**, meaning "match `ColorFormat()` exactly" (resolved
  internally in `Renderer.cpp`, not baked into the default argument as a
  literal) - this is what lets a single pipeline built once against
  `ColorFormat()` legally draw into either the swapchain or a default-format
  `RenderTexture` (e.g. the Editor's "Game" view). Only pass an explicit
  format when a target is deliberately different (e.g. a future HDR
  intermediate or a shadow map) - that target needs its own dedicated
  pipeline variant built for its exact format, never the default pipeline.
- **`FrameRecorder::RecordFrame()` asserts (debug builds only) that
  every target it's given has `target.format == ColorFormat()`.** This is
  the one recording path shared by `Present()` and `RenderOffscreen()`, so
  it's the natural place a future pipeline-bound draw call (recorded via
  `recordExtra`) runs - the assert exists to catch a format mismatch loudly,
  right there, instead of a confusing validation-layer warning (or silent
  misrendering on a driver that happens to tolerate it). A deliberately
  different-format target (see above) needs its own recording path rather
  than going through this assert unmodified - don't weaken or delete the
  assert to make a special case fit.
- **This same discipline applies to DEPTH, not just color.**
  `Renderer::DepthFormat()` (`VulkanDevice::PickDepthFormat()`, queried once
  from the physical device rather than hardcoded) is depth's equivalent of
  `Renderer::ColorFormat()` - every `Pipeline` is built with
  `VkPipelineRenderingCreateInfo::depthAttachmentFormat` set to it, and every
  render target (the swapchain's own per-image `DepthBuffer`s in
  `FramePresenter`, or a `RenderTexture`'s own companion `DepthBuffer` - see
  `src/Renderer/DepthBuffer.h`) is created at that exact same format.
  `FrameRecorder::RecordFrame()` asserts `target.depthFormat ==
  DepthFormat()` right alongside its existing color-format assert, for
  exactly the same reason. This was added specifically because the engine's
  original hardcoded triangle demo was always flat/coplanar (no real
  occlusion to get wrong), so a genuinely 3D, depth-tested render target
  (needed once the built-in primitive shapes - `Renderer/Primitives/
  PrimitiveMeshGenerator.h` - introduced real overlapping-in-screen-space
  geometry) never existed until now - don't reintroduce a render target or
  pipeline that skips a depth attachment/depth test, even for something that
  "looks flat," without a specific reason.
