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
  swapchain format - whatever `VulkanSwapchain` actually negotiated at runtime (see
  `ChooseSurfaceFormat` in `VulkanSwapchain.cpp`), which can legitimately
  differ across GPUs/drivers. Never hardcode a `VkFormat` literal (e.g.
  `VK_FORMAT_B8G8R8A8_UNORM`) into a pipeline's
  `VkPipelineRenderingCreateInfo` - read it from the renderer instead.
  Only what really draws into the swapchain (ImGui, the final blit) and the mesh
  preview renderer (its own `RenderTexture` and pipeline) use it.
- **`Renderer::ScenePixelFormat()`** is the one format of scene color: RGBA16_Float.
  The Game and Scene view targets, the direct Game target, every scene geometry
  pipeline (the 7-argument `Renderer::CreatePipeline()`), the scene grid and the
  render-feature compositor targets are built against it. Scene color is linear HDR;
  the ToneMapping feature turns it into display values. This is a deliberately
  different format from the swapchain, so a scene pipeline must never be recorded
  into a swapchain-format target.
- **`Renderer::CreateRenderTexture()`'s `format` parameter defaults to
  `Undefined`**, meaning "match `ColorFormat()` exactly" (resolved
  internally in `Renderer.cpp`). View targets created through
  `Core::CreateRenderView` resolve `Undefined` to `ScenePixelFormat()` instead.
  Pass an explicit format when a target is deliberately different (e.g. a
  shadow map) - that target needs its own dedicated pipeline variant built for its exact format.
- **`FrameRecorder::RecordFrame()` asserts (debug builds only) that
  every target it's given has `target.format == ColorFormat()`.** It serves
  the swapchain `Present()` and `RenderOffscreen()` (used by the mesh preview).
  Scene views do not go through it. Don't weaken the assert to fit a special case.
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
- **A `RenderTexture`'s GPU barrier state is never guessed by a caller.**
  It is always read via `CurrentState()` and written back via
  `SetCurrentState()` - see `RenderTexture::FinalizeForExternalSampling()`
  and `RenderGraphBuilder::ImportTexture()`'s `trackedOwner` parameter.
- **The cluster viewer (`ClusterViewRenderer`) uses explicit RGBA8_UNorm.** Its slot and ID targets and all of its colour
  pipelines are built for `RGBA8_UNorm`, never for `ColorFormat()`, so the swapchain format does not matter to it.
