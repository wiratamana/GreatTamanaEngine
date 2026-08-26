#include "VulkanDevice.h"

#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gte {

namespace {

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    bool Complete() const { return graphics.has_value() && present.has_value(); }
};

QueueFamilies FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    QueueFamilies result;

    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (std::uint32_t i = 0; i < count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !result.graphics.has_value()) {
            result.graphics = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport && !result.present.has_value()) {
            result.present = i;
        }

        if (result.Complete()) {
            break;
        }
    }

    return result;
}

bool SupportsSwapchainExtension(VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());

    for (const auto& ext : extensions) {
        if (std::strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            return true;
        }
    }
    return false;
}

bool SupportsDynamicRendering(VkPhysicalDevice device)
{
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &features13;

    vkGetPhysicalDeviceFeatures2(device, &features2);
    return features13.dynamicRendering == VK_TRUE;
}

bool IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    const QueueFamilies families = FindQueueFamilies(device, surface);
    if (!families.Complete()) {
        return false;
    }
    if (!SupportsSwapchainExtension(device)) {
        return false;
    }
    if (!SupportsDynamicRendering(device)) {
        return false;
    }

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    std::uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    return formatCount > 0 && presentModeCount > 0;
}

int ScoreDevice(VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);

    int score = 1;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 100;
    }
    return score;
}

} // namespace

VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface)
{
    PickPhysicalDevice(instance, surface);
    CreateLogicalDevice();
    QueryTimestampCapability();
}

VulkanDevice::~VulkanDevice()
{
    Destroy();
}

VulkanDevice::VulkanDevice(VulkanDevice&& other) noexcept
    : m_physicalDevice(std::exchange(other.m_physicalDevice, VK_NULL_HANDLE))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_graphicsQueue(std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE))
    , m_presentQueue(std::exchange(other.m_presentQueue, VK_NULL_HANDLE))
    , m_graphicsFamily(other.m_graphicsFamily)
    , m_presentFamily(other.m_presentFamily)
    , m_timestampCapability(other.m_timestampCapability)
{
}

VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_physicalDevice = std::exchange(other.m_physicalDevice, VK_NULL_HANDLE);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_graphicsQueue = std::exchange(other.m_graphicsQueue, VK_NULL_HANDLE);
        m_presentQueue = std::exchange(other.m_presentQueue, VK_NULL_HANDLE);
        m_graphicsFamily = other.m_graphicsFamily;
        m_presentFamily = other.m_presentFamily;
        m_timestampCapability = other.m_timestampCapability;
    }
    return *this;
}

void VulkanDevice::PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
{
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) {
        throw std::runtime_error("No Vulkan-capable physical devices found on this system.");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    for (VkPhysicalDevice device : devices) {
        if (!IsDeviceSuitable(device, surface)) {
            continue;
        }
        const int score = ScoreDevice(device);
        if (score > bestScore) {
            bestScore = score;
            best = device;
        }
    }

    if (best == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "No suitable Vulkan physical device found (need graphics+present queues, "
            "VK_KHR_swapchain, and dynamic rendering support).");
    }

    m_physicalDevice = best;

    const QueueFamilies families = FindQueueFamilies(m_physicalDevice, surface);
    m_graphicsFamily = families.graphics.value();
    m_presentFamily = families.present.value();
}

void VulkanDevice::CreateLogicalDevice()
{
    std::set<std::uint32_t> uniqueFamilies = { m_graphicsFamily, m_presentFamily };

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    const float queuePriority = 1.0f;
    for (std::uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = family;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features13;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    const VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDevice failed (VkResult=" + std::to_string(result) + ")");
    }

    // Load device-level function pointers directly (bypasses the instance
    // dispatch table for lower call overhead) - safe to call even with
    // multiple devices since this project only ever creates one.
    volkLoadDevice(m_device);

    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);
}

VkFormat VulkanDevice::PickDepthFormat() const
{
    // Depth-only first (see the declaration comment in VulkanDevice.h for
    // why) - combined depth+stencil formats only as a fallback.
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, candidate, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return candidate;
        }
    }

    throw std::runtime_error(
        "VulkanDevice::PickDepthFormat: no supported depth/stencil attachment format found "
        "(none of D32_SFLOAT/D32_SFLOAT_S8_UINT/D24_UNORM_S8_UINT) - should be impossible per the Vulkan spec.");
}

void VulkanDevice::QueryTimestampCapability()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, families.data());

    const std::uint32_t validBits =
        (m_graphicsFamily < families.size()) ? families[m_graphicsFamily].timestampValidBits : 0;

    m_timestampCapability = InterpretTimestampCapability(
        properties.limits.timestampComputeAndGraphics == VK_TRUE,
        properties.limits.timestampPeriod,
        validBits);
}

void VulkanDevice::Destroy() noexcept
{
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    m_physicalDevice = VK_NULL_HANDLE;
}

} // namespace gte
