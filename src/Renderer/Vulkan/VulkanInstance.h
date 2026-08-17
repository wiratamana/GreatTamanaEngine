#pragma once

#include <volk.h>

#include <string>
#include <vector>

namespace gte {

// RAII wrapper around a VkInstance (+ its VK_EXT_debug_utils messenger, if
// validation is enabled). Owns the underlying Vulkan handles for its entire
// lifetime: acquired in the constructor, released in the destructor.
//
// Also responsible for the one-time volkInitialize() call and for loading
// instance-level function pointers via volkLoadInstance() once the instance
// exists. VulkanDevice::Native() loads device-level pointers separately via
// volkLoadDevice() for faster dispatch.
class VulkanInstance {
public:
    // requiredExtensions: platform-specific extensions the window system
    // needs (e.g. from Window::VulkanInstanceExtensions()). enableValidation
    // requests VK_LAYER_KHRONOS_validation + VK_EXT_debug_utils; if the
    // validation layer isn't available on this machine, this silently falls
    // back to running without it rather than failing the whole application.
    VulkanInstance(const std::string& applicationName,
                   const std::vector<std::string>& requiredExtensions,
                   bool enableValidation);
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    VulkanInstance(VulkanInstance&& other) noexcept;
    VulkanInstance& operator=(VulkanInstance&& other) noexcept;

    VkInstance Native() const noexcept { return m_instance; }
    bool ValidationEnabled() const noexcept { return m_validationEnabled; }

private:
    void CreateInstance(const std::string& applicationName,
                         const std::vector<std::string>& requiredExtensions,
                         bool wantValidation);
    void CreateDebugMessenger();
    void Destroy() noexcept;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    bool m_validationEnabled = false;
};

} // namespace gte
