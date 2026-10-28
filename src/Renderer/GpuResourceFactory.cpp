#include "GpuResourceFactory.h"

#include "../Core/Logging.h"
#include "Compute/ComputePipeline.h"
#include "GpuWaitFailure.h"
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
#include <string>
#include <utility>

namespace gte {

GpuResourceFactory::GpuResourceFactory(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator,
    std::uint32_t graphicsQueueFamily, VkFormat depthFormat, std::shared_ptr<GpuMemoryTracker> memoryTracker,
    VulkanDevice& vulkanDevice, AbandonedWork& abandoned)
    : m_device(device)
    , m_allocator(allocator)
    , m_vulkanDevice(&vulkanDevice)
    , m_abandoned(abandoned)
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

    // Material set layout (fragment stage): 0 = base color, 1 = MaterialParams, 2 = normal,
    // 3 = metallic-roughness, 4 = occlusion.
    VkDescriptorSetLayoutBinding materialBindings[5]{};
    for (std::uint32_t binding = 0; binding < 5; ++binding) {
        materialBindings[binding].binding = binding;
        materialBindings[binding].descriptorType
            = binding == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        materialBindings[binding].descriptorCount = 1;
        materialBindings[binding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = 5;
    setLayoutInfo.pBindings = materialBindings;

    if (vkCreateDescriptorSetLayout(m_device, &setLayoutInfo, nullptr, &m_materialSetLayout) != VK_SUCCESS) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw std::runtime_error("GpuResourceFactory: vkCreateDescriptorSetLayout failed");
    }

    constexpr std::uint32_t kMaxMaterials = 4096;
    try {
        m_materialSets = std::make_shared<MaterialSetPool>(m_device, m_materialSetLayout, kMaxMaterials);
    } catch (...) {
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw;
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
        m_materialSets.reset();
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw std::runtime_error("GpuResourceFactory: vkCreateDescriptorPool (compute) failed");
    }
    m_computePools.push_back(firstComputePool);
    m_lastComputePoolCapacity = kInitialComputePool;
    m_computeBudget.AddCapacity(kInitialComputePool);

    // Buffer set: visible list at binding 0, instances at binding 1, both vertex-stage storage buffers.
    try {
        DescriptorSetLayoutBuilder bufferLayoutBuilder(m_device);
        m_bufferSetLayout = bufferLayoutBuilder.AddStorageBuffer(/*binding=*/0, VK_SHADER_STAGE_VERTEX_BIT)
                                .AddStorageBuffer(/*binding=*/1, VK_SHADER_STAGE_VERTEX_BIT)
                                .Build();
    } catch (...) {
        vkDestroyDescriptorPool(m_device, firstComputePool, nullptr);
        m_computePools.clear();
        m_materialSets.reset();
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        throw;
    }
    m_layoutCosts.emplace(m_bufferSetLayout, PoolCapacity{ .sets = 1, .storageBuffers = 2 });
}

GpuResourceFactory::~GpuResourceFactory()
{
    Destroy();
}

void GpuResourceFactory::Destroy() noexcept
{
    m_defaultMaterialImages = {}; // Images go before the allocator they came from.
    m_materialSets.reset();
    for (const VkDescriptorPool pool : m_computePools) {
        vkDestroyDescriptorPool(m_device, pool, nullptr);
    }
    m_computePools.clear();
    m_computeSets.clear();
    if (m_materialSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);
        m_materialSetLayout = VK_NULL_HANDLE;
    }
    if (m_bufferSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_bufferSetLayout, nullptr);
        m_bufferSetLayout = VK_NULL_HANDLE;
    }
    for (const VkFence fence : m_fencePool) {
        vkDestroyFence(m_device, fence, nullptr); // Pooled fences are never pending.
    }
    m_fencePool.clear();
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

namespace {

// One-shot command buffer and fence, plus everything the GPU touches while it runs.
// Destroying it before the GPU finished is only safe on a successful wait; a failed wait parks it instead.
struct OneShotLease {
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::mutex* poolMutex = nullptr; // Guards 'pool'.
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::vector<std::shared_ptr<void>> resources;

    OneShotLease() = default;
    OneShotLease(const OneShotLease&) = delete;
    OneShotLease& operator=(const OneShotLease&) = delete;
    ~OneShotLease()
    {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (cmd != VK_NULL_HANDLE) {
            const std::lock_guard lock(*poolMutex);
            vkFreeCommandBuffers(device, pool, 1, &cmd);
        }
    }
};

} // namespace

Buffer GpuResourceFactory::CreateDeviceLocalBuffer(
    const void* data, uint64_t size, BufferUsage usage, const char* debugName) const
{
    // Unnamed throwaway staging buffer. Both buffers are parked if the copy's wait fails.
    const auto staging = std::make_shared<Buffer>(CreateBuffer(size, BufferUsage::TransferSrc, BufferMemoryUsage::CpuToGpu));
    staging->Upload(data, static_cast<std::size_t>(size));

    const auto deviceLocal = std::make_shared<Buffer>(
        CreateBuffer(size, usage | BufferUsage::TransferDst, BufferMemoryUsage::GpuOnly, debugName));

    const std::array<std::shared_ptr<void>, 2> inFlight{ staging, deviceLocal };
    SubmitOneShot(
        BlockSite::ImmediateSubmit, "buffer upload",
        [&](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion{};
            copyRegion.size = static_cast<VkDeviceSize>(size);
            vkCmdCopyBuffer(cmd, staging->Native(), deviceLocal->Native(), 1, &copyRegion);
        },
        inFlight);

    return std::move(*deviceLocal);
}

VkCommandBuffer GpuResourceFactory::AllocateTransientCommandBuffer() const
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    const std::lock_guard lock(m_commandPoolMutex);
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        throw std::runtime_error("GpuResourceFactory::AllocateTransientCommandBuffer: vkAllocateCommandBuffers failed.");
    }
    return cmd;
}

VkFence GpuResourceFactory::AcquireFence() const
{
    {
        const std::lock_guard lock(m_fencePoolMutex);
        if (!m_fencePool.empty()) {
            const VkFence fence = m_fencePool.back();
            m_fencePool.pop_back();
            return fence;
        }
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    return vkCreateFence(m_device, &fenceInfo, nullptr, &fence) == VK_SUCCESS ? fence : VK_NULL_HANDLE;
}

void GpuResourceFactory::ReleaseFence(VkFence fence) const noexcept
{
    vkResetFences(m_device, 1, &fence);
    try {
        const std::lock_guard lock(m_fencePoolMutex);
        m_fencePool.push_back(fence);
    } catch (...) {
        vkDestroyFence(m_device, fence, nullptr);
    }
}

void GpuResourceFactory::SubmitOneShot(BlockSite site, const char* detail,
    const std::function<void(VkCommandBuffer)>& recordFn, std::span<const std::shared_ptr<void>> inFlight) const
{
    GpuWaitService& waits = m_vulkanDevice->Waits();
    if (waits.DeviceLost()) {
        throw GpuWaitFailure(
            std::string("GpuResourceFactory::SubmitOneShot(") + detail + "): device lost", BlockOutcome::DeviceLost);
    }

    const auto lease = std::make_shared<OneShotLease>();
    lease->device = m_device;
    lease->pool = m_commandPool;
    lease->poolMutex = &m_commandPoolMutex;
    lease->resources.assign(inFlight.begin(), inFlight.end());
    lease->cmd = AllocateTransientCommandBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(lease->cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("GpuResourceFactory::SubmitOneShot: vkBeginCommandBuffer failed.");
    }

    recordFn(lease->cmd);

    if (vkEndCommandBuffer(lease->cmd) != VK_SUCCESS) {
        throw std::runtime_error("GpuResourceFactory::SubmitOneShot: vkEndCommandBuffer failed.");
    }

    lease->fence = AcquireFence();
    if (lease->fence == VK_NULL_HANDLE) {
        throw std::runtime_error("GpuResourceFactory::SubmitOneShot: vkCreateFence failed.");
    }

    VkCommandBufferSubmitInfo cmdSubmitInfo{};
    cmdSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdSubmitInfo.commandBuffer = lease->cmd;

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdSubmitInfo;

    if (const VkResult submitted = m_vulkanDevice->SubmitToGraphicsQueue(submitInfo, lease->fence);
        submitted != VK_SUCCESS) {
        if (submitted == VK_ERROR_DEVICE_LOST) {
            waits.MarkDeviceLost();
        }
        throw std::runtime_error("GpuResourceFactory::SubmitOneShot: vkQueueSubmit2 failed.");
    }

    const BlockOutcome outcome = waits.WaitFence(lease->fence, kUploadWaitTimeout, WaitClass::Upload, site, detail);
    if (outcome != BlockOutcome::Completed) {
        m_abandoned.Park(lease);
        throw GpuWaitFailure(
            std::string("GpuResourceFactory::SubmitOneShot(") + detail + "): GPU wait " + ToString(outcome), outcome);
    }

    ReleaseFence(std::exchange(lease->fence, VK_NULL_HANDLE));
}

Pipeline GpuResourceFactory::CreatePipeline(VkFormat colorFormat, const std::string& vertexShaderSpirvPath,
    const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout, bool useMaterialTexture,
    const char* debugName, VkDescriptorSetLayout sceneServicesSetLayout) const
{
    const VkDescriptorSetLayout materialSetLayout = useMaterialTexture ? m_materialSetLayout : VK_NULL_HANDLE;
    return Pipeline(m_device, colorFormat, m_depthFormat, vertexShaderSpirvPath, fragmentShaderSpirvPath, vertexLayout,
        materialSetLayout, debugName, VK_NULL_HANDLE, sceneServicesSetLayout);
}

Pipeline GpuResourceFactory::CreatePipeline(std::span<const VkFormat> colorFormats,
    const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout,
    bool useMaterialTexture, const char* debugName, VkDescriptorSetLayout sceneServicesSetLayout) const
{
    const VkDescriptorSetLayout materialSetLayout = useMaterialTexture ? m_materialSetLayout : VK_NULL_HANDLE;
    return Pipeline(m_device, colorFormats, m_depthFormat, vertexShaderSpirvPath, fragmentShaderSpirvPath,
        vertexLayout, materialSetLayout, debugName, VK_NULL_HANDLE, sceneServicesSetLayout);
}

Pipeline GpuResourceFactory::CreateClusterPipeline(std::span<const VkFormat> colorFormats,
    const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout,
    bool useMaterialSet, bool cullBackFaces, const char* debugName, VkDescriptorSetLayout sceneServicesSetLayout) const
{
    const VkDescriptorSetLayout materialSetLayout = useMaterialSet ? m_materialSetLayout : VK_NULL_HANDLE;
    return Pipeline(m_device, colorFormats, m_depthFormat, vertexShaderSpirvPath, fragmentShaderSpirvPath,
        vertexLayout, materialSetLayout, debugName, m_bufferSetLayout, sceneServicesSetLayout,
        cullBackFaces ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE);
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

namespace {

constexpr std::uint64_t kUploadStagingBudgetBytes = 64ull * 1024 * 1024;
constexpr std::size_t kUploadMaxItemsPerChunk = 256;
constexpr std::uint64_t kStagingAlignment = 16;

constexpr std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment) noexcept
{
    return (value + alignment - 1) / alignment * alignment;
}

} // namespace

Texture2D GpuResourceFactory::CreateTexture2D(const Texture2DUploadDesc& desc) const
{
    std::vector<Texture2D> textures = UploadTexture2DChunk(std::span<const Texture2DUploadDesc>(&desc, 1));
    return std::move(textures.front());
}

std::vector<Texture2D> GpuResourceFactory::UploadTexture2DChunk(std::span<const Texture2DUploadDesc> descs) const
{
    std::vector<Texture2D> textures;
    if (descs.empty()) {
        return textures;
    }

    for (const Texture2DUploadDesc& desc : descs) {
        if (desc.format != PixelFormat::RGBA8_UNorm && desc.format != PixelFormat::RGBA8_SRGB) {
            throw std::runtime_error(
                "GpuResourceFactory::UploadTexture2DChunk: only RGBA8_UNorm and RGBA8_SRGB are supported.");
        }
        const VkFormat vkFormat = ToVkFormat(desc.format);
        if (!SupportsLinearSampling(m_physicalDevice, vkFormat)) {
            throw std::runtime_error(
                "GpuResourceFactory::UploadTexture2DChunk: this physical device cannot sample the requested format.");
        }
        if (desc.allowStorageImageAccess && !SupportsStorageImageUsage(m_physicalDevice, vkFormat)) {
            throw std::runtime_error(
                "GpuResourceFactory::UploadTexture2DChunk: requested allowStorageImageAccess = true, but this "
                "physical device does not support VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT for the requested format.");
        }
    }

    // Mip chains and their offsets inside the shared staging buffer. Sizes below 1 are clamped like Texture does.
    std::vector<MipChainRgba8> chains;
    std::vector<std::uint64_t> stagingOffsets;
    chains.reserve(descs.size());
    stagingOffsets.reserve(descs.size());
    std::uint64_t stagingSize = 0;
    for (const Texture2DUploadDesc& desc : descs) {
        MipChainRgba8 chain = BuildMipChainRgba8(static_cast<const std::uint8_t*>(desc.pixelsRgba8),
            static_cast<std::uint32_t>(std::max(desc.width, 1)), static_cast<std::uint32_t>(std::max(desc.height, 1)),
            desc.generateMips ? std::numeric_limits<std::uint32_t>::max() : 1U,
            desc.format == PixelFormat::RGBA8_SRGB ? MipColorSpace::Srgb : MipColorSpace::Linear);
        if (chain.levelCount == 0) {
            throw std::runtime_error("GpuResourceFactory::UploadTexture2DChunk: pixel data is null.");
        }
        stagingOffsets.push_back(stagingSize);
        stagingSize += AlignUp(chain.pixels.size(), kStagingAlignment);
        chains.push_back(std::move(chain));
    }

    // Created before the staging buffer so a throw destroys every image made so far.
    textures.reserve(descs.size());
    for (std::size_t i = 0; i < descs.size(); ++i) {
        textures.emplace_back(m_allocator, m_memoryTracker, m_device, descs[i], chains[i].levelCount);
    }

    // Unnamed throwaway staging buffer; parked if the upload's wait fails.
    const auto staging =
        std::make_shared<Buffer>(CreateBuffer(stagingSize, BufferUsage::TransferSrc, BufferMemoryUsage::CpuToGpu));
    for (std::size_t i = 0; i < chains.size(); ++i) {
        staging->Upload(chains[i].pixels.data(), chains[i].pixels.size(), static_cast<std::size_t>(stagingOffsets[i]));
    }

    const rg::ResourceState undefinedState{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE };
    const rg::ResourceState transferDst = rg::RequiredStateFor(rg::ResourceAccess::TransferDst, false);
    const rg::ResourceState shaderRead = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);

    const auto record = [&](VkCommandBuffer cmd) {
        std::vector<VkImageMemoryBarrier2> barriers(textures.size());
        const auto emitBarriers = [&](const rg::ResourceState& from, const rg::ResourceState& to) {
            for (std::size_t i = 0; i < textures.size(); ++i) {
                const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, chains[i].levelCount, 0, 1 };
                barriers[i] = rg::BuildImageMemoryBarrier2(textures[i].Image(), range, from, to);
            }
            VkDependencyInfo dependency{};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
            dependency.pImageMemoryBarriers = barriers.data();
            vkCmdPipelineBarrier2(cmd, &dependency);
        };

        emitBarriers(undefinedState, transferDst);

        std::vector<VkBufferImageCopy> regions;
        for (std::size_t i = 0; i < textures.size(); ++i) {
            const MipChainRgba8& chain = chains[i];
            regions.clear();
            for (std::uint32_t level = 0; level < chain.levelCount; ++level) {
                VkBufferImageCopy region{};
                region.bufferOffset = stagingOffsets[i] + chain.levelByteOffsets[level];
                region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
                region.imageExtent = { chain.levelWidths[level], chain.levelHeights[level], 1 };
                regions.push_back(region);
            }
            vkCmdCopyBufferToImage(cmd, staging->Native(), textures[i].Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<std::uint32_t>(regions.size()), regions.data());
        }

        emitBarriers(transferDst, shaderRead);
    };

    try {
        const std::array<std::shared_ptr<void>, 1> inFlight{ staging };
        SubmitOneShot(BlockSite::ImmediateSubmit, "texture upload", record, inFlight);
    } catch (...) {
        // The GPU may still write the images: park them instead of freeing them under it.
        if (m_vulkanDevice->Waits().DeviceLost()) {
            try {
                m_abandoned.Park(std::make_shared<std::vector<Texture2D>>(std::move(textures)));
            } catch (...) {
                // Out of memory: the images are freed with the vector.
            }
        }
        throw;
    }

    for (Texture2D& texture : textures) {
        texture.SetCurrentState(shaderRead);
    }
    m_uploadSubmitCount.fetch_add(1, std::memory_order_relaxed);
    m_uploadImageCount.fetch_add(textures.size(), std::memory_order_relaxed);
    m_uploadStagingBytes.fetch_add(stagingSize, std::memory_order_relaxed);
    return textures;
}

std::vector<std::shared_ptr<Texture2D>> GpuResourceFactory::CreateMaterialImages(
    std::span<const MaterialImageUpload> uploads) const
{
    std::vector<std::shared_ptr<Texture2D>> result(uploads.size());

    // Valid uploads only; `names` is reserved up front so the c_str pointers in `descs` stay put.
    std::vector<std::string> names;
    std::vector<Texture2DUploadDesc> descs;
    std::vector<std::size_t> resultIndex;
    std::vector<std::uint64_t> chainBytes;
    names.reserve(uploads.size());
    descs.reserve(uploads.size());
    resultIndex.reserve(uploads.size());
    chainBytes.reserve(uploads.size());
    for (std::size_t i = 0; i < uploads.size(); ++i) {
        const MaterialImageUpload& upload = uploads[i];
        if (upload.pixelsRgba8 == nullptr || upload.width < 1 || upload.height < 1) {
            continue;
        }
        names.emplace_back(upload.debugName);
        descs.push_back({ .pixelsRgba8 = upload.pixelsRgba8,
            .width = upload.width,
            .height = upload.height,
            .debugName = names.back().c_str(),
            .format = MaterialImageFormat(upload.role),
            .addressMode = TextureAddressMode::Repeat,
            .generateMips = true,
            .maxAnisotropy = m_vulkanDevice->MaterialAnisotropy() });
        resultIndex.push_back(i);
        chainBytes.push_back(
            MipChainByteSize(static_cast<std::uint32_t>(upload.width), static_cast<std::uint32_t>(upload.height)));
    }

    for (const UploadChunk& chunk : PlanUploadChunks(chainBytes, kUploadStagingBudgetBytes, kUploadMaxItemsPerChunk)) {
        std::vector<Texture2D> uploaded = UploadTexture2DChunk(
            std::span<const Texture2DUploadDesc>(descs.data() + chunk.first, chunk.count));
        for (std::size_t i = 0; i < uploaded.size(); ++i) {
            result[resultIndex[chunk.first + i]] = std::make_shared<Texture2D>(std::move(uploaded[i]));
        }
    }
    return result;
}

GpuUploadStats GpuResourceFactory::UploadStats() const noexcept
{
    return GpuUploadStats{ m_uploadSubmitCount.load(std::memory_order_relaxed),
        m_uploadImageCount.load(std::memory_order_relaxed), m_uploadStagingBytes.load(std::memory_order_relaxed) };
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

std::shared_ptr<Texture2D> GpuResourceFactory::DefaultMaterialImage(MaterialImageRole role) const
{
    const std::scoped_lock lock(m_defaultMaterialImageMutex);
    const std::size_t slot = static_cast<std::size_t>(role);
    if (std::any_of(m_defaultMaterialImages.begin(), m_defaultMaterialImages.end(),
            [](const std::shared_ptr<Texture2D>& image) { return image == nullptr; })) {
        static constexpr std::uint8_t kWhite[4] = { 255, 255, 255, 255 };
        static constexpr std::uint8_t kFlatNormal[4] = { 128, 128, 255, 255 }; // Decodes to about (0, 0, 1).
        const std::array<MaterialImageUpload, kMaterialImageRoleCount> uploads = { {
            { kWhite, 1, 1, MaterialImageRole::BaseColor, "MaterialDefaultBaseColor" },
            { kFlatNormal, 1, 1, MaterialImageRole::Normal, "MaterialDefaultNormal" },
            { kWhite, 1, 1, MaterialImageRole::MetallicRoughness, "MaterialDefaultMetallicRoughness" },
            { kWhite, 1, 1, MaterialImageRole::Occlusion, "MaterialDefaultOcclusion" },
        } };
        std::vector<std::shared_ptr<Texture2D>> created = CreateMaterialImages(uploads);
        if (created.size() != kMaterialImageRoleCount
            || std::any_of(created.begin(), created.end(), [](const auto& image) { return image == nullptr; })) {
            throw std::runtime_error("GpuResourceFactory: default material image upload failed.");
        }
        for (std::size_t i = 0; i < kMaterialImageRoleCount; ++i) {
            m_defaultMaterialImages[i] = std::move(created[i]);
        }
    }
    return m_defaultMaterialImages[slot];
}

MaterialTexture GpuResourceFactory::CreateMaterialVariant(const MaterialImages& images, const MaterialParams& params) const
{
    // Resolve every slot first, so a failing default image never costs a set.
    MaterialImages resolved;
    for (std::size_t i = 0; i < kMaterialImageRoleCount; ++i) {
        resolved[i] = images[i] ? images[i] : DefaultMaterialImage(static_cast<MaterialImageRole>(i));
    }

    Buffer paramsBuffer = CreateBuffer(
        sizeof(MaterialParams), BufferUsage::UniformBuffer, BufferMemoryUsage::CpuToGpu, "MaterialParams");
    paramsBuffer.Upload(&params, sizeof(params));

    const VkDescriptorSet descriptorSet = m_materialSets->Allocate();
    MaterialSetLease lease(m_materialSets, descriptorSet); // Frees the set if anything below throws.

    // Image bindings in role order: base color, normal, metallic-roughness, occlusion.
    static constexpr std::uint32_t kImageBindings[kMaterialImageRoleCount] = { 0, 2, 3, 4 };
    VkDescriptorImageInfo imageInfos[kMaterialImageRoleCount]{};
    VkWriteDescriptorSet writes[kMaterialImageRoleCount + 1]{};
    for (std::size_t i = 0; i < kMaterialImageRoleCount; ++i) {
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[i].imageView = resolved[i]->View();
        imageInfos[i].sampler = resolved[i]->Sampler();
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = kImageBindings[i];
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &imageInfos[i];
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = paramsBuffer.Native();
    bufferInfo.range = sizeof(MaterialParams);
    VkWriteDescriptorSet& paramsWrite = writes[kMaterialImageRoleCount];
    paramsWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    paramsWrite.dstSet = descriptorSet;
    paramsWrite.dstBinding = 1;
    paramsWrite.descriptorCount = 1;
    paramsWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    paramsWrite.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(m_device, kMaterialImageRoleCount + 1, writes, 0, nullptr);

    return MaterialTexture{ std::move(resolved), std::move(paramsBuffer), std::move(lease) };
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
