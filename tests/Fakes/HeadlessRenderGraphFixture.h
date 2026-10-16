#pragma once

// Headless (VK_EXT_headless_surface) GPU fixture: a real Renderer and RenderGraph, no Core/Game/Editor/SDL.
// Usage: if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }  then fixture.RunSynchronousFrame(build).

#include "../../src/Renderer/Compute/ComputePipeline.h"
#include "../../src/Renderer/DescriptorSet.h"
#include "../../src/Renderer/Renderer.h"
#include "../../src/Renderer/RenderGraph/RenderGraph.h"
#include "HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gte {

class HeadlessRenderGraphFixture {
public:
    HeadlessRenderGraphFixture()
    {
        try {
            m_renderer = std::make_unique<Renderer>(m_surfaceProvider);
            m_renderGraph = std::make_unique<rg::RenderGraph>(*m_renderer);
        } catch (const std::exception& e) {
            m_skipReason = std::string("HeadlessRenderGraphFixture: construction failed - this machine's "
                                        "Vulkan driver/loader most likely does not support "
                                        "VK_EXT_headless_surface (see HeadlessSurfaceProvider.h). Underlying "
                                        "error: ") + e.what();
        }
    }

    bool IsUsable() const noexcept { return m_renderer != nullptr && m_renderGraph != nullptr; }
    const std::string& SkipReason() const noexcept { return m_skipReason; }

    Renderer& GetRenderer() { return *m_renderer; }
    rg::RenderGraph& GetRenderGraph() { return *m_renderGraph; }

    // Test-only access to the Vulkan device (this fixture is a friend of Renderer).
    VkDevice Device() const noexcept { return m_renderer->GetVulkanContextInfo().device; }

    // Raw Renderer entry points the tests need; only friends may call them.
    ComputePipeline CreateComputePipeline(const std::string& spirvPath) const { return m_renderer->CreateComputePipeline(spirvPath); }
    DescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout) const { return m_renderer->AllocateDescriptorSet(layout); }
    DescriptorSetHandle AllocateInstanceBufferDescriptorSet() const
    {
        return RegisterDescriptorSet(
            m_renderer->AllocateDescriptorSet(m_renderer->InstanceBufferDescriptorSetLayout()).Native());
    }
    void BeginGraphPassRecording(const rg::RecordingContext& recording)
    {
        m_renderer->BeginGraphPassRecording(rg::RecordingAccess::Native(recording), {});
    }
    void EndGraphPassRecording() noexcept { m_renderer->EndGraphPassRecording(); }

    // Square 2D image edge no device can create: one past maxImageDimension2D.
    std::uint32_t UnsupportedImageExtent() const
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_renderer->GetVulkanContextInfo().physicalDevice, &properties);
        return properties.limits.maxImageDimension2D + 1u;
    }

    // Buffer size no device can allocate: one byte more than its largest memory heap.
    std::size_t UnsupportedBufferSize() const
    {
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(m_renderer->GetVulkanContextInfo().physicalDevice, &memory);
        VkDeviceSize largest = 0;
        for (std::uint32_t i = 0; i < memory.memoryHeapCount; ++i) {
            largest = std::max(largest, memory.memoryHeaps[i].size);
        }
        return static_cast<std::size_t>(largest) + 1u;
    }

    // Drives one SynchronousImmediateReadback Execute(); `build` declares whatever passes/resources a test needs.
    void RunSynchronousFrame(
        const std::function<std::vector<rg::TextureHandle>(rg::RenderGraphBuilder&)>& build)
    {
        const rg::RecordingContext cmd = m_renderer->BeginOffscreenRenderGraphRecording();
        m_renderGraph->Execute(cmd, rg::ExecuteTimingMode::SynchronousImmediateReadback, build);
        m_renderer->EndOffscreenRenderGraphRecording();
    }

private:
    HeadlessSurfaceProvider m_surfaceProvider;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<rg::RenderGraph> m_renderGraph;
    std::string m_skipReason;
};

} // namespace gte
