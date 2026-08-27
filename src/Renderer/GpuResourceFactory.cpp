#include "GpuResourceFactory.h"

#include "Vulkan/FormatCapabilities.h"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gte {

GpuResourceFactory::GpuResourceFactory(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator,
    VkQueue graphicsQueue, std::uint32_t graphicsQueueFamily, VkFormat depthFormat,
    std::shared_ptr<GpuMemoryTracker> memoryTracker)
    : m_device(device)
    , m_allocator(allocator)
    , m_graphicsQueue(graphicsQueue)
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

    // The one shared material descriptor-set-layout - a single combined-
    // image-sampler, fragment stage only (see MaterialDescriptorSetLayout()'s
    // own comment in GpuResourceFactory.h).
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

    // Generously sized for every material texture this process is ever
    // likely to load in one run (a single richly-materialed PMX model can
    // easily have several dozen) - individual sets are never freed (see
    // MaterialTexture.h), only the whole pool at once, in Destroy().
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

    // Phase 3 (COMPUTE_PHASE3_DESCRIPTOR_BINDING_MODEL_STRATEGY_v1.md) - a
    // SECOND, dedicated descriptor pool for compute-shaped descriptor
    // types - never shared with m_materialDescriptorPool above (see
    // m_computeDescriptorPool's own comment in GpuResourceFactory.h).
    // Generously sized (a low hundreds, not thousands - compute shaders
    // are far less numerous per-frame than material textures) for every
    // distinct compute-bound resource this engine is realistically likely
    // to need in one process lifetime; individual sets allocated from it
    // are never freed (see AllocateComputeDescriptorSet()), only the whole
    // pool at once, in Destroy().
    constexpr std::uint32_t kMaxComputeStorageBuffers = 256;
    constexpr std::uint32_t kMaxComputeStorageImages = 128;
    constexpr std::uint32_t kMaxComputeCombinedImageSamplers = 128;
    constexpr std::uint32_t kMaxComputeDescriptorSets = 256;

    VkDescriptorPoolSize computePoolSizes[3]{};
    computePoolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    computePoolSizes[0].descriptorCount = kMaxComputeStorageBuffers;
    computePoolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    computePoolSizes[1].descriptorCount = kMaxComputeStorageImages;
    computePoolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    computePoolSizes[2].descriptorCount = kMaxComputeCombinedImageSamplers;

    VkDescriptorPoolCreateInfo computePoolInfo{};
    computePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    computePoolInfo.maxSets = kMaxComputeDescriptorSets;
    computePoolInfo.poolSizeCount = 3;
    computePoolInfo.pPoolSizes = computePoolSizes;

    if (vkCreateDescriptorPool(m_device, &computePoolInfo, nullptr, &m_computeDescriptorPool) != VK_SUCCESS) {
        vkDestroyDescriptorPool(m_device, m_materialDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw std::runtime_error("GpuResourceFactory: vkCreateDescriptorPool (compute) failed");
    }
}

GpuResourceFactory::~GpuResourceFactory()
{
    Destroy();
}

GpuResourceFactory::GpuResourceFactory(GpuResourceFactory&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_graphicsQueue(std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE))
    , m_physicalDevice(std::exchange(other.m_physicalDevice, VK_NULL_HANDLE))
    , m_depthFormat(other.m_depthFormat)
    , m_memoryTracker(std::move(other.m_memoryTracker))
    , m_commandPool(std::exchange(other.m_commandPool, VK_NULL_HANDLE))
    , m_materialSetLayout(std::exchange(other.m_materialSetLayout, VK_NULL_HANDLE))
    , m_materialDescriptorPool(std::exchange(other.m_materialDescriptorPool, VK_NULL_HANDLE))
    , m_computeDescriptorPool(std::exchange(other.m_computeDescriptorPool, VK_NULL_HANDLE))
{
}

GpuResourceFactory& GpuResourceFactory::operator=(GpuResourceFactory&& other) noexcept
{
    if (this != &other) {
        Destroy();

        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_graphicsQueue = std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE);
        m_physicalDevice = std::exchange(other.m_physicalDevice, VK_NULL_HANDLE);
        m_depthFormat = other.m_depthFormat;
        m_memoryTracker = std::move(other.m_memoryTracker);
        m_commandPool = std::exchange(other.m_commandPool, VK_NULL_HANDLE);
        m_materialSetLayout = std::exchange(other.m_materialSetLayout, VK_NULL_HANDLE);
        m_materialDescriptorPool = std::exchange(other.m_materialDescriptorPool, VK_NULL_HANDLE);
        m_computeDescriptorPool = std::exchange(other.m_computeDescriptorPool, VK_NULL_HANDLE);
    }
    return *this;
}

void GpuResourceFactory::Destroy() noexcept
{
    if (m_materialDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_materialDescriptorPool, nullptr);
        m_materialDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_computeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_computeDescriptorPool, nullptr);
        m_computeDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_materialSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        m_materialSetLayout = VK_NULL_HANDLE;
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
}

RenderTexture GpuResourceFactory::CreateRenderTexture(int width, int height, VkFormat format, const char* debugName,
    const char* depthDebugName, bool allowStorageImageAccess) const
{
    if (allowStorageImageAccess && !SupportsStorageImageUsage(m_physicalDevice, format)) {
        throw std::runtime_error(
            "GpuResourceFactory::CreateRenderTexture: requested allowStorageImageAccess = true, but this "
            "physical device does not support VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT for the requested format.");
    }
    return RenderTexture(m_allocator, m_memoryTracker, m_device, width, height, format, m_depthFormat, debugName,
        depthDebugName, allowStorageImageAccess);
}

Buffer GpuResourceFactory::CreateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage, const char* debugName) const
{
    return Buffer(m_allocator, m_memoryTracker, size, usage, memoryUsage, debugName);
}

Buffer GpuResourceFactory::CreateStructuredBuffer(VkDeviceSize elementStride, std::uint32_t elementCount,
    BufferMemoryUsage memoryUsage, VkBufferUsageFlags extraUsage, const char* debugName) const
{
    const VkDeviceSize size = elementStride * static_cast<VkDeviceSize>(elementCount);
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | extraUsage;
    if (memoryUsage == BufferMemoryUsage::GpuOnly) {
        // Mirrors CreateDeviceLocalBuffer()'s own convention - so a
        // GpuOnly structured buffer can still be initialized once via a
        // caller-driven staging upload (ImmediateSubmit() + vkCmdCopyBuffer).
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return CreateBuffer(size, usage, memoryUsage, debugName);
}

Buffer GpuResourceFactory::CreateDeviceLocalBuffer(
    const void* data, VkDeviceSize size, VkBufferUsageFlags usage, const char* debugName) const
{
    // The staging buffer is intentionally unnamed (nullptr) - it's a
    // throwaway that never outlives this function, so it's never worth
    // showing up as a named entry in a future Memory panel.
    Buffer staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, BufferMemoryUsage::CpuToGpu);
    staging.Upload(data, static_cast<std::size_t>(size));

    Buffer deviceLocal =
        CreateBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, BufferMemoryUsage::GpuOnly, debugName);

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, staging.Native(), deviceLocal.Native(), 1, &copyRegion);
    });
    // staging goes out of scope here and is destroyed - the copy above has
    // already completed (ImmediateSubmit blocks until the GPU is done).

    return deviceLocal;
}

void GpuResourceFactory::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& recordFn) const
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("GpuResourceFactory::ImmediateSubmit: vkAllocateCommandBuffers failed.");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
        throw std::runtime_error("GpuResourceFactory::ImmediateSubmit: vkBeginCommandBuffer failed.");
    }

    recordFn(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
        throw std::runtime_error("GpuResourceFactory::ImmediateSubmit: vkEndCommandBuffer failed.");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
        throw std::runtime_error("GpuResourceFactory::ImmediateSubmit: vkCreateFence failed.");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
        vkDestroyFence(m_device, fence, nullptr);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
        throw std::runtime_error("GpuResourceFactory::ImmediateSubmit: vkQueueSubmit failed.");
    }

    vkWaitForFences(m_device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());

    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

Pipeline GpuResourceFactory::CreatePipeline(VkFormat colorFormat, const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture) const
{
    const VkDescriptorSetLayout materialSetLayout = useMaterialTexture ? m_materialSetLayout : VK_NULL_HANDLE;
    return Pipeline(m_device, colorFormat, m_depthFormat, vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
        materialSetLayout);
}

ComputePipeline GpuResourceFactory::CreateComputePipeline(const std::string& shaderSpirvPath,
    const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
    std::optional<VkPushConstantRange> pushConstantRange) const
{
    return ComputePipeline(m_device, shaderSpirvPath, descriptorSetLayouts, pushConstantRange);
}

Mesh GpuResourceFactory::CreateMesh(
    const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount, const char* debugName) const
{
    Buffer vertexBuffer =
        CreateDeviceLocalBuffer(vertexData, vertexDataSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, debugName);
    return Mesh(std::move(vertexBuffer), vertexCount);
}

Mesh GpuResourceFactory::CreateMesh(const void* vertexData, VkDeviceSize vertexDataSize, std::uint32_t vertexCount,
    const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount, const char* debugName) const
{
    Buffer vertexBuffer =
        CreateDeviceLocalBuffer(vertexData, vertexDataSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, debugName);
    Buffer indexBuffer =
        CreateDeviceLocalBuffer(indexData, indexDataSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, debugName);
    return Mesh(std::move(vertexBuffer), vertexCount, std::move(indexBuffer), indexCount);
}

Mesh GpuResourceFactory::CreateSkinnedMesh(const void* vertexData, VkDeviceSize vertexDataSize,
    std::uint32_t vertexCount, const void* indexData, VkDeviceSize indexDataSize, std::uint32_t indexCount,
    const char* debugName) const
{
    // Host-visible + persistently mapped (BufferMemoryUsage::CpuToGpu),
    // unlike CreateMesh()'s device-local vertex buffer - initialized with
    // the bind pose right away, then re-written every frame afterwards via
    // Mesh::UpdateVertexData() (see Game::UpdateSkeletalAnimators()).
    Buffer vertexBuffer = CreateBuffer(vertexDataSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, BufferMemoryUsage::CpuToGpu,
        debugName);
    vertexBuffer.Upload(vertexData, static_cast<std::size_t>(vertexDataSize));

    // The index buffer never changes as a mesh animates - keep it static/
    // device-local exactly like CreateMesh()'s own indexed overload.
    Buffer indexBuffer =
        CreateDeviceLocalBuffer(indexData, indexDataSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, debugName);

    return Mesh(std::move(vertexBuffer), vertexCount, std::move(indexBuffer), indexCount);
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

    const auto safeWidth = static_cast<VkDeviceSize>(texture.Width());
    const auto safeHeight = static_cast<VkDeviceSize>(texture.Height());
    const VkDeviceSize size = safeWidth * safeHeight * 4;

    // Unnamed - same reasoning as CreateDeviceLocalBuffer()'s own staging
    // buffer above: a throwaway that never outlives this function.
    Buffer staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, BufferMemoryUsage::CpuToGpu);
    staging.Upload(pixelsRgba8, static_cast<std::size_t>(size));

    ImmediateSubmit([&](VkCommandBuffer cmd) {
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
    // staging goes out of scope here and is destroyed - the copy above has
    // already completed (ImmediateSubmit blocks until the GPU is done).

    return texture;
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

    return MaterialTexture{ std::move(texture), descriptorSet };
}

VkDescriptorSet GpuResourceFactory::AllocateComputeDescriptorSet(VkDescriptorSetLayout layout) const
{
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_computeDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error(
            "GpuResourceFactory::AllocateComputeDescriptorSet: vkAllocateDescriptorSets failed "
            "(consider raising GpuResourceFactory.cpp's compute descriptor pool sizes).");
    }
    return descriptorSet;
}

GpuMemoryTracker::Totals GpuResourceFactory::GetMemoryTotals() const
{
    return m_memoryTracker->GetTotals();
}

std::vector<GpuMemoryTracker::Entry> GpuResourceFactory::GetMemoryResources() const
{
    return m_memoryTracker->GetAllResources();
}

#if GTE_ENABLE_EDITOR
const std::string& GpuResourceFactory::GetMemoryDebugName(GpuResourceHandle handle) const
{
    return m_memoryTracker->GetDebugName(handle);
}
#endif

} // namespace gte
