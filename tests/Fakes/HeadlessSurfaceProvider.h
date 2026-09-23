#pragma once

// editor-core-separation-1 campaign, PHASE18
// (task_manager/editor-core-separation-1/
// PHASE18_HEADLESS_TEST_FIXTURE_AND_CORE_STANDALONE_PROBE.md) - a fake,
// headless ISurfaceProvider, used ONLY by tests, proving `gte::Core` can be
// constructed and driven without any real Window/SDL involved at all.
//
// THIS FILE'S OWN STEP-1 QUESTION, ANSWERED BY READING THE REAL CODE (per
// this phase's own explicit instruction: "do not assume either way"):
// Renderer's constructor (src/Renderer/Renderer.cpp) EAGERLY builds a real
// VkInstance -> VkSurfaceKHR -> VkPhysicalDevice/VkDevice -> a real
// swapchain (via FramePresenter, constructed unconditionally inside
// Renderer's own initializer list) - there is no lazy/deferred path. A
// literally-null (VK_NULL_HANDLE) surface handle is real, live undefined
// behavior once VulkanDevice::PickPhysicalDevice() -> IsDeviceSuitable() ->
// FindQueueFamilies() calls vkGetPhysicalDeviceSurfaceSupportKHR() with it
// (that function's own VUID explicitly requires a valid, non-null surface
// handle) - so "Core's construction path can tolerate never actually
// presenting" (this phase's own Step 1, option (a)) is FALSE: a genuinely
// null/no-op CreateVulkanSurface() is not safe here, confirmed by reading
// the real constructor chain, not assumed.
//
// The correct answer is this phase's own Step 1 option (b) - "a real
// Vulkan surface is unavoidable for full construction" - but a REAL,
// VALID VkSurfaceKHR does not have to mean a real OS window/SDL: this
// project's own vendored Vulkan headers (fetched by cmake/FetchVulkan.cmake)
// already carry VK_EXT_headless_surface completely unconditionally (see
// include/vulkan/vulkan_core.h - "VK_EXT_headless_surface is a preprocessor
// guard... #define VK_EXT_headless_surface 1", never gated behind any
// VK_USE_PLATFORM_* macro the way VK_KHR_win32_surface is), and volk.c/
// volk.h load vkCreateHeadlessSurfaceEXT's function pointer unconditionally
// too (`#if defined(VK_EXT_headless_surface)` - always true here). This is
// EXACTLY the mechanism tests/CMakeLists.txt's own pre-existing "Tier 2
// (GPU-dependent) tests" section already anticipated, verbatim: "A future
// GpuTestFixture could build a headless VkSurfaceKHR via
// VK_EXT_headless_surface... instead of a real Window, then GTEST_SKIP()
// the whole fixture at runtime if instance/device creation fails."
//
// Whether this actually succeeds on any given machine depends entirely on
// whether that machine's installed Vulkan driver/loader genuinely reports
// VK_EXT_headless_surface as an available INSTANCE extension at
// vkCreateInstance() time - a real, honest, machine-dependent capability
// gap (NOT assumed to always work) this fixture's own caller
// (CoreHeadlessConstructionTests.cpp) must GTEST_SKIP() around, mirroring
// this same test suite's own existing convention for a comparable
// machine-dependent gap (PmxLoaderRealModelSmokeTest.
// LoadsAnMmdModelIfPresentOnThisMachine, Assets/PmxLoaderTests.cpp) - see
// PHASE18_COMPLETION_REPORT.md for the actual, empirically-confirmed result
// on this development machine.

#include "../../src/Core/ISurfaceProvider.h"

#include <volk.h>

#include <stdexcept>
#include <string>

namespace gte {

class HeadlessSurfaceProvider : public ISurfaceProvider {
public:
    // No real platform surface extension requested at all (no
    // VK_KHR_win32_surface/etc) - only the two extensions
    // vkCreateHeadlessSurfaceEXT() itself needs (VK_KHR_surface, the base
    // extension every VkSurfaceKHR-producing extension builds on, plus
    // VK_EXT_headless_surface itself).
    std::vector<std::string> VulkanInstanceExtensions() const override
    {
        return { "VK_KHR_surface", "VK_EXT_headless_surface" };
    }

    // Real, valid VkSurfaceKHR, backed by zero real OS window/SDL - see
    // this file's own top-of-file comment for the full reasoning. Throws
    // std::runtime_error (never silently returns VK_NULL_HANDLE) if this
    // machine's driver/loader doesn't actually support the extension, so
    // the caller gets a clear, catchable, descriptive failure instead of
    // undefined behavior further down Renderer's own constructor chain.
    VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const override
    {
        VkHeadlessSurfaceCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const VkResult result = vkCreateHeadlessSurfaceEXT(instance, &createInfo, nullptr, &surface);
        if (result != VK_SUCCESS) {
            throw std::runtime_error(
                "HeadlessSurfaceProvider::CreateVulkanSurface: vkCreateHeadlessSurfaceEXT failed (VkResult=" +
                std::to_string(static_cast<int>(result)) +
                ") - this machine's Vulkan driver/loader most likely does not report VK_EXT_headless_surface as "
                "available.");
        }
        return surface;
    }

    // Fixed, fake, non-zero values - never the size of any real window
    // (there is none).
    int Width() const noexcept override { return 64; }
    int Height() const noexcept override { return 64; }
};

} // namespace gte
