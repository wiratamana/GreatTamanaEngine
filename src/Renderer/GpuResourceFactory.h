#pragma once

#include "Buffer.h"
#include "ComputePipeline.h"
#include "MaterialTexture.h"
#include "Memory/GpuMemoryTracker.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderTexture.h"
#include "Texture2D.h"
#include "Vulkan/VulkanAllocator.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// Owns everything needed to manufacture GPU resources (buffers, off-screen
// render textures, pipelines, meshes) on behalf of Renderer, plus the
// bookkeeping (GpuMemoryTracker) and one-off command submission
// (ImmediateSubmit()) those factories need - see the individual method
// comments (moved here verbatim from the old Renderer.h/.cpp) for the
// reasoning behind each one.
//
// Does NOT own the VkDevice/VmaAllocator/VkQueue passed in - all three must
// outlive this object (same non-ownership convention as VulkanSwapchain/
// VulkanAllocator not owning the instance/device/surface passed to them).
// DOES own its own VkCommandPool (for ImmediateSubmit()), entirely separate
// from FramePresenter's per-frame/offscreen command buffers, so a resource
// upload (e.g. CreateDeviceLocalBuffer() at load time) never contends with
// in-flight presentation/offscreen work.
class GpuResourceFactory {
public:
    GpuResourceFactory(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator, VkQueue graphicsQueue,
        std::uint32_t graphicsQueueFamily, VkFormat depthFormat, std::shared_ptr<GpuMemoryTracker> memoryTracker);
    ~GpuResourceFactory();

    GpuResourceFactory(const GpuResourceFactory&) = delete;
    GpuResourceFactory& operator=(const GpuResourceFactory&) = delete;

    GpuResourceFactory(GpuResourceFactory&& other) noexcept;
    GpuResourceFactory& operator=(GpuResourceFactory&& other) noexcept;

    // See Renderer::CreateRenderTexture(). `format` here must already be
    // fully resolved - VK_FORMAT_UNDEFINED-as-"match ColorFormat()" is
    // resolved by Renderer itself, since only Renderer (via FramePresenter)
    // knows ColorFormat(). depthDebugName optionally names the companion
    // DepthBuffer separately from the color image's debugName - see
    // RenderTexture's constructor comment. `allowStorageImageAccess`
    // (default false) opts the returned RenderTexture's color image into
    // VK_IMAGE_USAGE_STORAGE_BIT (an `RWTexture` - see
    // COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md) - when true, this
    // method first confirms `format` actually supports
    // VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT via
    // Vulkan/FormatCapabilities.h's SupportsStorageImageUsage() and throws
    // std::runtime_error loudly if it doesn't, rather than silently
    // creating a RenderTexture a compute shader can't actually bind as a
    // storage image.
    RenderTexture CreateRenderTexture(int width, int height, VkFormat format, const char* debugName,
        const char* depthDebugName = nullptr, bool allowStorageImageAccess = false) const;

    // See Renderer::CreateBuffer().
    Buffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage,
        const char* debugName = nullptr) const;

    // See Renderer::CreateDeviceLocalBuffer().
    Buffer CreateDeviceLocalBuffer(
        const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* debugName = nullptr) const;

    // See Renderer::CreateStructuredBuffer(). A thin, self-documenting
    // wrapper over CreateBuffer() above, for the `RWStructuredBuffer`/
    // `StructuredBuffer` compute-shader resource kinds (see
    // COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md) - both map onto
    // the SAME underlying Vulkan buffer/descriptor type
    // (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); the read-only-vs-read-write
    // distinction is enforced entirely at the GLSL (`readonly buffer` vs.
    // plain `buffer`) and render-graph (ComputeShaderRead vs.
    // ComputeShaderWrite - see Phase 5) levels, never by a different
    // Vulkan object here. `elementStride`/`elementCount` are plain
    // bookkeeping (size = elementStride * elementCount) - this does NOT
    // introduce a typed/templated buffer wrapper; the returned Buffer is
    // exactly as untyped as ever. Always ORs in
    // VK_BUFFER_USAGE_STORAGE_BUFFER_BIT (plus VK_BUFFER_USAGE_TRANSFER_DST_BIT
    // when `memoryUsage == BufferMemoryUsage::GpuOnly`, mirroring
    // CreateDeviceLocalBuffer()'s own convention, so a GPU-only structured
    // buffer can still be initialized once via a caller-driven staging
    // upload). `extraUsage` (default 0) lets a caller OR in additional
    // usage flags this buffer also needs (e.g.
    // VK_BUFFER_USAGE_INDIRECT_COMMAND_BIT for an indirect-draw buffer
    // that's ALSO written as a plain RWStructuredBuffer by a culling
    // compute shader - see the companion GPU-driven-rendering document)
    // without needing a second, near-duplicate factory method.
    Buffer CreateStructuredBuffer(VkDeviceSize elementStride, std::uint32_t elementCount,
        BufferMemoryUsage memoryUsage, VkBufferUsageFlags extraUsage = 0, const char* debugName = nullptr) const;

    // See Renderer::ImmediateSubmit().
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& recordFn) const;

    // See Renderer::CreatePipeline(). `colorFormat` here is always exactly
    // Renderer::ColorFormat(), passed in by Renderer since only Renderer
    // knows it. `vertexLayout` defaults to VertexLayout::PositionColor -
    // see Pipeline.h. `useMaterialTexture`, when true, passes this
    // factory's own persistent MaterialDescriptorSetLayout() through to
    // Pipeline's constructor - meaningful (and expected to be true) only
    // when vertexLayout is VertexLayout::PositionNormalUv.
    Pipeline CreatePipeline(VkFormat colorFormat, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        bool useMaterialTexture = false) const;

    // See Renderer::CreateComputePipeline() (Phase 2 -
    // COMPUTE_PHASE2_PIPELINE_INFRASTRUCTURE_STRATEGY_v1.md). Builds a
    // ComputePipeline from a single compiled .comp SPIR-V binary -
    // `descriptorSetLayouts`/`pushConstantRange` are forwarded verbatim to
    // ComputePipeline's own constructor (see ComputePipeline.h for the full
    // reasoning behind both). Unlike CreatePipeline() above, this needs no
    // color/depth format at all - a compute pipeline has no
    // VkPipelineRenderingCreateInfo/render target of its own.
    ComputePipeline CreateComputePipeline(const std::string& shaderSpirvPath,
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
        std::optional<VkPushConstantRange> pushConstantRange = std::nullopt) const;

    // See Renderer::CreateMesh() (non-indexed overload).
    Mesh CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const char* debugName = nullptr) const;

    // See Renderer::CreateMesh() (indexed overload) - builds BOTH a vertex
    // buffer and an index buffer, one CreateDeviceLocalBuffer() upload each,
    // and returns a Mesh built via Mesh's indexed constructor.
    Mesh CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    // Like CreateMesh() (indexed overload) above, but the VERTEX buffer is
    // built as a host-visible, persistently-mapped BufferMemoryUsage::
    // CpuToGpu buffer (initialized with `vertexData` immediately, then
    // re-writable afterwards via Mesh::UpdateVertexData()) instead of an
    // immutable device-local one - for a rigged mesh whose vertex
    // positions/normals need to be re-uploaded every frame as its pose
    // animates (see Game::UpdateSkeletalAnimators(), src/Game/Game.cpp).
    // The INDEX buffer is still built via CreateDeviceLocalBuffer() exactly
    // like CreateMesh() - a mesh's triangle topology never changes as it
    // animates, only its vertex positions/normals do.
    Mesh CreateSkinnedMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;


    // See Renderer::CreateTexture2D(). `pixelsRgba8` must be
    // width*height*4 tightly-packed bytes (e.g. straight out of
    // stbi_load(..., desired_channels=4)) - uploaded via a temporary
    // staging Buffer + ImmediateSubmit(), the same pattern
    // CreateDeviceLocalBuffer() above already uses. `allowStorageImageAccess`
    // (default false) opts the returned Texture2D's image into
    // VK_IMAGE_USAGE_STORAGE_BIT (an `RWTexture`) - when true, this method
    // first confirms Texture2D's own fixed VK_FORMAT_R8G8B8A8_UNORM format
    // actually supports VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT via
    // Vulkan/FormatCapabilities.h's SupportsStorageImageUsage() and throws
    // std::runtime_error loudly if it doesn't - same discipline as
    // CreateRenderTexture() above.
    Texture2D CreateTexture2D(const void* pixelsRgba8, int width, int height, const char* debugName = nullptr,
        bool allowStorageImageAccess = false) const;

    // The ONE descriptor-set-layout (a single combined-image-sampler,
    // fragment stage, set = 0 binding = 0) every VertexLayout::
    // PositionNormalUv Pipeline is built with (see CreatePipeline()'s
    // `useMaterialTexture` above) AND every MaterialTexture's own
    // VkDescriptorSet (see CreateMaterialTexture2D() below) is allocated
    // against - created once, for this factory's entire lifetime, in the
    // constructor, so any Pipeline/MaterialTexture pair built through this
    // factory is always binding-compatible with each other.
    VkDescriptorSetLayout MaterialDescriptorSetLayout() const noexcept { return m_materialSetLayout; }

    // Like CreateTexture2D() above, but ALSO allocates (from this
    // factory's own persistent m_materialDescriptorPool) and writes a
    // VkDescriptorSet - built against MaterialDescriptorSetLayout() above -
    // pointing at the freshly-created Texture2D's view/sampler, bundled
    // together as a MaterialTexture (see Renderer/MaterialTexture.h). This
    // is what Game::EnsureMeshAsset() uses for a PMX material's diffuse
    // texture (src/Game/Game.cpp), as opposed to CreateTexture2D() above,
    // which the Editor's Inspector preview (AssetPreviewTexture.h) uses for
    // a texture that's only ever displayed via ImGui::Image() (its own,
    // separate ImGui-owned descriptor set), never sampled by one of this
    // engine's own Pipelines.
    MaterialTexture CreateMaterialTexture2D(
        const void* pixelsRgba8, int width, int height, const char* debugName = nullptr) const;

    // Phase 3 (COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md) -
    // allocates one VkDescriptorSet from this factory's own persistent
    // m_computeDescriptorPool against `layout` (built via
    // Vulkan/DescriptorSetLayoutBuilder.h). Wrap the result in a
    // ComputeDescriptorSet (see Renderer/ComputeDescriptorSet.h) and call
    // its own Rewrite() to actually point it at real buffer/image
    // resources before first use. Like m_materialDescriptorPool's own
    // sets (see MaterialTexture.h's own comment), a set allocated here is
    // NEVER individually freed - only the whole m_computeDescriptorPool at
    // once, when this factory itself is destroyed.
    VkDescriptorSet AllocateComputeDescriptorSet(VkDescriptorSetLayout layout) const;

    // See Renderer::GetMemoryTotals()/GetMemoryResources().
    GpuMemoryTracker::Totals GetMemoryTotals() const;
    std::vector<GpuMemoryTracker::Entry> GetMemoryResources() const;

#if GTE_ENABLE_EDITOR
    // See Renderer::GetMemoryDebugName() - Editor-only, forwards straight to
    // GpuMemoryTracker::GetDebugName(). Compiled out entirely when
    // GTE_ENABLE_EDITOR is OFF, same as GpuMemoryTracker's own debug-name
    // storage (see AGENTS.md, "GPU Resource Memory Tracking").
    const std::string& GetMemoryDebugName(GpuResourceHandle handle) const;
#endif

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;

    // Needed purely for the storage-image capability check
    // (Vulkan/FormatCapabilities.h's SupportsStorageImageUsage()) that
    // CreateRenderTexture()/CreateTexture2D() perform when
    // `allowStorageImageAccess` is requested - see
    // COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md. Not owned; must
    // outlive this factory, same non-ownership convention as m_device/
    // m_allocator/m_graphicsQueue above.
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    // The single depth format every RenderTexture/Pipeline this factory
    // creates is built against - always exactly Renderer::DepthFormat()
    // (VulkanDevice::PickDepthFormat()), stored once here rather than
    // threaded through every CreateRenderTexture()/CreatePipeline() call
    // (unlike colorFormat, which a caller can deliberately override per
    // RenderTexture - see CreateRenderTexture() above - depth never needs
    // that: it's never sampled/displayed, so there's no reason for it to
    // ever differ from the one shared format). See AGENTS.md ("Render
    // Target Format Matching").
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

    // Owned via shared_ptr (not by value) so it can be handed out to every
    // Buffer/RenderTexture this factory creates without any risk of
    // dangling if this factory (or the Renderer owning it) is later moved -
    // see GpuMemoryTracker's class comment.
    std::shared_ptr<GpuMemoryTracker> m_memoryTracker;

    // Dedicated to ImmediateSubmit() - never shared with FramePresenter's
    // own per-frame/offscreen command buffers, so a buffer/mesh upload can
    // never contend with in-flight presentation work.
    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    // See MaterialDescriptorSetLayout()/CreateMaterialTexture2D() above -
    // both created once in the constructor, destroyed together in
    // Destroy(). m_materialDescriptorPool is sized generously (see
    // GpuResourceFactory.cpp) for every material texture this process is
    // ever likely to load; individual VkDescriptorSets allocated from it
    // are never individually freed (see MaterialTexture.h's own comment).
    VkDescriptorSetLayout m_materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_materialDescriptorPool = VK_NULL_HANDLE;

    // Phase 3 (COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md) - a
    // SECOND, dedicated descriptor pool for compute-shaped descriptor
    // types (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER/STORAGE_IMAGE, plus
    // COMBINED_IMAGE_SAMPLER for a compute shader's own plain `Texture`
    // reads - see AllocateComputeDescriptorSet() above). Deliberately
    // NEVER shared with m_materialDescriptorPool above - different
    // descriptor type requirements, and keeping them separate means a
    // compute-heavy feature can never exhaust the material-texture pool's
    // budget or vice versa. Created once in the constructor, destroyed in
    // Destroy() - sets allocated from it are never individually freed,
    // same convention as m_materialDescriptorPool.
    VkDescriptorPool m_computeDescriptorPool = VK_NULL_HANDLE;
};

} // namespace gte
