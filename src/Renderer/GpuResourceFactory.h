#pragma once

#include "Buffer.h"
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
    GpuResourceFactory(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator, VkQueue graphicsQueue,
        std::uint32_t graphicsQueueFamily, VkFormat depthFormat, std::shared_ptr<GpuMemoryTracker> memoryTracker,
        VulkanDevice& vulkanDevice);
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

    // Transient command buffer helpers
    VkCommandBuffer AllocateTransientCommandBuffer() const;
    void FreeTransientCommandBuffer(VkCommandBuffer cmd) const noexcept;

    // Pipelines
    Pipeline CreatePipeline(VkFormat colorFormat, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        bool useMaterialTexture = false, const char* debugName = nullptr, bool useInstanceBuffer = false,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE) const;

    Pipeline CreatePipeline(std::span<const VkFormat> colorFormats, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        bool useMaterialTexture = false, const char* debugName = nullptr, bool useInstanceBuffer = false,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE) const;

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
    VkDescriptorSetLayout InstanceBufferDescriptorSetLayout() const noexcept { return m_instanceBufferSetLayout; }

    // Uploads `pixelsRgba8` as an sRGB base-color image with a mip chain and wraps it in a material.
    MaterialTexture CreateMaterialTexture2D(const void* pixelsRgba8, int width, int height,
        const MaterialParams& params = {}, const char* debugName = nullptr) const;

    // A new material (own params block and descriptor set) over an image that already exists.
    MaterialTexture CreateMaterialVariant(std::shared_ptr<Texture2D> image, const MaterialParams& params) const;

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

    // Creates one compute descriptor pool; VK_NULL_HANDLE on failure.
    VkDescriptorPool CreateComputeDescriptorPool(const PoolCapacity& capacity) const noexcept;

    // Records, submits and waits on one command buffer. Logs and throws when the wait fails.
    void ImmediateSubmitInternal(const std::function<void(VkCommandBuffer)>& recordFn) const;

    // Uploads one chunk with one staging buffer, one submit and one wait. Throws; nothing leaks on failure.
    std::vector<Texture2D> UploadTexture2DChunk(std::span<const Texture2DUploadDesc> descs) const;

    mutable std::atomic<std::uint64_t> m_uploadSubmitCount{ 0 };
    mutable std::atomic<std::uint64_t> m_uploadImageCount{ 0 };
    mutable std::atomic<std::uint64_t> m_uploadStagingBytes{ 0 };

    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VulkanDevice* m_vulkanDevice = nullptr;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;

    std::shared_ptr<GpuMemoryTracker> m_memoryTracker;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_materialDescriptorPool = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_instanceBufferSetLayout = VK_NULL_HANDLE;

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
