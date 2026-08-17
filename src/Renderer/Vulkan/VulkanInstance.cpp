#include "VulkanInstance.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace gte {

namespace {

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

bool IsLayerAvailable(const char* layerName)
{
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());

    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, layerName) == 0) {
            return true;
        }
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/)
{
    const char* severityText = "INFO";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severityText = "ERROR";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severityText = "WARNING";
    }

    std::fprintf(stderr, "[Vulkan][%s] %s\n", severityText,
        callbackData != nullptr ? callbackData->pMessage : "(no message)");
    return VK_FALSE;
}

} // namespace

VulkanInstance::VulkanInstance(const std::string& applicationName,
                                const std::vector<std::string>& requiredExtensions,
                                bool enableValidation)
{
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("volkInitialize() failed - no Vulkan loader (vulkan-1.dll) found on this system.");
    }

    CreateInstance(applicationName, requiredExtensions, enableValidation);
    volkLoadInstance(m_instance);

    if (m_validationEnabled) {
        CreateDebugMessenger();
    }
}

VulkanInstance::~VulkanInstance()
{
    Destroy();
}

VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept
    : m_instance(std::exchange(other.m_instance, VK_NULL_HANDLE))
    , m_debugMessenger(std::exchange(other.m_debugMessenger, VK_NULL_HANDLE))
    , m_validationEnabled(other.m_validationEnabled)
{
}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept
{
    if (this != &other) {
        Destroy();
        m_instance = std::exchange(other.m_instance, VK_NULL_HANDLE);
        m_debugMessenger = std::exchange(other.m_debugMessenger, VK_NULL_HANDLE);
        m_validationEnabled = other.m_validationEnabled;
    }
    return *this;
}

void VulkanInstance::CreateInstance(const std::string& applicationName,
                                     const std::vector<std::string>& requiredExtensions,
                                     bool wantValidation)
{
    m_validationEnabled = wantValidation && IsLayerAvailable(kValidationLayerName);
    if (wantValidation && !m_validationEnabled) {
        std::fprintf(stderr,
            "[Vulkan] Validation was requested but %s is not available on this system - continuing without it.\n",
            kValidationLayerName);
    }

    std::vector<const char*> extensions;
    extensions.reserve(requiredExtensions.size() + 1);
    for (const auto& ext : requiredExtensions) {
        extensions.push_back(ext.c_str());
    }
    if (m_validationEnabled) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> layers;
    if (m_validationEnabled) {
        layers.push_back(kValidationLayerName);
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = applicationName.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "GreatTamanaEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateInstance failed (VkResult=" + std::to_string(result) + ")");
    }
}

void VulkanInstance::CreateDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugMessengerCallback;

    if (vkCreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create debug utils messenger - continuing without it.\n");
        m_debugMessenger = VK_NULL_HANDLE;
    }
}

void VulkanInstance::Destroy() noexcept
{
    if (m_debugMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

} // namespace gte
