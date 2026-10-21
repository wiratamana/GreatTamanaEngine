#include "MaterialSetPool.h"

#include "../Core/Logging.h"
#include "GraphicsHandleRegistry.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace gte {

namespace {

constexpr PoolCapacity SetCost() noexcept
{
    return PoolCapacity{ .sets = 1,
        .combinedSamplers = MaterialSetPool::kSamplersPerSet,
        .uniformBuffers = MaterialSetPool::kUniformBuffersPerSet };
}

} // namespace

MaterialSetPool::MaterialSetPool(VkDevice device, VkDescriptorSetLayout layout, std::uint32_t capacity)
    : m_device(device)
    , m_layout(layout)
    , m_capacity(capacity)
{
    const VkDescriptorPoolSize sizes[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, capacity * kSamplersPerSet },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, capacity * kUniformBuffersPerSet },
    };

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = capacity;
    info.poolSizeCount = 2;
    info.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_device, &info, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("MaterialSetPool: vkCreateDescriptorPool failed");
    }

    m_budget.AddCapacity(PoolCapacity{ .sets = capacity,
        .combinedSamplers = capacity * kSamplersPerSet,
        .uniformBuffers = capacity * kUniformBuffersPerSet });
}

MaterialSetPool::~MaterialSetPool()
{
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_pool, nullptr);
    }
}

VkDescriptorSet MaterialSetPool::Allocate()
{
    const std::scoped_lock lock(m_mutex);

    VkDescriptorSetAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool = m_pool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &m_layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult result = vkAllocateDescriptorSets(m_device, &info, &set);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("MaterialSetPool: vkAllocateDescriptorSets failed (capacity "
            + std::to_string(m_capacity) + " sets, " + std::to_string(m_live.size())
            + " in use; raise kMaxMaterials), VkResult " + std::to_string(static_cast<int>(result)));
    }

    m_live.insert(set);
    m_budget.OnAllocated(SetCost());
    if (m_budget.TakeWarning()) {
        GTE_LOG_WARNING("MaterialSetPool",
            "Material descriptor pool is above 75% full: " + std::to_string(m_live.size()) + "/"
                + std::to_string(m_capacity) + " sets.");
    }
    return set;
}

void MaterialSetPool::Free(VkDescriptorSet set) noexcept
{
    if (set == VK_NULL_HANDLE) {
        return;
    }
    const std::scoped_lock lock(m_mutex);
    if (m_live.erase(set) == 0) {
        return;
    }
    vkFreeDescriptorSets(m_device, m_pool, 1, &set);
    m_budget.OnFreed(SetCost());
    m_budget.TakeWarning(); // Re-arms the warning once use falls low enough.
}

std::uint32_t MaterialSetPool::InUse() const
{
    const std::scoped_lock lock(m_mutex);
    return static_cast<std::uint32_t>(m_live.size());
}

MaterialSetLease::MaterialSetLease(std::shared_ptr<MaterialSetPool> pool, VkDescriptorSet set) noexcept
    : m_pool(std::move(pool))
    , m_set(set)
{
    if (m_pool && m_set != VK_NULL_HANDLE) {
        m_handle = RegisterDescriptorSet(m_set);
    } else {
        m_set = VK_NULL_HANDLE;
    }
}

MaterialSetLease::~MaterialSetLease()
{
    Reset();
}

MaterialSetLease::MaterialSetLease(MaterialSetLease&& other) noexcept
    : m_pool(std::move(other.m_pool))
    , m_set(std::exchange(other.m_set, VK_NULL_HANDLE))
    , m_handle(std::exchange(other.m_handle, DescriptorSetHandle::Invalid))
{
}

MaterialSetLease& MaterialSetLease::operator=(MaterialSetLease&& other) noexcept
{
    if (this != &other) {
        Reset();
        m_pool = std::move(other.m_pool);
        m_set = std::exchange(other.m_set, VK_NULL_HANDLE);
        m_handle = std::exchange(other.m_handle, DescriptorSetHandle::Invalid);
    }
    return *this;
}

void MaterialSetLease::Reset() noexcept
{
    if (m_handle != DescriptorSetHandle::Invalid) {
        UnregisterDescriptorSet(m_handle);
        m_handle = DescriptorSetHandle::Invalid;
    }
    if (m_pool) {
        m_pool->Free(m_set);
        m_pool.reset();
    }
    m_set = VK_NULL_HANDLE;
}

} // namespace gte
