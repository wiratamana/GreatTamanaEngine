#include "GpuWaitPolicy.h"

namespace gte {

BlockOutcome GpuWaitPolicy::Judge(WaitClass waitClass, BlockOutcome raw) noexcept
{
    if (Lost()) {
        return BlockOutcome::DeviceLost;
    }

    switch (raw) {
    case BlockOutcome::Completed:
        if (waitClass == WaitClass::Frame) {
            m_frameTimeouts.store(0, std::memory_order_relaxed);
        }
        return BlockOutcome::Completed;

    case BlockOutcome::DeviceLost:
        MarkLost();
        return BlockOutcome::DeviceLost;

    case BlockOutcome::TimedOut:
        if (waitClass == WaitClass::Upload) {
            MarkLost();
            return BlockOutcome::TimedOut;
        }
        if (m_frameTimeouts.fetch_add(1, std::memory_order_relaxed) + 1 > kToleratedFrameTimeouts) {
            MarkLost();
            return BlockOutcome::DeviceLost;
        }
        return BlockOutcome::TimedOut;

    case BlockOutcome::Failed:
        if (waitClass == WaitClass::Upload) {
            MarkLost();
        }
        return BlockOutcome::Failed;
    }
    return BlockOutcome::Failed;
}

} // namespace gte
