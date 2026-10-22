#pragma once

#include <volk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Outcome of the validation request made at instance creation.
enum class ValidationState : std::uint8_t {
    Off,          // Not requested.
    On,           // Layer and debug messenger active.
    LayerMissing, // Requested, but the layer is not installed.
};

// RAII wrapper around a VkInstance and its debug messenger (when validation is on). Also does the
// one-time volkInitialize() and loads instance-level function pointers; VulkanDevice loads the
// device-level ones.
class VulkanInstance {
public:
    // requiredExtensions: window-system extensions (ISurfaceProvider::VulkanInstanceExtensions()).
    // enableValidation requests the Khronos validation layer and debug utils; without the layer on this
    // machine it runs unvalidated instead of failing (see Validation()).
    VulkanInstance(const std::string& applicationName,
                   const std::vector<std::string>& requiredExtensions,
                   bool enableValidation);
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    VulkanInstance(VulkanInstance&& other) noexcept;
    VulkanInstance& operator=(VulkanInstance&& other) noexcept;

    VkInstance Native() const noexcept { return m_instance; }
    ValidationState Validation() const noexcept { return m_validation; }

private:
    void CreateInstance(const std::string& applicationName,
                         const std::vector<std::string>& requiredExtensions,
                         bool wantValidation);
    void CreateDebugMessenger();
    void Destroy() noexcept;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    ValidationState m_validation = ValidationState::Off;
};

} // namespace gte
