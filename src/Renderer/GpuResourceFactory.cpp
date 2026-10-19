#include "GpuResourceFactory.h"

#include "../Core/Logging.h"
#include "Compute/ComputePipeline.h"
#include "DescriptorSet.h"
#include "MipChain.h"
#include "GraphicsHandleRegistry.h" // RegisterDescriptorSet() - sets leave this file as engine handles.
#include "SceneServicesDescriptorSet.h" // gte::kSceneServiceSlotCount for the pool budget static_asserts.
#include "VulkanFormatInfo.h" // ToVkFormat() for pre-construction capability checks.
#include "Vulkan/DescriptorCost.h"
#include "Vulkan/DescriptorSetLayoutBuilder.h"
#include "Vulkan/FormatCapabilities.h"
#include "Vulkan/VulkanDevice.h"

#include <algorithm>
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

    // Shared material set layout (fragment stage): binding 0 = base-color sampler, binding 1 = MaterialParams block.
    VkDescriptorSetLayoutBinding materialBindings[2]{};
    materialBindings[0].binding = 0;
    materialBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    materialBindings[0].descriptorCount = 1;
    materialBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    materialBindings[1].binding = 1;
    materialBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    materialBindings[1].descriptorCount = 1;
    materialBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = 2;
    setLayoutInfo.pBindings = materialBindings;

    if (vkCreateDescriptorSetLayout(m_device, &setLayoutInfo, nullptr, &m_materialSetLayout) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw std::runtime_error("GpuResourceFactory: vkCreateDescriptorSetLayout failed");
    }

    // Material sets are never freed individually; the whole pool dies in Destroy().
    constexpr std::uint32_t kMaxMaterials = 4096;
    const VkDescriptorPoolSize materialPoolSizes[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxMaterials },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxMaterials },
    };

    VkDescriptorPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.maxSets = kMaxMaterials;
    poolCreateInfo.poolSizeCount = 2;
    poolCreateInfo.pPoolSizes = materialPoolSizes;

    if (vkCreateDescriptorPool(m_device, &poolCreateInfo, nullptr, &m_materialDescriptorPool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw std::runtime_error("GpuResourceFactory: vkCreateDescriptorPool failed");
    }

    // First compute descriptor pool; more are added on demand (see AllocateDescriptorSet()).
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
    bool createDepthCompanion, bool createColorImage, bool transferSrc, bool transferDst) const
{
    // The VkFormat is needed only for this capability check; the depth format is stored as VkFormat.
    if (allowStorageImageAccess && !SupportsStorageImageUsage(m_physicalDevice, ToVkFormat(format))) {
        throw std::runtime_error(
            "GpuResourceFactory::CreateRenderTexture: requested allowStorageImageAccess = true, but this "
            "physical device does not support VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT for the requested format.");
    }
    return RenderTexture(m_allocator, m_memoryTracker, m_device, width, height, format, FromVkFormat(m_depthFormat),
        debugName, depthDebugName, allowStorageImageAccess, allowDepthSampledAccess, createDepthCompanion,
        createColorImage, transferSrc, transferDst);
}

Buffer GpuResourceFactory::CreateBuffer(
    uint64_t size, BufferUsage usage, BufferMemoryUsage memoryUsage, const char* debugName) const
{
    return Buffer(m_allocator, m_memoryTracker, size, usage, memoryUsage, debugName);
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

Texture2D GpuResourceFactory::CreateTexture2D(const Texture2DUploadDesc& desc) const
{
    if (desc.format != PixelFormat::RGBA8_UNorm && desc.format != PixelFormat::RGBA8_SRGB) {
        throw std::runtime_error("GpuResourceFactory::CreateTexture2D: only RGBA8_UNorm and RGBA8_SRGB are supported.");
    }
    const VkFormat vkFormat = ToVkFormat(desc.format);
    if (!SupportsLinearSampling(m_physicalDevice, vkFormat)) {
        throw std::runtime_error("GpuResourceFactory::CreateTexture2D: this physical device cannot sample the requested format.");
    }
    if (desc.allowStorageImageAccess && !SupportsStorageImageUsage(m_physicalDevice, vkFormat)) {
        throw std::runtime_error(
            "GpuResourceFactory::CreateTexture2D: requested allowStorageImageAccess = true, but this physical "
            "device does not support VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT for the requested format.");
    }

    // Level 0 only unless a mip chain was requested. Sizes below 1 are clamped like Texture does.
    const MipChainRgba8 chain = BuildMipChainRgba8(static_cast<const std::uint8_t*>(desc.pixelsRgba8),
        static_cast<std::uint32_t>(std::max(desc.width, 1)), static_cast<std::uint32_t>(std::max(desc.height, 1)),
        desc.generateMips ? std::numeric_limits<std::uint32_t>::max() : 1U,
        desc.format == PixelFormat::RGBA8_SRGB ? MipColorSpace::Srgb : MipColorSpace::Linear);
    if (chain.levelCount == 0) {
        throw std::runtime_error("GpuResourceFactory::CreateTexture2D: pixel data is null.");
    }

    Texture2D texture(m_allocator, m_memoryTracker, m_device, desc, chain.levelCount);

    // One copy region per mip level.
    std::vector<VkBufferImageCopy> regions;
    regions.reserve(chain.levelCount);
    for (std::uint32_t level = 0; level < chain.levelCount; ++level) {
        VkBufferImageCopy region{};
        region.bufferOffset = chain.levelByteOffsets[level];
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
        region.imageExtent = { chain.levelWidths[level], chain.levelHeights[level], 1 };
        regions.push_back(region);
    }

    // Unnamed throwaway staging buffer.
    const uint64_t size = chain.pixels.size();
    Buffer staging = CreateBuffer(size, BufferUsage::TransferSrc, BufferMemoryUsage::CpuToGpu);
    staging.Upload(chain.pixels.data(), static_cast<std::size_t>(size));

    ImmediateSubmitInternal([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier toTransferDst{};
        toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransferDst.image = texture.Image();
        toTransferDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, chain.levelCount, 0, 1 };
        toTransferDst.srcAccessMask = 0;
        toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
            nullptr, 1, &toTransferDst);

        vkCmdCopyBufferToImage(cmd, staging.Native(), texture.Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<std::uint32_t>(regions.size()), regions.data());

        VkImageMemoryBarrier toShaderRead = toTransferDst;
        toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
            nullptr, 0, nullptr, 1, &toShaderRead);
    });
    // staging is destroyed here; the copy has already completed.
    texture.SetCurrentState(rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false));

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

// Material textures hold colour data: stored as sRGB, decoded to linear by the sampler.
MaterialTexture GpuResourceFactory::CreateMaterialTexture2D(
    const void* pixelsRgba8, int width, int height, const MaterialParams& params, const char* debugName) const
{
    auto image = std::make_shared<Texture2D>(CreateTexture2D({ .pixelsRgba8 = pixelsRgba8,
        .width = width,
        .height = height,
        .debugName = debugName,
        .format = PixelFormat::RGBA8_SRGB,
        .addressMode = TextureAddressMode::Repeat,
        .generateMips = true,
        .maxAnisotropy = m_vulkanDevice->MaterialAnisotropy() }));
    return CreateMaterialVariant(std::move(image), params);
}

MaterialTexture GpuResourceFactory::CreateMaterialVariant(
    std::shared_ptr<Texture2D> image, const MaterialParams& params) const
{
    if (!image) {
        throw std::runtime_error("GpuResourceFactory::CreateMaterialVariant: image is null.");
    }

    Buffer paramsBuffer = CreateBuffer(sizeof(MaterialParams), BufferUsage::UniformBuffer, BufferMemoryUsage::CpuToGpu,
        "MaterialParams");
    paramsBuffer.Upload(&params, sizeof(params));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_materialDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_materialSetLayout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("GpuResourceFactory::CreateMaterialVariant: vkAllocateDescriptorSets failed "
            "(consider raising kMaxMaterials in GpuResourceFactory.cpp).");
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = image->View();
    imageInfo.sampler = image->Sampler();

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = paramsBuffer.Native();
    bufferInfo.range = sizeof(MaterialParams);

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &imageInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);

    // Registered once; the set is pool-owned and never freed or reallocated individually.
    return MaterialTexture{ std::move(image), std::move(paramsBuffer), RegisterDescriptorSet(descriptorSet) };
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

DescriptorSet GpuResourceFactory::AllocateDescriptorSet(VkDescriptorSetLayout layout) const
{
    const std::scoped_lock lock(m_computePoolMutex);

    // A layout that already failed on a fresh pool is refused without growing anything (and without logging again).
    if (m_unsatisfiableLayouts.Contains(layout)) {
        throw std::runtime_error("GpuResourceFactory::AllocateDescriptorSet: layout does not fit any pool.");
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
                throw std::runtime_error("GpuResourceFactory::AllocateDescriptorSet: vkAllocateDescriptorSets failed.");
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
                throw std::runtime_error("GpuResourceFactory::AllocateDescriptorSet: vkCreateDescriptorPool failed (pool "
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
        throw std::runtime_error("GpuResourceFactory::AllocateDescriptorSet: no pool can hold this layout ("
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
    return DescriptorSet(*set, m_device); // Device bound here, once - callers never see VkDevice.
}

void GpuResourceFactory::ForgetComputeLayout(VkDescriptorSetLayout layout) const noexcept
{
    const std::scoped_lock lock(m_computePoolMutex);
    m_unsatisfiableLayouts.Erase(layout);
    m_layoutCosts.erase(layout);
}

void GpuResourceFactory::FreeDescriptorSet(VkDescriptorSet descriptorSet) const noexcept
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
