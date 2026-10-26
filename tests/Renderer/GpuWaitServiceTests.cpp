// Tier-2 (headless GPU) test of GpuWaitService against a real, never-signaled fence.

#include "Renderer/Vulkan/GpuWaitService.h"

#include "../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <chrono>
#include <mutex>

namespace gte {
namespace {

// Owns one unsignaled fence for the test's lifetime.
class UnsignaledFence {
public:
    explicit UnsignaledFence(VkDevice device) : m_device(device)
    {
        VkFenceCreateInfo info{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        if (vkCreateFence(m_device, &info, nullptr, &m_fence) != VK_SUCCESS) {
            m_fence = VK_NULL_HANDLE;
        }
    }
    ~UnsignaledFence()
    {
        if (m_fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, m_fence, nullptr);
        }
    }
    UnsignaledFence(const UnsignaledFence&) = delete;
    UnsignaledFence& operator=(const UnsignaledFence&) = delete;

    VkFence Get() const noexcept { return m_fence; }

private:
    VkDevice m_device;
    VkFence m_fence = VK_NULL_HANDLE;
};

TEST(GpuWaitServiceTest, UnsignaledFenceTimesOutInsideTheGivenBound)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const Renderer::VulkanContextInfo info = fixture.GetRenderer().GetImGuiVulkanBackendInfo();
    std::mutex queueMutex;
    GpuWaitService service(info.device, info.graphicsQueue, queueMutex);
    UnsignaledFence fence(info.device);
    ASSERT_NE(fence.Get(), static_cast<VkFence>(VK_NULL_HANDLE));

    const auto begin = std::chrono::steady_clock::now();
    const BlockOutcome outcome = service.WaitFence(
        fence.Get(), std::chrono::milliseconds(50), WaitClass::Frame, BlockSite::PresentInFlight, "timeout test");
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_EQ(outcome, BlockOutcome::TimedOut);
    EXPECT_GE(elapsed, std::chrono::milliseconds(45));
    EXPECT_LT(elapsed, std::chrono::milliseconds(500));
    EXPECT_FALSE(service.DeviceLost());
    EXPECT_EQ(service.ConsecutiveFrameTimeouts(), 1u);
}

TEST(GpuWaitServiceTest, FourthConsecutiveFrameTimeoutLosesTheDevice)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const Renderer::VulkanContextInfo info = fixture.GetRenderer().GetImGuiVulkanBackendInfo();
    std::mutex queueMutex;
    GpuWaitService service(info.device, info.graphicsQueue, queueMutex);
    UnsignaledFence fence(info.device);
    ASSERT_NE(fence.Get(), static_cast<VkFence>(VK_NULL_HANDLE));

    BlockOutcome outcome = BlockOutcome::Completed;
    for (std::uint32_t i = 0; i <= kToleratedFrameTimeouts; ++i) {
        outcome = service.WaitFence(
            fence.Get(), std::chrono::milliseconds(10), WaitClass::Frame, BlockSite::PresentInFlight, "timeout test");
    }

    EXPECT_EQ(outcome, BlockOutcome::DeviceLost);
    EXPECT_TRUE(service.DeviceLost());
}

} // namespace
} // namespace gte
