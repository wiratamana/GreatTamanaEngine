// Defining VMA_IMPLEMENTATION before vk_mem_alloc.h is first included in
// this translation unit (via VulkanAllocator.h below) is what compiles VMA's
// actual implementation here - exactly one .cpp in the whole project must do
// this. See VulkanAllocator.h for the VMA_STATIC_VULKAN_FUNCTIONS/
// VMA_DYNAMIC_VULKAN_FUNCTIONS reasoning.
#define VMA_IMPLEMENTATION
#include "VulkanAllocator.h"

#include <stdexcept>
#include <utility>

namespace gte {

VulkanAllocator::VulkanAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, std::uint32_t apiVersion)
{
    // volk has already resolved these two by the time this constructor
    // runs (volkInitialize() -> vkGetInstanceProcAddr, volkLoadInstance()/
    // volkLoadDevice() -> vkGetDeviceProcAddr - see VulkanInstance/
    // VulkanDevice). Handing VMA just these two lets it fetch every other
    // function it needs itself (VMA_DYNAMIC_VULKAN_FUNCTIONS), with no
    // dependency on a classic Vulkan loader import lib.
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

} // namespace gte
