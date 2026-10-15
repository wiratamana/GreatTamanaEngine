#pragma once

#include <string>
#include <vector>

// Forward-declared Vulkan handle types, so this header does not include <volk.h>. They match Vulkan's
// handle macro expansion on 64-bit, so they interchange with the real types where both are visible.
struct VkInstance_T;
using VkInstance = VkInstance_T*;
struct VkSurfaceKHR_T;
using VkSurfaceKHR = VkSurfaceKHR_T*;

namespace gte {

// Whatever owns an OS window/surface the renderer can build a swapchain against. The renderer depends on
// this interface, not on the concrete Window; headless test fixtures and standalone hosts implement it too.
class ISurfaceProvider {
public:
    virtual ~ISurfaceProvider() = default;

    // Instance extensions the platform window needs to create a surface (e.g. VK_KHR_win32_surface).
    virtual std::vector<std::string> VulkanInstanceExtensions() const = 0;

    // Creates a surface under `instance`. The caller owns it and destroys it before the instance
    // (VulkanSurface wraps exactly that).
    virtual VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const = 0;

    virtual int Width() const noexcept = 0;
    virtual int Height() const noexcept = 0;
};

} // namespace gte
