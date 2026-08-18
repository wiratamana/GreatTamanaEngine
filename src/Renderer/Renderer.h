#pragma once

#include "Buffer.h"
#include "Memory/GpuMemoryTracker.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderTarget.h"
#include "RenderTexture.h"
#include "Vulkan/VulkanAllocator.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanFrameSync.h"
#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanSurface.h"
#include "Vulkan/VulkanSwapchain.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gte {

class Window;

// Owns the entire Vulkan pipeline for a Window: instance, surface, device,
// swapchain, command buffers, and the per-frame synchronization objects
// needed to clear the swapchain and present it. Acquired piece-by-piece in
// the constructor (each piece itself RAII-owned - see Vulkan/*), released
// automatically in reverse order in the destructor.
//
// Public API is intentionally still just Clear()/Present(), matching the
// previous SDL_Renderer-backed version, so Game/main code did not need to
// change for this swap. Clear() only records the desired clear color;
// the actual clear happens as part of Present() (Vulkan has no equivalent
// of an immediate "clear now" call outside of a recorded command buffer).
//
// Also owns the machinery to render into an off-screen RenderTexture
// instead of the swapchain (RenderOffscreen()/CreateRenderTexture()) - the
// primitive behind Unity-style Editor "Game"/"Scene" panels: a camera
// renders into a RenderTexture, which the Editor then displays inside an
// ImGui::Image() panel, while a final/release build (no Editor compiled
// in) instead renders the same scene straight into the swapchain via
// Present(), fullscreen, with no Editor/ImGui involved at all. Callers
// (Game, a future Editor module, ...) never touch raw Vulkan handles for
// this - RenderTarget/RenderTexture are the abstraction boundary.
class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    Renderer(Renderer&& other) noexcept;
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
    RenderTexture CreateRenderTexture(int width, int height, VkFormat format = VK_FORMAT_UNDEFINED,
        const char* debugName = nullptr) const;

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
    // minimized window with no Editor - see RecordClearAndTransition).
    void BeginFrame();

    // Queues one draw call - a Pipeline plus the Mesh to draw with it - to
    // be recorded the next time this frame's contents are actually recorded
    // (whichever of RenderOffscreen()/Present() runs RecordClearAndTransition
    // first this frame). This is the seam that lets Game record draws
    // without ever touching a VkCommandBuffer or knowing Vulkan exists at
    // all: Game holds onto Pipeline/Mesh objects (built via
    // CreatePipeline()/CreateMesh()) and just calls this once per object it
    // wants drawn each frame - Renderer is the only thing that ever issues
    // the actual vkCmdBindPipeline/vkCmdBindVertexBuffers/vkCmdDraw calls.
    // Not persistent - must be called again every frame an object should be
    // drawn (there is no retained scene graph yet).
    void Submit(const Pipeline& pipeline, const Mesh& mesh);

    // Factory for graphics pipelines, so callers never need direct access
    // to the VkDevice this Renderer owns internally, and always get a
    // pipeline built against the exact color format this Renderer actually
    // renders with (see ColorFormat() and AGENTS.md, "Render Target Format
    // Matching"). vertexShaderSpirvPath/fragmentShaderSpirvPath point at
    // compiled SPIR-V binaries - see cmake/CompileShaders.cmake, which
    // compiles src/Shaders/*.vert/*.frag into "<exe dir>/shaders/*.spv" at
    // build time.
    Pipeline CreatePipeline(const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath) const;

    // Factory for meshes (currently: a vertex buffer + vertex count, no
    // index buffer - see Mesh.h), so callers never need direct access to
    // the VmaAllocator this Renderer owns internally. vertexData/
    // vertexDataSize describe a plain CPU-side array of Vertex (Vertex.h);
    // uploaded once via CreateDeviceLocalBuffer(), same as any other
    // static GPU-only buffer. debugName is optional/Editor-only - see
    // Buffer's constructor comment.
    Mesh CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const char* debugName = nullptr) const;

    // Aggregate live-memory totals across every Buffer/RenderTexture this
    // Renderer has ever created and not yet destroyed - see
    // Memory/GpuMemoryTracker.h. O(1); safe to call every frame if desired
    // (e.g. a future Editor "Memory" panel's header).
    GpuMemoryTracker::Totals GetMemoryTotals() const;

    // Full per-object snapshot of every currently-live GPU resource - the
    // primitive behind a Unity-Memory-Profiler-style listing. Carries no
    // names (see GpuMemoryTracker::GetDebugName(), Editor-only, for that).
    std::vector<GpuMemoryTracker::Entry> GetMemoryResources() const;

    // Read-only snapshot of this Renderer's core Vulkan handles + swapchain
    // format/image count, for an external Vulkan-based rendering backend
    // (e.g. Dear ImGui's Vulkan backend, owned by the Editor module) to
    // initialize its own pipeline against the exact same device/swapchain -
    // without Renderer ever needing to know that consumer exists, and
    // without exposing VulkanInstance/VulkanDevice/VulkanSwapchain
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
    static constexpr std::uint32_t kMaxFramesInFlight = 2;

    void CreateCommandObjects();
    void RecreateSwapchain();

    // Shared by Present() (target = current swapchain image) and
    // RenderOffscreen() (target = a RenderTexture): records the
    // undefined->color-attachment barrier, the dynamic-rendering
    // clear+recordExtra, and the final transition to `finalLayout`
    // (VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for the swapchain,
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL for a RenderTexture).
    void RecordClearAndTransition(VkCommandBuffer cmd, const RenderTarget& target, VkImageLayout finalLayout,
        const std::function<void(VkCommandBuffer)>& recordExtra);

    // One queued Submit() call's worth of plain Vulkan handles - deliberately
    // NOT a reference/pointer to the Pipeline/Mesh themselves (those are
    // owned by whoever called Submit(), typically Game, and must outlive
    // the RecordClearAndTransition() call that consumes this - which is
    // always true within a single frame, since Submit() is called from
    // Game::Render() and consumed later that same frame).
    struct DrawItem {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        std::uint32_t vertexCount = 0;
    };

    VulkanInstance m_instance;
    VulkanSurface m_surface;
    VulkanDevice m_device;
    // Declared (and thus destroyed, per reverse-declaration-order RAII
    // teardown) right after m_device and before m_swapchain: relative order
    // vs. m_swapchain doesn't matter (the allocator never touches it), but
    // it MUST be destroyed before m_device/m_instance are, since VMA holds
    // handles derived from both.
    VulkanAllocator m_allocator;
    VulkanSwapchain m_swapchain;

    // All per-frame/per-image semaphores and fences (see VulkanFrameSync) -
    // declared after m_swapchain (its constructor needs
    // m_swapchain.ImageCount()) so it is destroyed, in
    // reverse-declaration-order RAII teardown, before m_swapchain/
    // m_allocator/m_device/m_surface/m_instance, but after
    // m_memoryTracker/m_drawQueue/etc. below. Its per-swapchain-image
    // semaphores are rebuilt (RecreateRenderFinishedSemaphores()) whenever
    // the swapchain itself is recreated - see RecreateSwapchain().
    VulkanFrameSync m_frameSync;

    // Owned via shared_ptr (not by value) so it can be handed out to every
    // Buffer/RenderTexture this Renderer creates without any risk of
    // dangling if Renderer itself (and thus m_allocator) is later moved -
    // see GpuMemoryTracker's class comment. Declaration order relative to
    // the Vulkan objects above/below is irrelevant: this owns no Vulkan
    // handles itself, and shared_ptr keeps it alive as long as anything
    // (including a live Buffer/RenderTexture) still references it.
    std::shared_ptr<GpuMemoryTracker> m_memoryTracker = std::make_shared<GpuMemoryTracker>();

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kMaxFramesInFlight> m_commandBuffers{};

    // Separate command buffer for RenderOffscreen() (paired with
    // m_frameSync.OffscreenFence()), so off-screen rendering (Editor
    // panels) never contends with the swapchain's own per-frame-in-flight
    // command buffers/fences.
    VkCommandBuffer m_offscreenCommandBuffer = VK_NULL_HANDLE;

    std::uint32_t m_currentFrame = 0;

    float m_clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    bool m_resizeRequested = false;
    int m_pendingWidth = 0;
    int m_pendingHeight = 0;

    // This frame's queued Submit() calls - see Submit()/BeginFrame()/
    // RecordClearAndTransition. Cleared at the top of every frame
    // (BeginFrame()) AND immediately after being recorded
    // (RecordClearAndTransition), so a frame is never drawn twice (e.g.
    // RenderOffscreen() then Present() in the same Editor-build frame) and
    // never silently accumulates across frames where nothing consumes it.
    std::vector<DrawItem> m_drawQueue;
};

} // namespace gte
