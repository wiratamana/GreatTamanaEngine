#include "ProjectAssemblyHotReloadCommandBridge.h"

namespace gte {

ProjectAssemblyHotReloadCommandBridge::SubmitResult ProjectAssemblyHotReloadCommandBridge::SubmitAndWait(
    const std::string& projectName, int timeoutMilliseconds)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_requested) {
        // Another hot-reload request is already in flight - never wait,
        // return the "already pending" outcome immediately.
        SubmitResult result;
        result.alreadyPending = true;
        return result;
    }

    m_requested = true;
    m_fulfilled = false;
    m_projectName = projectName;

    const bool fulfilledInTime = m_conditionVariable.wait_for(
        lock,
        std::chrono::milliseconds(timeoutMilliseconds),
        [this] { return m_fulfilled; });

    SubmitResult result;
    if (fulfilledInTime) {
        // FulfillPending() already cleared m_requested, under this same
        // lock, before it ever called notify_one() - by the time
        // wait_for() returns true this thread has already re-acquired the
        // lock and observed that fresh state. Deliberately NOT cleared a
        // second time here - see this class's own header comment for why
        // there is exactly ONE place in this whole class that ever writes
        // m_requested = false.
        return result;
    }

    // Timed out - deliberately does NOT clear m_requested (see this
    // class's own header comment on FulfillPending() for the full
    // reasoning: the underlying cycle MUST keep running to completion on
    // the main thread regardless of whether this caller is still waiting).
    result.timedOut = true;
    return result;
}

std::optional<std::string> ProjectAssemblyHotReloadCommandBridge::TryPeekPendingProjectName() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_requested) {
        return std::nullopt;
    }
    return m_projectName;
}

void ProjectAssemblyHotReloadCommandBridge::FulfillPending()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_requested) {
            // Nobody is actually waiting (already timed out and nobody
            // resubmitted, or nobody ever asked) - inert no-op, never
            // crash/assert.
            return;
        }
        m_fulfilled = true;
        m_requested = false;
    }
    m_conditionVariable.notify_one();
}

} // namespace gte
