#include "RenderGraphControlCommandBridge.h"

#include <chrono>
#include <utility>

// editor-core-separation-8 campaign, PHASE5 - copied verbatim (locking/
// condvar logic unchanged) from FrameDebuggerCommandBridge.cpp, substituting
// only the type names - see this class's own header comment for why this
// exact shape is reused rather than "improved".

namespace gte {

RenderGraphControlCommandBridge::SubmitResult RenderGraphControlCommandBridge::SubmitAndWait(
    RenderGraphControlCommandRequest request, int timeoutMilliseconds)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_requested) {
        // Another command is already in flight - never wait, return the
        // "already pending" outcome immediately (HTTP 503).
        SubmitResult result;
        result.alreadyPending = true;
        return result;
    }

    m_requested = true;
    m_fulfilled = false;
    m_request = std::move(request);

    const bool fulfilledInTime = m_conditionVariable.wait_for(
        lock,
        std::chrono::milliseconds(timeoutMilliseconds),
        [this] { return m_fulfilled; });

    SubmitResult result;
    if (fulfilledInTime) {
        result.result = m_result;
        m_requested = false;
        return result;
    }

    // Timed out - reset back to idle so this bridge isn't stuck "pending"
    // forever; a late FulfillCommand() call arriving after this point must
    // be a safe, silent no-op (see below).
    m_requested = false;
    result.timedOut = true;
    return result;
}

bool RenderGraphControlCommandBridge::IsCommandPending() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_requested;
}

std::optional<RenderGraphControlCommandRequest> RenderGraphControlCommandBridge::TryPeekPendingCommandRequest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_requested) {
        return std::nullopt;
    }
    return m_request;
}

void RenderGraphControlCommandBridge::FulfillCommand(RenderGraphControlCommandResult result)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_requested) {
            // Nobody is actually waiting (already timed out, or nobody ever
            // asked) - inert no-op, never crash/assert.
            return;
        }
        m_result = std::move(result);
        m_fulfilled = true;
    }
    m_conditionVariable.notify_one();
}

} // namespace gte
