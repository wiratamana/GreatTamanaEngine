#pragma once

#include "../Core/CapturedPixels.h"
#include "RenderTimingObserver.h"

#include <cstdint>

namespace gte {

enum class CaptureStatus : std::uint8_t {
    Ok,
    NothingToCapture, // The image is missing or its format cannot be read back.
    GpuBusy,          // The bounded GPU wait timed out.
    DeviceLost,
    Failed,
};

// Pixels are meaningful only when status is Ok.
struct CaptureResult {
    CapturedPixels pixels;
    CaptureStatus status = CaptureStatus::Failed;
};

constexpr CaptureStatus CaptureStatusFromBlockOutcome(BlockOutcome outcome) noexcept
{
    switch (outcome) {
    case BlockOutcome::Completed: return CaptureStatus::Ok;
    case BlockOutcome::TimedOut: return CaptureStatus::GpuBusy;
    case BlockOutcome::DeviceLost: return CaptureStatus::DeviceLost;
    case BlockOutcome::Failed: break;
    }
    return CaptureStatus::Failed;
}

} // namespace gte
