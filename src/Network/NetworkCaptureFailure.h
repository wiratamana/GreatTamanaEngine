#pragma once

#include "../Application/FrameCaptureBridge.h"

namespace gte::Network {

// HTTP status for a failed capture request. A busy or lost GPU is a service problem (503), not a bad request.
constexpr int HttpStatusForCaptureFailure(FrameCaptureFailureReason reason) noexcept
{
    switch (reason) {
    case FrameCaptureFailureReason::TimedOut: return 504;
    case FrameCaptureFailureReason::GpuBusy:
    case FrameCaptureFailureReason::DeviceLost: return 503;
    case FrameCaptureFailureReason::TargetNotAvailable: break;
    }
    return 409;
}

// Reason text for the GPU-side failures; empty for the other reasons, which keep their route-specific text.
constexpr const char* GpuFailureText(FrameCaptureFailureReason reason) noexcept
{
    switch (reason) {
    case FrameCaptureFailureReason::GpuBusy: return "the GPU did not finish in time; try again";
    case FrameCaptureFailureReason::DeviceLost: return "GPU device lost, rendering stopped";
    case FrameCaptureFailureReason::TargetNotAvailable:
    case FrameCaptureFailureReason::TimedOut: break;
    }
    return "";
}

} // namespace gte::Network
