#pragma once

#include "../Application/FrameCaptureBridge.h"
#include "../Renderer/CaptureResult.h"

namespace gte {

// Maps a failed host capture to the reason the network route reports. Busy and lost GPUs keep their real cause.
constexpr FrameCaptureFailureReason FailureReasonForCapture(CaptureStatus status) noexcept
{
    switch (status) {
    case CaptureStatus::GpuBusy: return FrameCaptureFailureReason::GpuBusy;
    case CaptureStatus::DeviceLost: return FrameCaptureFailureReason::DeviceLost;
    case CaptureStatus::Ok:
    case CaptureStatus::NothingToCapture:
    case CaptureStatus::Failed: break;
    }
    return FrameCaptureFailureReason::TargetNotAvailable;
}

} // namespace gte
