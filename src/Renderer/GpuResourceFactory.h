#pragma once

#include "Buffer.h"
#include "Compute/ComputeDescriptorSet.h"
#include "Compute/ComputePipeline.h"
#include "MaterialTexture.h"
#include "Memory/GpuMemoryTracker.h"
#include "Mesh.h"
#include "Pipeline.h"
#include "RenderTexture.h"
#include "Texture2D.h"
#include "TextureArray2D.h"
#include "VolumeTexture.h"
#include "Vulkan/VulkanAllocator.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte {

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

    // Render textures - `format` is this engine's own PixelFormat (never a
    // raw VkFormat); translated internally wherever Vulkan actually needs
    // it (the real VkFormat constructors of RenderTexture/VolumeTexture/
    // TextureArray2D themselves - see each class's own constructor body).
    RenderTexture CreateRenderTexture(int width, int height, PixelFormat format, const char* debugName,
        const char* depthDebugName = nullptr, bool allowStorageImageAccess = false,
        bool allowDepthSampledAccess = false, bool createDepthCompanion = true,
        bool createColorImage = true) const;

    // Buffers
    Buffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage,
        const char* debugName = nullptr) const;

    Buffer CreateDeviceLocalBuffer(
        const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* debugName = nullptr) const;

    Buffer CreateStructuredBuffer(VkDeviceSize elementStride, std::uint32_t elementCount,
        BufferMemoryUsage memoryUsage, VkBufferUsageFlags extraUsage = 0, const char* debugName = nullptr) const;

    Buffer CreateGpuSkinningTargetBuffer(VkDeviceSize size, const char* debugName = nullptr) const;

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
    Mesh CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const char* debugName = nullptr) const;

    Mesh CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    Mesh CreateSkinnedMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
        const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    std::shared_ptr<Buffer> CreateSharedMeshVertexBuffer(
        const void* vertexData, VkDeviceSize vertexDataSize, const char* debugName = nullptr) const;

    std::shared_ptr<Buffer> CreateSharedSkinnedMeshVertexBuffer(
        const void* vertexData, VkDeviceSize vertexDataSize, const char* debugName = nullptr) const;

    Mesh CreateMeshFromSharedVertexBuffer(std::shared_ptr<Buffer> sharedVertexBuffer, std::uint32_t vertexCount,
        const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount,
        const char* debugName = nullptr) const;

    // Textures
    Texture2D CreateTexture2D(const void* pixelsRgba8, int width, int height, const char* debugName = nullptr,
        bool allowStorageImageAccess = false) const;

    VolumeTexture CreateVolumeTexture(
        int width, int height, int depth, PixelFormat format, const char* debugName = nullptr) const;

    TextureArray2D CreateTextureArray(int width, int height, int arrayLayers, PixelFormat format, bool hasDepth,
        bool isCubemap, bool allowStorageImageAccess = false, const char* debugName = nullptr) const;

    // Descriptors and Materials
    VkDescriptorSetLayout MaterialDescriptorSetLayout() const noexcept { return m_materialSetLayout; }
    VkDescriptorSetLayout InstanceBufferDescriptorSetLayout() const noexcept { return m_instanceBufferSetLayout; }

    MaterialTexture CreateMaterialTexture2D(
        const void* pixelsRgba8, int width, int height, const char* debugName = nullptr) const;

    ComputeDescriptorSet AllocateComputeDescriptorSet(VkDescriptorSetLayout layout) const;

    // Memory Tracking
    GpuMemoryTracker::Totals GetMemoryTotals() const;
    std::vector<GpuMemoryTracker::Entry> GetMemoryResources() const;
    std::shared_ptr<GpuMemoryTracker> GetMemoryTracker() const noexcept { return m_memoryTracker; }

private:
    void Destroy() noexcept;

    void ImmediateSubmitInternal(const std::function<void(VkCommandBuffer)>& recordFn) const;

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
    VkDescriptorPool m_computeDescriptorPool = VK_NULL_HANDLE;
};

} // namespace gte
