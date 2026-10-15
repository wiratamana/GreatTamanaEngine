#pragma once

#include <volk.h>

#include <string>
#include <vector>

namespace gte {

// RAII wrapper around a VkInstance and its debug messenger (when validation is on). Also does the
// one-time volkInitialize() and loads instance-level function pointers; VulkanDevice loads the
// device-level ones.
class VulkanInstance {
public:
    // requiredExtensions: window-system extensions (ISurfaceProvider::VulkanInstanceExtensions()).
    // enableValidation requests the Khronos validation layer and debug utils; without the layer on this
    // machine it runs unvalidated instead of failing.
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
