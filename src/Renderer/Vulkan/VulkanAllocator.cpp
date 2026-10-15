// Exactly one .cpp compiles VMA's implementation; this is it. The VMA_*_VULKAN_FUNCTIONS macros
// are set in VulkanAllocator.h.
#define VMA_IMPLEMENTATION
#include "VulkanAllocator.h"

#include <stdexcept>
#include <utility>

namespace gte {

VulkanAllocator::VulkanAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, std::uint32_t apiVersion)
{
    // volk has resolved the two proc-address entry points by now; VMA fetches the rest itself.
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = apiVersion;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        throw std::runtime_error("VulkanAllocator: vmaCreateAllocator failed.");
    }
}

VulkanAllocator::~VulkanAllocator()
{
    Destroy();
}

VulkanAllocator::VulkanAllocator(VulkanAllocator&& other) noexcept
    : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE))
{
}

VulkanAllocator& VulkanAllocator::operator=(VulkanAllocator&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
    }
    return *this;
}

void VulkanAllocator::Destroy() noexcept
{
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
}

std::vector<VmaBudget> VulkanAllocator::GetHeapBudgets() const
{
    const VkPhysicalDeviceMemoryProperties* memoryProperties = nullptr;
    vmaGetMemoryProperties(m_allocator, &memoryProperties);

    VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
    vmaGetHeapBudgets(m_allocator, budgets);

    return std::vector<VmaBudget>(budgets, budgets + memoryProperties->memoryHeapCount);
}

} // namespace gte
