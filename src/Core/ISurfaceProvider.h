#pragma once

#include <string>
#include <vector>

// Forward-declared Vulkan handle types, so this header does not need to
// include <volk.h>/<vulkan/vulkan.h> - the same trick Window.h used to define
// on its own (see Window.h's own comment history); the canonical home for
// these two typedefs now lives HERE instead, since Window implements this
// interface rather than defining its own copy of them (editor-core-separation-1
// campaign, PHASE10 - task_manager/editor-core-separation-1/
// PHASE10_ISURFACEPROVIDER_INTERFACE_AND_WINDOW_INVERSION.md). These match
// Vulkan's own handle-macro expansion exactly (VK_DEFINE_HANDLE /
// VK_DEFINE_NON_DISPATCHABLE_HANDLE on 64-bit), so they are interchangeable
// with the "real" VkInstance/VkSurfaceKHR wherever this header and the real
// Vulkan headers both end up included in the same translation unit (e.g.
// Renderer.cpp) - this is the same trick SDL's own <SDL3/SDL_vulkan.h> uses.
struct VkInstance_T;
using VkInstance = VkInstance_T*;
struct VkSurfaceKHR_T;
using VkSurfaceKHR = VkSurfaceKHR_T*;

namespace gte {

// editor-core-separation-1 campaign, PHASE10
// (task_manager/editor-core-separation-1/
// PHASE10_ISURFACEPROVIDER_INTERFACE_AND_WINDOW_INVERSION.md) - gte_core's
// own abstraction over "whatever owns a real OS window/surface Renderer can
// build a Vulkan swapchain against." Renderer/VulkanSurface depend on THIS
// interface instead of the concrete Window class, so gte_core never needs to
// name Window directly at the Renderer/VulkanSurface boundary. This is the
// seam Core (Phase 12) will be constructed through, and the seam a future
// headless test fixture (Phase 18) and a future Player host will use instead
// of a real Window.
//
// Deliberately reuses Window's EXISTING method names/signatures verbatim
// (VulkanInstanceExtensions()/CreateVulkanSurface()/Width()/Height()) - a
// deliberate minimal-diff choice (design doc Section 5.2): Window only needs
// to (a) inherit from this interface and (b) turn its previously `static`
// VulkanInstanceExtensions() into a real virtual instance method - every
// other method already matched exactly.
class ISurfaceProvider {
public:
    virtual ~ISurfaceProvider() = default;

    // Vulkan instance extension names this surface provider's platform/
    // window needs in order to create a VkSurfaceKHR for it (e.g.
    // VK_KHR_win32_surface).
    virtual std::vector<std::string> VulkanInstanceExtensions() const = 0;

    // Creates a VkSurfaceKHR for this surface provider under the given
    // VkInstance. Ownership of the returned surface transfers to the
    // caller: it must be destroyed with vkDestroySurfaceKHR(instance,
    // surface, ...) before the instance is destroyed (see VulkanSurface,
    // which wraps exactly that).
    virtual VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const = 0;

    virtual int Width() const noexcept = 0;
    virtual int Height() const noexcept = 0;
};

} // namespace gte
