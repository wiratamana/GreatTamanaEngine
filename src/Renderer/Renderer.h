#pragma once

#include "../Math/Mat4.h"
#include "Buffer.h"
#include "FramePresenter.h"
#include "FrameRecorder.h"
#include "GpuResourceFactory.h"
#include "Memory/GpuMemoryTracker.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderTexture.h"
#include "Texture2D.h"
#include "Vulkan/VulkanAllocator.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanSurface.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gte {

class Window;

// Owns the entire Vulkan pipeline for a Window: instance, surface, device,
// and allocator (acquired piece-by-piece in the constructor, each piece
// itself RAII-owned - see Vulkan/*), plus three collaborators that do the
// actual work and are released automatically, in reverse order, in the
// destructor:
//   - FramePresenter (FramePresenter.h): owns the swapchain and every
//     per-frame/per-image synchronization object, and implements
//     Present()/RenderOffscreen()'s actual Vulkan recording/submission.
//   - GpuResourceFactory (GpuResourceFactory.h): the Buffer/RenderTexture/
//     Pipeline/Mesh factories, GPU memory tracking, and ImmediateSubmit().
//   - FrameRecorder (FrameRecorder.h): this frame's clear color plus the
//     queued Submit() draw list, and the dynamic-rendering command
//     sequence shared by Present()/RenderOffscreen() to record it.
// Renderer itself is now just a thin façade: every public method below
// simply forwards to whichever collaborator actually implements it, so
// Game/main code is completely unaffected by this split - every signature
// is identical to before.
//
// Public API is intentionally still just Clear()/Present(), matching the
// original SDL_Renderer-backed version, so Game/main code did not need to
// change for this swap. Clear() only records the desired clear color;
// the actual clear happens as part of Present() (Vulkan has no equivalent
// of an immediate "clear now" call outside of a recorded command buffer).
//
// Also owns (via FramePresenter/GpuResourceFactory) the machinery to render
// into an off-screen RenderTexture instead of the swapchain
// (RenderOffscreen()/CreateRenderTexture()) - the primitive behind
// Unity-style Editor "Game"/"Scene" panels: a camera renders into a
// RenderTexture, which the Editor then displays inside an ImGui::Image()
// panel, while a final/release build (no Editor compiled in) instead
// renders the same scene straight into the swapchain via Present(),
// fullscreen, with no Editor/ImGui involved at all. Callers (Game, a future
// Editor module, ...) never touch raw Vulkan handles for this -
// RenderTarget/RenderTexture are the abstraction boundary.
class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Every member below is itself a properly move-safe RAII type (and
    // none of them hold a reference/pointer to each other - see
    // FramePresenter.h's class comment), so a plain member-wise move is
    // correct here. NOTE: if a raw Vulkan handle is ever added directly to
    // Renderer (rather than inside one of its collaborators), this would
    // need to become a hand-written move again, same as
    // operator=(Renderer&&) below.
    Renderer(Renderer&& other) noexcept = default;
    Renderer& operator=(Renderer&& other) noexcept;

    // Sets the color the render target will be cleared to on the next
    // Present() or RenderOffscreen() call (0-255 per channel).
    void Clear(std::uint8_t r = 0, std::uint8_t g = 0, std::uint8_t b = 0, std::uint8_t a = 255);

    // Acquires the next swapchain image, records+submits a command buffer
    // that clears it to the last Clear() color, and presents it. Handles
    // swapchain recreation transparently (resize, or out-of-date/suboptimal
    // results from the driver).
    //
    // recordExtra, if set, is invoked with the recording command buffer
    // between the clear and the final present-layout transition (i.e.
    // between what used to be vkCmdBeginRendering/vkCmdEndRendering) - the
    // seam a future overlay pass (the Editor's own ImGui chrome, a debug
    // UI, ...) hooks its draw commands into, without Renderer ever needing
    // to know what ImGui - or anything else - is.
    void Present(const std::function<void(VkCommandBuffer)>& recordExtra = {});

    // Renders into an off-screen RenderTexture instead of the swapchain:
    // clears it to the last Clear() color, runs recordExtra (if set)
    // exactly like Present() does, and leaves the texture in
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ready to sample (e.g. wrap
    // in an ImGui descriptor set and display via ImGui::Image() - the
    // Unity-style Editor "Game"/"Scene" panel use case).
    //
    // Deliberately synchronous for now (blocks until the GPU finishes) -
    // simplest correct thing while this is only used a couple of times a
    // frame for Editor panels; revisit (e.g. pipeline against its own
    // frames-in-flight) if it ever shows up as a bottleneck.
    void RenderOffscreen(RenderTexture& target, const std::function<void(VkCommandBuffer)>& recordExtra = {});

    // The color format this Renderer's swapchain actually negotiated at
    // runtime (see VulkanSwapchain.cpp's ChooseSurfaceFormat) - the single
    // shared source of truth every default-format RenderTexture (and every
    // pipeline built against it) should agree on, so the same pipeline can
    // legally draw into either the swapchain or an off-screen RenderTexture
    // created with CreateRenderTexture()'s default format below. See
    // AGENTS.md ("Render Target Format Matching").
    VkFormat ColorFormat() const noexcept;

    // The depth-buffer format every pipeline/DepthBuffer this Renderer
    // creates is built against (see VulkanDevice::PickDepthFormat() - the
    // single source of truth, queried once from the physical device rather
    // than hardcoded). The exact depth counterpart to ColorFormat() above -
    // see AGENTS.md ("Render Target Format Matching"). Every render target
    // this engine draws into (the swapchain, or an off-screen RenderTexture)
    // has its own real DepthBuffer at this format, so real (non-coplanar)
    // 3D geometry - the built-in primitive shapes (Renderer/Primitives/
    // PrimitiveMeshGenerator.h) - is correctly depth-tested/occluded.
    VkFormat DepthFormat() const noexcept;

    // Factory for off-screen render targets, so callers (Game, a future
    // Editor module, ...) never need direct access to the
    // VkPhysicalDevice/VkDevice this Renderer owns internally. `format`
    // defaults to VK_FORMAT_UNDEFINED, meaning "match ColorFormat() exactly"
    // (resolved in Renderer.cpp) - this is what guarantees a pipeline built
    // once against ColorFormat() can render into either the swapchain or a
    // default-format RenderTexture without a VkPipelineRenderingCreateInfo
    // mismatch. Only pass an explicit format for a target that's
    // deliberately different (e.g. a future HDR intermediate) and will have
    // its own dedicated pipeline variant built for that exact format. See
    // AGENTS.md ("Render Target Format Matching"). debugName is
    // optional/Editor-only - see RenderTexture's constructor comment.
    // depthDebugName likewise optionally names the companion DepthBuffer
    // separately (e.g. "GameView" / "GameViewDepth") - left null if the
    // depth side isn't worth naming individually.
    RenderTexture CreateRenderTexture(int width, int height, VkFormat format = VK_FORMAT_UNDEFINED,
        const char* debugName = nullptr, const char* depthDebugName = nullptr) const;

    // Factory for GPU buffers (vertex/index/uniform/staging), so callers
    // never need direct access to the VmaAllocator this Renderer owns
    // internally. See BufferMemoryUsage (Buffer.h) for how memoryUsage
    // picks between device-local and host-mapped allocation. debugName is
    // optional/Editor-only - see Buffer's constructor comment.
    Buffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage,
        const char* debugName = nullptr) const;

    // Convenience for the common "static GPU-only buffer initialized once"
    // case (vertex/index buffers, immutable uniform data, ...): uploads
    // `data` into a temporary host-visible staging Buffer, then records and
    // submits a one-shot command buffer (see ImmediateSubmit() below) that
    // copies it into a freshly created BufferMemoryUsage::GpuOnly Buffer of
    // the requested usage, and blocks until that copy finishes before
    // returning it. `usage` should NOT include VK_BUFFER_USAGE_TRANSFER_DST_BIT
    // - it's added automatically. debugName is optional/Editor-only, and
    // applies to the returned (destination) Buffer - the temporary staging
    // Buffer is unnamed, since it's gone before this call even returns.
    Buffer CreateDeviceLocalBuffer(
        const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* debugName = nullptr) const;

    // Records a one-time-submit command buffer (recordFn), submits it to
    // the graphics queue, and blocks until it finishes. The primitive
    // behind CreateDeviceLocalBuffer() above, but also useful directly for
    // future one-off GPU work (image layout transitions, mipmap
    // generation, ...) that doesn't belong inside Present()/
    // RenderOffscreen()'s per-frame recording.
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& recordFn) const;

    // Call once per frame, before Game::Update()/Render() - clears any draw
    // items queued last frame via Submit() (see below) so this frame always
    // starts from an empty queue. Also guards against a queue growing
    // unbounded across frames where nothing ever consumes it (e.g. a
    // minimized window with no Editor).
    void BeginFrame();

    // Queues one draw call - a Pipeline plus the Mesh to draw with it, plus
    // the world matrix to draw it with (defaults to Mat4::Identity()) and
    // the view-projection matrix of whichever camera this draw's target is
    // being rendered through (also defaults to Mat4::Identity(), meaning
    // "no camera" - vertices land directly in clip space, this engine's
    // original pre-Camera triangle-demo behavior) - both pushed to the
    // shader via vkCmdPushConstants (see Pipeline.h's push constant range)
    // as `pc.viewProj * pc.model * vec4(position, 0.0, 1.0)`. This is the
    // seam that lets Game/RenderSystem record draws without ever touching a
    // VkCommandBuffer or knowing Vulkan exists at all: the caller holds
    // onto Pipeline/Mesh objects (built via CreatePipeline()/CreateMesh())
    // and just calls this once per object it wants drawn each frame -
    // Renderer is the only thing that ever issues the actual
    // vkCmdBindPipeline/vkCmdPushConstants/vkCmdBindVertexBuffers/vkCmdDraw
    // calls. Not persistent - must be called again every frame an object
    // should be drawn (there is no retained scene graph inside Renderer
    // itself - see src/Game/RenderSystem.h for where that now lives, one
    // layer up). Called once per VISIBLE render target per frame (e.g. once
    // for the Editor's "Game" view, again for its "Scene" view) with that
    // target's own aspect-ratio-derived viewProjMatrix - see
    // RenderSystem::Draw().
    void Submit(const Pipeline& pipeline, const Mesh& mesh, const Mat4& modelMatrix = Mat4::Identity(),
        const Mat4& viewProjMatrix = Mat4::Identity());

    // Factory for graphics pipelines, so callers never need direct access
    // to the VkDevice this Renderer owns internally, and always get a
    // pipeline built against the exact color format this Renderer actually
    // renders with (see ColorFormat() and AGENTS.md, "Render Target Format
    // Matching"). vertexShaderSpirvPath/fragmentShaderSpirvPath point at
    // compiled SPIR-V binaries - see cmake/CompileShaders.cmake, which
    // compiles src/Shaders/*.vert/*.frag into "<exe dir>/shaders/*.spv" at
    // build time. `vertexLayout` (see Pipeline.h's VertexLayout) defaults to
    // VertexLayout::PositionColor - every existing call site is unaffected;
    // pass VertexLayout::PositionNormal for a pipeline meant to draw a Mesh
    // built from the indexed CreateMesh() overload below (e.g.
    // Game::CreateMeshEntityFromGtaFile()'s own pipeline).
    Pipeline CreatePipeline(const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath,
        VertexLayout vertexLayout = VertexLayout::PositionColor) const;

    // Factory for meshes: a vertex buffer + vertex count, NO index buffer -
    // see Mesh.h's non-indexed constructor. So callers never need direct
    // access to the VmaAllocator this Renderer owns internally. vertexData/
    // vertexDataSize describe a plain CPU-side array of Vertex (Vertex.h);
    // uploaded once via CreateDeviceLocalBuffer(), same as any other
    // static GPU-only buffer. debugName is optional/Editor-only - see
    // Buffer's constructor comment.
    Mesh CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const char* debugName = nullptr) const;

    // Indexed overload of CreateMesh() above - for a real imported mesh
    // whose CPU-side data already came with a triangle-index list (see
    // src/Assets/MeshData.h::indices), e.g. a decoded *.gta AssetType::Mesh
    // payload. `vertexData`/`vertexDataSize` describe a plain CPU-side array
    // of MeshVertex (MeshVertex.h) here, NOT Vertex - a Pipeline this Mesh
    // is drawn with must have been built with VertexLayout::PositionNormal
    // (see CreatePipeline() above) for the two to actually agree on layout.
    // Both the vertex AND index buffers are uploaded via
    // CreateDeviceLocalBuffer(), same as the non-indexed overload.
    // debugName is optional/Editor-only and applies to BOTH buffers.
    Mesh CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    // Factory for a static, immutable, CPU-authored texture (e.g. a decoded
    // PNG/JPEG asset - see src/Editor/AssetPreviewTexture.h, the first
    // consumer of this) built once from already-decoded RGBA8 pixel data,
    // as opposed to CreateRenderTexture() above (a resizable target
    // rendered into every frame). `pixelsRgba8` must be
    // width*height*4 tightly-packed bytes (e.g. straight out of
    // stbi_load(..., desired_channels=4)). debugName is optional/
    // Editor-only - see Buffer's constructor comment. See Texture2D.h.
    Texture2D CreateTexture2D(const void* pixelsRgba8, int width, int height, const char* debugName = nullptr) const;

    // Aggregate live-memory totals across every Buffer/RenderTexture this
    // Renderer has ever created and not yet destroyed - see
    // Memory/GpuMemoryTracker.h. O(1); safe to call every frame if desired
    // (e.g. the Editor's "Memory" panel header - see Panels/MemoryPanel.cpp).
    GpuMemoryTracker::Totals GetMemoryTotals() const;

    // Full per-object snapshot of every currently-live GPU resource - the
    // primitive behind a Unity-Memory-Profiler-style listing (see the
    // Editor's "Memory" panel, Panels/MemoryPanel.cpp). Carries no names
    // (see GetMemoryDebugName() below, Editor-only, for that).
    std::vector<GpuMemoryTracker::Entry> GetMemoryResources() const;

#if GTE_ENABLE_EDITOR
    // Editor-only: the human-readable debug name (if any) attached to a
    // still-live GPU resource handle via CreateBuffer()/CreateRenderTexture()/
    // CreateMesh()'s debugName parameter - forwards straight to
    // GpuMemoryTracker::GetDebugName(). Returns an empty string for an
    // unnamed/invalid/unknown handle. Compiled out entirely when
    // GTE_ENABLE_EDITOR is OFF, exactly like GpuMemoryTracker's own
    // debug-name storage (see AGENTS.md, "GPU Resource Memory Tracking").
    const std::string& GetMemoryDebugName(GpuResourceHandle handle) const;
#endif

    // The REAL, driver-reported memory usage/budget for every Vulkan memory
    // heap on this device (see VulkanAllocator::GetHeapBudgets()) - distinct
    // from GetMemoryTotals()/GetMemoryResources() above, which only tally
    // what THIS engine's own Buffer/RenderTexture objects requested.
    // Comparing the two together (see the Editor's "Memory" panel,
    // Panels/MemoryPanel.cpp) is how you tell whether GpuMemoryTracker
    // accounts for everything a real GPU tool (or Task Manager's dedicated
    // GPU memory column) would report, or whether something else - the
    // swapchain's own images, ImGui's Vulkan backend, driver/loader
    // overhead - isn't tracked yet.
    std::vector<VmaBudget> GetVmaHeapBudgets() const;

    // Read-only snapshot of this Renderer's core Vulkan handles + swapchain
    // format/image count, for an external Vulkan-based rendering backend
    // (e.g. Dear ImGui's Vulkan backend, owned by the Editor module) to
    // initialize its own pipeline against the exact same device/swapchain -
    // without Renderer ever needing to know that consumer exists, and
    // without exposing VulkanInstance/VulkanDevice/FramePresenter
    // themselves to callers outside this class.
    struct VulkanContextInfo {
        std::uint32_t apiVersion = 0;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        std::uint32_t graphicsQueueFamily = 0;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        std::uint32_t imageCount = 0;
        std::uint32_t minImageCount = 0;
    };
    VulkanContextInfo GetVulkanContextInfo() const;

    // Call when the window has been resized (e.g. from a WindowResized
    // event) - marks the swapchain dirty so the next Present() rebuilds it
    // at the new size instead of presenting into a stale-sized swapchain.
    void OnResize(int width, int height);

private:
    VulkanInstance m_instance;
    VulkanSurface m_surface;
    VulkanDevice m_device;

    // Computed once, right after m_device exists (VulkanDevice::
    // PickDepthFormat() needs its physical device) - see DepthFormat()
    // above. Declared here (not lower) so it's already initialized by the
    // time m_presenter/m_resources below are constructed, both of which
    // need it.
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

    // The ONE GpuMemoryTracker shared by both m_presenter (its per-
    // swapchain-image DepthBuffers) and m_resources (every Buffer/
    // RenderTexture/its own DepthBuffer created through it) - constructed
    // here (no dependencies of its own) so both collaborators below get the
    // SAME instance rather than two independent ones, which would silently
    // split GetMemoryTotals()/GetMemoryResources() across two disjoint
    // tallies instead of one accurate whole-engine picture. See
    // GpuMemoryTracker's own class comment for why this is owned via
    // shared_ptr in the first place.
    std::shared_ptr<GpuMemoryTracker> m_memoryTracker = std::make_shared<GpuMemoryTracker>();

    // Declared (and thus destroyed, per reverse-declaration-order RAII
    // teardown) right after m_device: relative order vs. m_presenter
    // doesn't matter (the allocator never touches the swapchain), but it
    // MUST be destroyed before m_device/m_instance are, since VMA holds
    // handles derived from both, and it must outlive m_resources
    // (declared after it below), which uses it.
    VulkanAllocator m_allocator;

    // Swapchain + per-frame sync objects + the actual Present()/
    // RenderOffscreen() Vulkan recording/submission - see FramePresenter.h.
    FramePresenter m_presenter;

    // Buffer/RenderTexture/Pipeline/Mesh factories + GPU memory tracking +
    // ImmediateSubmit() - see GpuResourceFactory.h.
    GpuResourceFactory m_resources;

    // This frame's clear color + queued Submit() draw list - see
    // FrameRecorder.h.
    FrameRecorder m_frameRecorder;
};

} // namespace gte
