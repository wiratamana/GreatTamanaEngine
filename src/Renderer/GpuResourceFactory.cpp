#include "GpuResourceFactory.h"

#include "../Core/Logging.h"
#include "GraphicsHandleRegistry.h" // RegisterDescriptorSet() - sets leave this file as engine handles.
#include "SceneServicesDescriptorSet.h" // gte::kSceneServiceSlotCount for the pool budget static_asserts.
#include "VulkanFormatInfo.h" // ToVkFormat() for pre-construction capability checks.
#include "Vulkan/DescriptorCost.h"
#include "Vulkan/DescriptorSetLayoutBuilder.h"
#include "Vulkan/FormatCapabilities.h"
#include "Vulkan/VulkanDevice.h"

#include <cassert>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gte {

GpuResourceFactory::GpuResourceFactory(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator,
    VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily, VkFormat depthFormat,
    std::shared_ptr<GpuMemoryTracker> memoryTracker, VulkanDevice& vulkanDevice)
    : m_device(device)
    , m_allocator(allocator)
    , m_graphicsQueue(graphicsQueue)
    , m_vulkanDevice(&vulkanDevice)
    , m_physicalDevice(physicalDevice)
    , m_depthFormat(depthFormat)
    , m_memoryTracker(std::move(memoryTracker))
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("GpuResourceFactory: vkCreateCommandPool failed");
    }

    // Shared material set layout: one combined image sampler, fragment stage.
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(m_device, &setLayoutInfo, nullptr, &m_materialSetLayout) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw std::runtime_error("GpuResourceFactory: vkCreateDescriptorSetLayout failed");
    }

    // Material sets are never freed individually; the whole pool dies in Destroy().
    constexpr std::uint32_t kMaxMaterialTextures = 4096;
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = kMaxMaterialTextures;

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.maxSets = kMaxMaterialTextures;
    poolCreateInfo.poolSizeCount = 1;
    poolCreateInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_device, &poolCreateInfo, nullptr, &m_materialDescriptorPool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw std::runtime_error("GpuResourceFactory: vkCreateDescriptorPool failed");
    }

    // First compute descriptor pool; more are added on demand (see AllocateComputeDescriptorSet()).
    constexpr PoolCapacity kInitialComputePool{ .sets = 256,
        .storageBuffers = 256,
        .storageImages = 128,
        .combinedSamplers = 128,
        .uniformBuffers = 64 };

    // Scene services for 2 views (kSceneServiceSlotCount samplers each) must fit the first pool.
    static_assert(gte::kSceneServiceSlotCount * 2 <= kInitialComputePool.combinedSamplers,
        "Scene services for 2 views must fit the first compute pool's sampler budget.");

    // Uniform buffers in the first pool: one per scene-services view (2) plus the asset preview and the bone viewer.
    constexpr std::uint32_t kKnownUniformBufferConsumers = 2 + 1 + 1;
    static_assert(kKnownUniformBufferConsumers <= kInitialComputePool.uniformBuffers,
        "The known uniform buffer consumers must fit the first compute pool.");

    const VkDescriptorPool firstComputePool = CreateComputeDescriptorPool(kInitialComputePool);
    if (firstComputePool == VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_materialDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw std::runtime_error("GpuResourceFactory: vkCreateDescriptorPool (compute) failed");
    }
    m_computePools.push_back(firstComputePool);
    m_lastComputePoolCapacity = kInitialComputePool;
    m_computeBudget.AddCapacity(kInitialComputePool);

    // Shared instance-buffer layout: one readonly storage buffer, vertex stage, set 0 binding 0.
    try {
        DescriptorSetLayoutBuilder instanceBufferLayoutBuilder(m_device);
        m_instanceBufferSetLayout = instanceBufferLayoutBuilder.AddStorageBuffer(/*binding=*/0, VK_SHADER_STAGE_VERTEX_BIT).Build();
    } catch (...) {
        vkDestroyDescriptorPool(m_device, firstComputePool, nullptr);
        m_computePools.clear();
        vkDestroyDescriptorPool(m_device, m_materialDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw;
    }
    m_layoutCosts.emplace(m_instanceBufferSetLayout, PoolCapacity{ .sets = 1, .storageBuffers = 1 });
}

GpuResourceFactory::~GpuResourceFactory()
{
    Destroy();
}

Buffer GpuResourceFactory::CreateGpuSkinningTargetBuffer(uint64_t size, const char* debugName) const
{
    return CreateBuffer(size, BufferUsage::StorageBuffer | BufferUsage::VertexBuffer,
        BufferMemoryUsage::GpuOnly, debugName);
}

void GpuResourceFactory::Destroy() noexcept
{
    if (m_materialDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_materialDescriptorPool, nullptr);
        m_materialDescriptorPool = VK_NULL_HANDLE;
    }
    for (const VkDescriptorPool pool : m_computePools) {
        vkDestroyDescriptorPool(m_device, pool, nullptr);
    }
    m_computePools.clear();
    m_computeSets.clear();
    if (m_materialSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        m_materialSetLayout = VK_NULL_HANDLE;
    }
    if (m_instanceBufferSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_instanceBufferSetLayout, nullptr);
        m_instanceBufferSetLayout = VK_NULL_HANDLE;
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
}

RenderTexture GpuResourceFactory::CreateRenderTexture(int width, int height, PixelFormat format, const char* debugName,
    const char* depthDebugName, bool allowStorageImageAccess, bool allowDepthSampledAccess,
    bool createDepthCompanion, bool createColorImage) const
{
    // The VkFormat is needed only for this capability check; the depth format is stored as VkFormat.
    if (allowStorageImageAccess && !SupportsStorageImageUsage(m_physicalDevice, ToVkFormat(format))) {
        throw std::runtime_error(
            "GpuResourceFactory::CreateRenderTexture: requested allowStorageImageAccess = true, but this "
            "physical device does not support VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT for the requested format.");
    }
    return RenderTexture(m_allocator, m_memoryTracker, m_device, width, height, format, FromVkFormat(m_depthFormat),
        debugName, depthDebugName, allowStorageImageAccess, allowDepthSampledAccess, createDepthCompanion,
        createColorImage);
}

Buffer GpuResourceFactory::CreateBuffer(
    uint64_t size, BufferUsage usage, BufferMemoryUsage memoryUsage, const char* debugName) const
{
    return Buffer(m_allocator, m_memoryTracker, size, usage, memoryUsage, debugName);
}

Buffer GpuResourceFactory::CreateStructuredBuffer(uint64_t elementStride, std::uint32_t elementCount,
    BufferMemoryUsage memoryUsage, BufferUsage extraUsage, const char* debugName) const
{
    const uint64_t size = elementStride * static_cast<uint64_t>(elementCount);
    BufferUsage usage = BufferUsage::StorageBuffer | extraUsage;
    if (memoryUsage == BufferMemoryUsage::GpuOnly) {
        // GpuOnly buffers need TransferDst so they can be filled by a staging upload.
        usage |= BufferUsage::TransferDst;
    }
    return CreateBuffer(size, usage, memoryUsage, debugName);
}

Buffer GpuResourceFactory::CreateDeviceLocalBuffer(
    const void* data, uint64_t size, BufferUsage usage, const char* debugName) const
{
    // Unnamed throwaway staging buffer.
    Buffer staging = CreateBuffer(size, BufferUsage::TransferSrc, BufferMemoryUsage::CpuToGpu);
    staging.Upload(data, static_cast<std::size_t>(size));

    Buffer deviceLocal =
        CreateBuffer(size, usage | BufferUsage::TransferDst, BufferMemoryUsage::GpuOnly, debugName);

    ImmediateSubmitInternal([&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion{};
        copyRegion.size = static_cast<VkDeviceSize>(size);
        vkCmdCopyBuffer(cmd, staging.Native(), deviceLocal.Native(), 1, &copyRegion);
    });
    // staging is destroyed here; ImmediateSubmitInternal already waited for the copy.

    return deviceLocal;
}

VkCommandBuffer GpuResourceFactory::AllocateTransientCommandBuffer() const
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("GpuResourceFactory::AllocateTransientCommandBuffer: vkAllocateCommandBuffers failed.");
    }
    return cmd;
}

void GpuResourceFactory::FreeTransientCommandBuffer(VkCommandBuffer cmd) const noexcept
{
    if (cmd != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    }
}

void GpuResourceFactory::ImmediateSubmitInternal(const std::function<void(VkCommandBuffer)>& recordFn) const
{
    VkCommandBuffer cmd = AllocateTransientCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        FreeTransientCommandBuffer(cmd);
        throw std::runtime_error("GpuResourceFactory::ImmediateSubmitInternal: vkBeginCommandBuffer failed.");
    }

    try {
        recordFn(cmd);
    } catch (...) {
        FreeTransientCommandBuffer(cmd);
        throw;
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        FreeTransientCommandBuffer(cmd);
        throw std::runtime_error("GpuResourceFactory::ImmediateSubmitInternal: vkEndCommandBuffer failed.");
    }

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        FreeTransientCommandBuffer(cmd);
        throw std::runtime_error("GpuResourceFactory::ImmediateSubmitInternal: vkCreateFence failed.");
    }

    if (m_vulkanDevice->SubmitToGraphicsQueue(submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(m_device, fence, nullptr);
        FreeTransientCommandBuffer(cmd);
        throw std::runtime_error("GpuResourceFactory::ImmediateSubmitInternal: vkQueueSubmit2 failed.");
    }

    vkWaitForFences(m_device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    vkDestroyFence(m_device, fence, nullptr);
    FreeTransientCommandBuffer(cmd);
}

Pipeline GpuResourceFactory::CreatePipeline(VkFormat colorFormat, const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture,
    const char* debugName, bool useInstanceBuffer, VkDescriptorSetLayout sceneServicesSetLayout) const
{
    const VkDescriptorSetLayout materialSetLayout = useMaterialTexture ? m_materialSetLayout : VK_NULL_HANDLE;
    const VkDescriptorSetLayout instanceBufferSetLayout = useInstanceBuffer ? m_instanceBufferSetLayout : VK_NULL_HANDLE;
    return Pipeline(m_device, colorFormat, m_depthFormat, vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
        materialSetLayout, debugName, instanceBufferSetLayout, sceneServicesSetLayout);
}

Pipeline GpuResourceFactory::CreatePipeline(std::span<const VkFormat> colorFormats,
    const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout,
    bool useMaterialTexture, const char* debugName, bool useInstanceBuffer,
    VkDescriptorSetLayout sceneServicesSetLayout) const
{
    const VkDescriptorSetLayout materialSetLayout = useMaterialTexture ? m_materialSetLayout : VK_NULL_HANDLE;
    const VkDescriptorSetLayout instanceBufferSetLayout = useInstanceBuffer ? m_instanceBufferSetLayout : VK_NULL_HANDLE;
    return Pipeline(m_device, colorFormats, m_depthFormat, vertexShaderSpirvPath, fragmentShaderSpirvPath,
        vertexLayout, materialSetLayout, debugName, instanceBufferSetLayout, sceneServicesSetLayout);
}

ComputePipeline GpuResourceFactory::CreateComputePipeline(const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
    std::optional<VkPushConstantRange> pushConstantRange) const
{
    ComputePipeline pipeline(m_device, shaderSpirvPath, descriptorSetLayouts, pushConstantRange);
    const std::span<const ReflectedDescriptorBinding> reflected = pipeline.ReflectedBindings();
    const std::vector<ReflectedDescriptorBinding> bindings(reflected.begin(), reflected.end());
    for (const DescriptorBindingSetGroup& group : GroupDescriptorBindingsBySet(bindings)) {
        RegisterLayoutCost(pipeline.ReflectedDescriptorSetLayout(group.set), DescriptorCostOf(group.bindings));
    }
    return pipeline;
}

Mesh GpuResourceFactory::CreateMesh(
    const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount, const char* debugName) const
{
    Buffer vertexBuffer =
        CreateDeviceLocalBuffer(vertexData, vertexDataSize, BufferUsage::VertexBuffer, debugName);
    return Mesh(std::move(vertexBuffer), vertexCount);
}

Mesh GpuResourceFactory::CreateMesh(const void* vertexData, uint64_t vertexDataSize, std::uint32_t vertexCount,
    const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount, const char* debugName) const
{
    Buffer vertexBuffer =
        CreateDeviceLocalBuffer(vertexData, vertexDataSize, BufferUsage::VertexBuffer, debugName);
    Buffer indexBuffer =
        CreateDeviceLocalBuffer(indexData, indexDataSize, BufferUsage::IndexBuffer, debugName);
    return Mesh(std::move(vertexBuffer), vertexCount, std::move(indexBuffer), indexCount);
}

Mesh GpuResourceFactory::CreateSkinnedMesh(const void* vertexData, uint64_t vertexDataSize,
    std::uint32_t vertexCount, const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
    const char* debugName) const
{
    // Host-visible, persistently mapped vertex buffer: rewritten every frame via Mesh::UpdateVertexData().
    Buffer vertexBuffer = CreateBuffer(vertexDataSize, BufferUsage::VertexBuffer, BufferMemoryUsage::CpuToGpu,
        debugName);
    vertexBuffer.Upload(vertexData, static_cast<std::size_t>(vertexDataSize));

    // Indices never change while a mesh animates: static, device-local.
    Buffer indexBuffer =
        CreateDeviceLocalBuffer(indexData, indexDataSize, BufferUsage::IndexBuffer, debugName);

    return Mesh(std::move(vertexBuffer), vertexCount, std::move(indexBuffer), indexCount);
}

std::shared_ptr<Buffer> GpuResourceFactory::CreateSharedMeshVertexBuffer(
    const void* vertexData, uint64_t vertexDataSize, const char* debugName) const
{
    Buffer vertexBuffer =
        CreateDeviceLocalBuffer(vertexData, vertexDataSize, BufferUsage::VertexBuffer, debugName);
    return std::make_shared<Buffer>(std::move(vertexBuffer));
}

std::shared_ptr<Buffer> GpuResourceFactory::CreateSharedSkinnedMeshVertexBuffer(
    const void* vertexData, uint64_t vertexDataSize, const char* debugName) const
{
    Buffer vertexBuffer = CreateBuffer(vertexDataSize, BufferUsage::VertexBuffer, BufferMemoryUsage::CpuToGpu,
        debugName);
    vertexBuffer.Upload(vertexData, static_cast<std::size_t>(vertexDataSize));
    return std::make_shared<Buffer>(std::move(vertexBuffer));
}

Mesh GpuResourceFactory::CreateMeshFromSharedVertexBuffer(std::shared_ptr<Buffer> sharedVertexBuffer,
    std::uint32_t vertexCount, const void* indexData, uint64_t indexDataSize, std::uint32_t indexCount,
    const char* debugName) const
{
    Buffer indexBuffer =
        CreateDeviceLocalBuffer(indexData, indexDataSize, BufferUsage::IndexBuffer, debugName);
    return Mesh(std::move(sharedVertexBuffer), vertexCount, std::move(indexBuffer), indexCount);
}

Texture2D GpuResourceFactory::CreateTexture2D(
    const void* pixelsRgba8, int width, int height, const char* debugName, bool allowStorageImageAccess) const
{
    if (allowStorageImageAccess && !SupportsStorageImageUsage(m_physicalDevice, VK_FORMAT_R8G8B8A8_UNORM)) {
        throw std::runtime_error(
            "GpuResourceFactory::CreateTexture2D: requested allowStorageImageAccess = true, but this physical "
            "device does not support VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT for VK_FORMAT_R8G8B8A8_UNORM.");
    }
    Texture2D texture(m_allocator, m_memoryTracker, m_device, width, height, debugName, allowStorageImageAccess);

    const auto safeWidth = static_cast<uint64_t>(texture.Width());
    const auto safeHeight = static_cast<uint64_t>(texture.Height());
    const uint64_t size = safeWidth * safeHeight * 4;

    // Unnamed throwaway staging buffer.
    Buffer staging = CreateBuffer(size, BufferUsage::TransferSrc, BufferMemoryUsage::CpuToGpu);
    staging.Upload(pixelsRgba8, static_cast<std::size_t>(size));

    ImmediateSubmitInternal([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toTransferDst{};
        toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst.image = texture.Image();
        toTransferDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toTransferDst.srcAccessMask = 0;
        toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
            nullptr, 1, &toTransferDst);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.imageOffset = { 0, 0, 0 };
        copyRegion.imageExtent = { static_cast<std::uint32_t>(texture.Width()),
            static_cast<std::uint32_t>(texture.Height()), 1 };
        vkCmdCopyBufferToImage(
            cmd, staging.Native(), texture.Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier toShaderRead = toTransferDst;
        toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &toShaderRead);
    });
    // staging is destroyed here; the copy has already completed.

    return texture;
}

VolumeTexture GpuResourceFactory::CreateVolumeTexture(
    int width, int height, int depth, PixelFormat format, const char* debugName) const
{
    if (!SupportsStorageImageUsage(m_physicalDevice, ToVkFormat(format))) {
        throw std::runtime_error(
            "GpuResourceFactory::CreateVolumeTexture: this physical device does not support "
            "VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT for the requested format.");
    }
    return VolumeTexture(m_allocator, m_memoryTracker, m_device, width, height, depth, format, debugName);
}

TextureArray2D GpuResourceFactory::CreateTextureArray(int width, int height, int arrayLayers, PixelFormat format,
    bool hasDepth, bool isCubemap, bool allowStorageImageAccess, const char* debugName) const
{
    // `format` must already be resolved; Undefined is handled in Renderer::CreateTextureArray().
    assert(format != PixelFormat::Undefined &&
        "GpuResourceFactory::CreateTextureArray requires an already-resolved concrete format - "
        "PixelFormat::Undefined resolution happens one layer up, in Renderer::CreateTextureArray()");

    // Vulkan cube-compatible images need a multiple of 6 layers and square faces.
    if (isCubemap && (arrayLayers < 6 || arrayLayers % 6 != 0)) {
        throw std::runtime_error(
            "GpuResourceFactory::CreateTextureArray: isCubemap requires arrayLayers to be a "
            "positive multiple of 6.");
    }
    if (isCubemap && width != height) {
        throw std::runtime_error(
            "GpuResourceFactory::CreateTextureArray: isCubemap requires width == height "
            "(Vulkan requires square faces for a cube-compatible image).");
    }

    // Only checked for color arrays; depth arrays skip it.
    if (allowStorageImageAccess && !hasDepth && !SupportsStorageImageUsage(m_physicalDevice, ToVkFormat(format))) {
        throw std::runtime_error(
            "GpuResourceFactory::CreateTextureArray: requested allowStorageImageAccess = true, but this "
            "physical device does not support VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT for the requested format.");
    }

    return TextureArray2D(m_allocator, m_memoryTracker, m_device, width, height, arrayLayers, format, hasDepth,
        isCubemap, allowStorageImageAccess, debugName);
}


MaterialTexture GpuResourceFactory::CreateMaterialTexture2D(
    const void* pixelsRgba8, int width, int height, const char* debugName) const
{
    Texture2D texture = CreateTexture2D(pixelsRgba8, width, height, debugName);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_materialDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_materialSetLayout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("GpuResourceFactory::CreateMaterialTexture2D: vkAllocateDescriptorSets failed "
            "(consider raising GpuResourceFactory.cpp's kMaxMaterialTextures).");
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texture.View();
    imageInfo.sampler = texture.Sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    // Registered once; the set is pool-owned and never freed or reallocated individually.
    return MaterialTexture{ std::move(texture), RegisterDescriptorSet(descriptorSet) };
}

VkDescriptorPool GpuResourceFactory::CreateComputeDescriptorPool(const PoolCapacity& capacity) const noexcept
{
    const VkDescriptorPoolSize sizes[4] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, capacity.storageBuffers },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, capacity.storageImages },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, capacity.combinedSamplers },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, capacity.uniformBuffers },
    };

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = capacity.sets;
    info.poolSizeCount = 4;
    info.pPoolSizes = sizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    return vkCreateDescriptorPool(m_device, &info, nullptr, &pool) == VK_SUCCESS ? pool : VK_NULL_HANDLE;
}

void GpuResourceFactory::RegisterLayoutCost(VkDescriptorSetLayout layout, const PoolCapacity& cost) const
{
    if (layout == VK_NULL_HANDLE) {
        return;
    }
    const std::scoped_lock lock(m_computePoolMutex);
    m_layoutCosts.insert_or_assign(layout, cost); // Overwrites a stale entry when a driver reuses the handle.
}

DescriptorPoolStats GpuResourceFactory::DescriptorStats() const
{
    const std::scoped_lock lock(m_computePoolMutex);
    return DescriptorPoolStats{ m_computePools.size(), m_computeBudget.Capacity(), m_computeBudget.Used(),
        m_computeBudget.HighWater() };
}

ComputeDescriptorSet GpuResourceFactory::AllocateComputeDescriptorSet(VkDescriptorSetLayout layout) const
{
    const std::scoped_lock lock(m_computePoolMutex);

    // A layout that already failed on a fresh pool is refused without growing anything (and without logging again).
    if (m_unsatisfiableLayouts.Contains(layout)) {
        throw std::runtime_error("GpuResourceFactory::AllocateComputeDescriptorSet: layout does not fit any pool.");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    const auto costIt = m_layoutCosts.find(layout);
    const PoolCapacity cost = costIt != m_layoutCosts.end() ? costIt->second : PoolCapacity{ .sets = 1 };

    // Newest pool first; freed sets may also have opened room in an older one.
    const auto tryAllocate = [&]() -> std::optional<VkDescriptorSet> {
        for (std::size_t i = m_computePools.size(); i-- > 0;) {
            allocInfo.descriptorPool = m_computePools[i];
            VkDescriptorSet set = VK_NULL_HANDLE;
            const VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &set);
            if (result == VK_SUCCESS) {
                m_computeSets.emplace(set, ComputeSetInfo{ static_cast<std::uint32_t>(i), cost });
                m_computeBudget.OnAllocated(cost);
                return set;
            }
            if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
                throw std::runtime_error("GpuResourceFactory::AllocateComputeDescriptorSet: vkAllocateDescriptorSets failed.");
            }
        }
        return std::nullopt;
    };

    std::optional<VkDescriptorSet> set = tryAllocate();
    bool freshPoolFailed = false; // A newly added pool could not hold this layout either.
    bool poolCapReached = false;
    if (!set.has_value()) {
        // One new pool per request. If the fresh pool cannot hold the layout, more pools will not help.
        const std::optional<PoolCapacity> next = NextPoolCapacity(m_lastComputePoolCapacity, m_computePools.size());
        if (next.has_value()) {
            const VkDescriptorPool pool = CreateComputeDescriptorPool(*next);
            if (pool == VK_NULL_HANDLE) {
                throw std::runtime_error("GpuResourceFactory::AllocateComputeDescriptorSet: vkCreateDescriptorPool failed (pool "
                    + std::to_string(m_computePools.size() + 1) + ").");
            }
            m_computePools.push_back(pool);
            m_lastComputePoolCapacity = *next;
            m_computeBudget.AddCapacity(*next);
            GTE_LOG_WARNING("GpuResourceFactory",
                "Compute descriptor pool added: " + std::to_string(m_computePools.size()) + " pools, newest holds "
                    + std::to_string(next->sets) + " sets.");
            set = tryAllocate();
            freshPoolFailed = !set.has_value();
        } else {
            poolCapReached = true;
        }
    }

    if (!set.has_value()) {
        const std::string stats = std::to_string(m_computePools.size()) + " pools, newest holds "
            + std::to_string(m_lastComputePoolCapacity.sets) + " sets, " + std::to_string(m_computeBudget.Used().sets)
            + " sets in use";
        if (freshPoolFailed) {
            // Log once per failing layout; if the memory of failing layouts is full, say so once.
            if (m_unsatisfiableLayouts.Insert(layout)) {
                GTE_LOG_ERROR("GpuResourceFactory",
                    "A descriptor layout fits no pool and is refused from now on (" + stats + ").");
            } else if (!m_unsatisfiableOverflowLogged) {
                m_unsatisfiableOverflowLogged = true;
                GTE_LOG_ERROR("GpuResourceFactory",
                    "Too many descriptor layouts fit no pool; further ones are not remembered and retry every call ("
                        + stats + ").");
            }
        } else if (poolCapReached && !m_poolCapLogged) {
            m_poolCapLogged = true;
            GTE_LOG_ERROR("GpuResourceFactory", "Compute descriptor pool set is exhausted (" + stats + ").");
        }
        throw std::runtime_error("GpuResourceFactory::AllocateComputeDescriptorSet: no pool can hold this layout ("
            + stats + ").");
    }

    m_poolCapLogged = false; // A later exhaustion is a new event.
    if (m_computeBudget.TakeWarning()) {
        GTE_LOG_WARNING("GpuResourceFactory",
            "Compute descriptor pools are above 75% full: " + std::to_string(m_computeBudget.Used().sets) + "/"
                + std::to_string(m_computeBudget.Capacity().sets) + " sets, "
                + std::to_string(m_computeBudget.Used().storageBuffers) + "/"
                + std::to_string(m_computeBudget.Capacity().storageBuffers) + " storage buffers, "
                + std::to_string(m_computeBudget.Used().storageImages) + "/"
                + std::to_string(m_computeBudget.Capacity().storageImages) + " storage images, "
                + std::to_string(m_computeBudget.Used().combinedSamplers) + "/"
                + std::to_string(m_computeBudget.Capacity().combinedSamplers) + " samplers, "
                + std::to_string(m_computeBudget.Used().uniformBuffers) + "/"
                + std::to_string(m_computeBudget.Capacity().uniformBuffers) + " uniform buffers.");
    }
    return ComputeDescriptorSet(*set, m_device); // Device bound here, once - callers never see VkDevice.
}

void GpuResourceFactory::ForgetComputeLayout(VkDescriptorSetLayout layout) const noexcept
{
    const std::scoped_lock lock(m_computePoolMutex);
    m_unsatisfiableLayouts.Erase(layout);
    m_layoutCosts.erase(layout);
}

void GpuResourceFactory::FreeComputeDescriptorSet(VkDescriptorSet descriptorSet) const noexcept
{
    if (descriptorSet == VK_NULL_HANDLE) {
        return;
    }
    const std::scoped_lock lock(m_computePoolMutex);
    const auto it = m_computeSets.find(descriptorSet);
    if (it == m_computeSets.end() || it->second.pool >= m_computePools.size()) {
        return;
    }
    vkFreeDescriptorSets(m_device, m_computePools[it->second.pool], 1, &descriptorSet);
    m_computeBudget.OnFreed(it->second.cost);
    m_computeSets.erase(it);
}

GpuMemoryTracker::Totals GpuResourceFactory::GetMemoryTotals() const
{
    return m_memoryTracker->GetTotals();
}

std::vector<GpuMemoryTracker::Entry> GpuResourceFactory::GetMemoryResources() const
{
    return m_memoryTracker->GetAllResources();
}

} // namespace gte
