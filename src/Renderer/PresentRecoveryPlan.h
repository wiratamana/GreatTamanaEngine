#pragma once

// Vulkan-free decision of what to submit after a present frame failed while its swapchain image was acquired.

#include <array>
#include <cstddef>
#include <cstdint>

namespace gte {

enum class PresentRecoveryStep : std::uint8_t {
    AbandonCapture,       // Drop the readback slot whose copy will never run.
    ResetCommandBuffer,   // Discard the half-recorded buffer.
    TransitionToPresent,  // Record only the image barrier to the present layout.
    ResetFence,           // Unsignal the in-flight fence right before the submit.
    SubmitAndSignal,      // Wait image-available, signal render-finished, signal the fence.
    Present,              // Hand the image back to the swapchain.
    AdvanceFrame,         // Move to the next frame slot.
};

struct PresentRecoveryInput {
    bool deviceLost = false;
    bool imageAcquired = false;
    bool fenceReset = false;    // The in-flight fence is already unsignaled.
    bool capturePending = false;
};

struct PresentRecoveryPlan {
    static constexpr std::size_t kMaxSteps = 7;

    std::array<PresentRecoveryStep, kMaxSteps> steps{};
    std::uint8_t count = 0;

    const PresentRecoveryStep* begin() const noexcept { return steps.data(); }
    const PresentRecoveryStep* end() const noexcept { return steps.data() + count; }
    bool Contains(PresentRecoveryStep step) const noexcept;
};

// A pending capture is always abandoned. The image is returned only when the device is alive.
PresentRecoveryPlan PlanPresentRecovery(const PresentRecoveryInput& in) noexcept;

} // namespace gte
