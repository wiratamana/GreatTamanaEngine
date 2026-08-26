#include "VulkanQueryPool.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace gte {

VulkanQueryPool::VulkanQueryPool(VkDevice device, std::uint32_t slotCount)
    : m_device(device)
    , m_slotCount(slotCount)
{
    VkQueryPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    createInfo.queryCount = slotCount;

    const VkResult result = vkCreateQueryPool(m_device, &createInfo, nullptr, &m_pool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("VulkanQueryPool: vkCreateQueryPool failed (VkResult=" + std::to_string(result) + ")");
    }
}

VulkanQueryPool::~VulkanQueryPool()
{
    Destroy();
}

VulkanQueryPool::VulkanQueryPool(VulkanQueryPool&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_pool(std::exchange(other.m_pool, VK_NULL_HANDLE))
    , m_slotCount(other.m_slotCount)
{
}

VulkanQueryPool& VulkanQueryPool::operator=(VulkanQueryPool&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_pool = std::exchange(other.m_pool, VK_NULL_HANDLE);
        m_slotCount = other.m_slotCount;
    }
    return *this;
}

void VulkanQueryPool::Destroy() noexcept
{
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

} // namespace gte
