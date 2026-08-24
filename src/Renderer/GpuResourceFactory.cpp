#include "GpuResourceFactory.h"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gte {

GpuResourceFactory::GpuResourceFactory(VkDevice device, VmaAllocator allocator, VkQueue graphicsQueue,
    std::uint32_t graphicsQueueFamily, VkFormat depthFormat, std::shared_ptr<GpuMemoryTracker> memoryTracker)
    : m_device(device)
    , m_allocator(allocator)
    , m_graphicsQueue(graphicsQueue)
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
}

GpuResourceFactory::~GpuResourceFactory()
{
    Destroy();
}

GpuResourceFactory::GpuResourceFactory(GpuResourceFactory&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
    , m_graphicsQueue(std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE))
    , m_depthFormat(other.m_depthFormat)
    , m_memoryTracker(std::move(other.m_memoryTracker))
    , m_commandPool(std::exchange(other.m_commandPool, VK_NULL_HANDLE))
{
}

GpuResourceFactory& GpuResourceFactory::operator=(GpuResourceFactory&& other) noexcept
{
    if (this != &other) {
        Destroy();

        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
        m_graphicsQueue = std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE);
        m_depthFormat = other.m_depthFormat;
        m_memoryTracker = std::move(other.m_memoryTracker);
        m_commandPool = std::exchange(other.m_commandPool, VK_NULL_HANDLE);
    }
    return *this;
}

void GpuResourceFactory::Destroy() noexcept
{
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
}

RenderTexture GpuResourceFactory::CreateRenderTexture(
    int width, int height, VkFormat format, const char* debugName, const char* depthDebugName) const
{
    return RenderTexture(
        m_allocator, m_memoryTracker, m_device, width, height, format, m_depthFormat, debugName, depthDebugName);
}

Buffer GpuResourceFactory::CreateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, BufferMemoryUsage memoryUsage, const char* debugName) const
{
    return Buffer(m_allocator, m_memoryTracker, size, usage, memoryUsage, debugName);
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
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout) const
{
    return Pipeline(m_device, colorFormat, m_depthFormat, vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout);
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

Texture2D GpuResourceFactory::CreateTexture2D(
    const void* pixelsRgba8, int width, int height, const char* debugName) const
{
    Texture2D texture(m_allocator, m_memoryTracker, m_device, width, height, debugName);

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
