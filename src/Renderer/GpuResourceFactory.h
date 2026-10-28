#pragma once

#include "AbandonedWork.h"
#include "Buffer.h"
#include "GpuWaitPolicy.h"
#include "MaterialImageRole.h"
#include "MaterialTexture.h"
#include "Memory/GpuMemoryTracker.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderTexture.h"
#include "Texture2D.h"
#include "TextureArray2D.h"
#include "UploadChunker.h"
#include "VolumeTexture.h"
#include "Vulkan/BoundedKeySet.h"
#include "Vulkan/DescriptorPoolGrowth.h"
#include "Vulkan/VulkanAllocator.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

class ComputePipeline;
class DescriptorSet;
class VulkanDevice;

// Factory for creating and managing GPU resources (buffers, textures, pipelines, meshes).
class GpuResourceFactory {
public:
    // `vulkanDevice` and `abandoned` must outlive this object.
    GpuResourceFactory(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator,
        std::uint32_t graphicsQueueFamily, VkFormat depthFormat, std::shared_ptr<GpuMemoryTracker> memoryTracker,
        VulkanDevice& vulkanDevice, AbandonedWork& abandoned);
    ~GpuResourceFactory();

    GpuResourceFactory(const GpuResourceFactory&) = delete;
    GpuResourceFactory& operator=(const GpuResourceFactory&) = delete;

    GpuResourceFactory(GpuResourceFactory&&) = delete;
    GpuResourceFactory& operator=(GpuResourceFactory&&) = delete;

    // Render textures - `format` is the engine's PixelFormat, never a raw VkFormat.
    RenderTexture CreateRenderTexture(int width, int height, PixelFormat format, const char* debugName,
        const char* depthDebugName = nullptr, bool allowStorageImageAccess = false,
        bool allowDepthSampledAccess = false, bool createDepthCompanion = true,
        bool createColorImage = true, bool transferSrc = false, bool transferDst = false) const;

    // Buffers - `usage`/`extraUsage` use the engine's BufferUsage bitmask, never raw Vulkan flags.
    Buffer CreateBuffer(uint64_t size, BufferUsage usage, BufferMemoryUsage memoryUsage,
        const char* debugName = nullptr) const;

    Buffer CreateDeviceLocalBuffer(
        const void* data, uint64_t size, BufferUsage usage, const char* debugName = nullptr) const;

    // Records, submits and waits (bounded) for one command buffer. Throws on failure. When the wait fails the GPU
    // may still run it: the command buffer, its fence and `inFlight` are parked in the abandoned work.
    void SubmitOneShot(BlockSite site, const char* detail, const std::function<void(VkCommandBuffer)>& recordFn,
        std::span<const std::shared_ptr<void>> inFlight = {}) const;

    // Pipelines
    Pipeline CreatePipeline(VkFormat colorFormat, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        bool useMaterialTexture = false, const char* debugName = nullptr,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE) const;

    Pipeline CreatePipeline(std::span<const VkFormat> colorFormats, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        bool useMaterialTexture = false, const char* debugName = nullptr,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE) const;

    // Cluster draw pipeline: set 0 = material set when `useMaterialSet`, set 1 = `sceneServicesSetLayout` when
    // non-null, set 2 = buffer set. Empty `colorFormats` builds a depth-only pipeline.
    Pipeline CreateClusterPipeline(std::span<const VkFormat> colorFormats, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialSet, bool cullBackFaces,
        const char* debugName, VkDescriptorSetLayout sceneServicesSetLayout) const;

    ComputePipeline CreateComputePipeline(const std::string& shaderSpirvPath,
        const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts = {},
        std::optional<VkPushConstantRange> pushConstantRange = std::nullopt) const;

    // Meshes
    Mesh CreateMesh(const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount,
        const char* debugName = nullptr) const;

    Mesh CreateMesh(const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount,
        const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    Mesh CreateSkinnedMesh(const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount,
        const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    std::shared_ptr<Buffer> CreateSharedMeshVertexBuffer(
        const void* vertexData, uint64_t vertexDataSize, const char* debugName = nullptr) const;

    std::shared_ptr<Buffer> CreateSharedSkinnedMeshVertexBuffer(
        const void* vertexData, uint64_t vertexDataSize, const char* debugName = nullptr) const;

    Mesh CreateMeshFromSharedVertexBuffer(std::shared_ptr<Buffer> sharedVertexBuffer, std::uint32_t vertexCount,
        const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    // Textures
    Texture2D CreateTexture2D(const Texture2DUploadDesc& desc) const;

    // Uploads every image with a mip chain (sRGB for base color, UNORM otherwise). Images share staging buffers,
    // command buffers and submits. Index-aligned with `uploads`; an entry is null when that upload is invalid.
    // Throws on a GPU failure; none of the images of the failed chunk are returned.
    std::vector<std::shared_ptr<Texture2D>> CreateMaterialImages(std::span<const MaterialImageUpload> uploads) const;

    // Finished texture uploads since creation.
    GpuUploadStats UploadStats() const noexcept;

    VolumeTexture CreateVolumeTexture(
        int width, int height, int depth, PixelFormat format, const char* debugName = nullptr) const;

    TextureArray2D CreateTextureArray(int width, int height, int arrayLayers, PixelFormat format, bool hasDepth,
        bool isCubemap, bool allowStorageImageAccess = false, const char* debugName = nullptr) const;

    // Descriptors and Materials
    VkDescriptorSetLayout BufferDescriptorSetLayout() const noexcept { return m_bufferSetLayout; }

    // A new material (own params block and set) over `images`; null entries use the default image of the slot.
    MaterialTexture CreateMaterialVariant(const MaterialImages& images, const MaterialParams& params) const;

    // Allocates from the compute descriptor pool set; adds at most one pool per call. Throws when no pool fits.
    // Failures log once per failing layout (and once per exhaustion), never on every call.
    DescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout) const;

    // Tells the pool accounting what one set of `layout` costs. Re-registering a layout overwrites its cost.
    void RegisterLayoutCost(VkDescriptorSetLayout layout, const PoolCapacity& cost) const;

    // Drops the remembered state (does-not-fit, cost) of a layout that is about to be destroyed.
    void ForgetComputeLayout(VkDescriptorSetLayout layout) const noexcept;

    // Returns a set to the pool it came from. Unknown sets are ignored. The set must not be in use by the GPU.
    void FreeDescriptorSet(VkDescriptorSet descriptorSet) const noexcept;

    // Pool counts for the GPU Memory panel: capacity, sets and descriptors in use, peak use.
    DescriptorPoolStats DescriptorStats() const;

    // Memory Tracking
    GpuMemoryTracker::Totals GetMemoryTotals() const;
    std::vector<GpuMemoryTracker::Entry> GetMemoryResources() const;
    std::shared_ptr<GpuMemoryTracker> GetMemoryTracker() const noexcept { return m_memoryTracker; }

private:
    void Destroy() noexcept;

    // Transient command buffer for SubmitOneShot().
    VkCommandBuffer AllocateTransientCommandBuffer() const;

    // Reusable fences for one-shot submits; null when a fence cannot be created.
    VkFence AcquireFence() const;
    void ReleaseFence(VkFence fence) const noexcept;

    // Creates one compute descriptor pool; VK_NULL_HANDLE on failure.
    VkDescriptorPool CreateComputeDescriptorPool(const PoolCapacity& capacity) const noexcept;

    // Uploads one chunk with one staging buffer, one submit and one wait. Throws; nothing leaks on failure.
    std::vector<Texture2D> UploadTexture2DChunk(std::span<const Texture2DUploadDesc> descs) const;

    mutable std::atomic<std::uint64_t> m_uploadSubmitCount{ 0 };
    mutable std::atomic<std::uint64_t> m_uploadImageCount{ 0 };
    mutable std::atomic<std::uint64_t> m_uploadStagingBytes{ 0 };

    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VulkanDevice* m_vulkanDevice = nullptr;
    AbandonedWork& m_abandoned;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

    std::shared_ptr<GpuMemoryTracker> m_memoryTracker;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    mutable std::mutex m_commandPoolMutex; // Command pool is externally synchronized; never held across a wait.

    mutable std::mutex m_fencePoolMutex;
    mutable std::vector<VkFence> m_fencePool;

    // Material set layout: 0 = base color, 1 = MaterialParams, 2 = normal, 3 = metallic-roughness, 4 = occlusion.
    VkDescriptorSetLayout m_materialSetLayout = VK_NULL_HANDLE;
    std::shared_ptr<MaterialSetPool> m_materialSets;

    // 1x1 image per MaterialImageRole for empty material slots; created on first use.
    std::shared_ptr<Texture2D> DefaultMaterialImage(MaterialImageRole role) const;
    mutable std::mutex m_defaultMaterialImageMutex;
    mutable std::array<std::shared_ptr<Texture2D>, kMaterialImageRoleCount> m_defaultMaterialImages;

    VkDescriptorSetLayout m_bufferSetLayout = VK_NULL_HANDLE; // 0 = visible list, 1 = instances.

    // Compute descriptor pools (newest last), their accounting and the live sets; guarded by the mutex.
    struct ComputeSetInfo {
        std::uint32_t pool = 0;
        PoolCapacity cost;
    };
    mutable std::mutex m_computePoolMutex;
    mutable std::vector<VkDescriptorPool> m_computePools;
    mutable PoolCapacity m_lastComputePoolCapacity;
    mutable DescriptorBudget m_computeBudget;
    mutable std::unordered_map<VkDescriptorSet, ComputeSetInfo> m_computeSets;
    mutable std::unordered_map<VkDescriptorSetLayout, PoolCapacity> m_layoutCosts;
    mutable BoundedKeySet<VkDescriptorSetLayout, 64> m_unsatisfiableLayouts;
    mutable bool m_unsatisfiableOverflowLogged = false;
    mutable bool m_poolCapLogged = false;
};

} // namespace gte
