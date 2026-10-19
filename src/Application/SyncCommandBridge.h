#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace gte {

// Cross-thread request/result handoff: one request pending at a time.
// Every request carries an id; a result for a timed-out request is dropped.
// Never allocates. Not used polymorphically, so nothing here is virtual.
template <typename Request, typename Result>
class SyncCommandBridge {
public:
    struct SubmitResult {
        std::optional<Result> result;
        bool alreadyPending = false;
        bool timedOut = false;
    };

    // A request handed to the owner thread. Id 0 is never issued.
    struct PendingCommand {
        std::uint64_t id = 0;
        Request request{};
    };

    // Caller thread: blocks until FulfillCommand() delivers this request's result or the timeout hits.
    SubmitResult SubmitAndWait(Request request, int timeoutMilliseconds = 3000)
    {
        std::unique_lock lock(m_mutex);
        if (m_requested) {
            return { std::nullopt, true, false };
        }
        m_request = std::move(request);
        m_requestId = ++m_nextId;
        m_requested = true;
        m_fulfilled = false;
        const bool signaled = m_conditionVariable.wait_for(
            lock, std::chrono::milliseconds(timeoutMilliseconds), [this] { return m_fulfilled; });
        m_requested = false;
        if (!signaled) {
            return { std::nullopt, false, true };
        }
        return { std::move(m_result), false, false };
    }

    // Owner thread: true while a request waits for its result.
    bool IsCommandPending() const
    {
        std::lock_guard lock(m_mutex);
        return m_requested && !m_fulfilled;
    }

    // Owner thread: non-blocking poll, call once per frame.
    std::optional<PendingCommand> TryPeekPendingCommand() const
    {
        std::lock_guard lock(m_mutex);
        if (!m_requested || m_fulfilled) {
            return std::nullopt;
        }
        return PendingCommand { m_requestId, m_request };
    }

    // Owner thread: returns false when the id is stale (timed out or superseded) and the result is dropped.
    bool FulfillCommand(std::uint64_t id, Result result)
    {
        {
            std::lock_guard lock(m_mutex);
            if (!m_requested || m_fulfilled || id != m_requestId) {
                return false;
            }
            m_result = std::move(result);
            m_fulfilled = true;
        }
        m_conditionVariable.notify_one();
        return true;
    }

    SyncCommandBridge() = default;
    SyncCommandBridge(const SyncCommandBridge&) = delete;
    SyncCommandBridge& operator=(const SyncCommandBridge&) = delete;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    std::uint64_t m_requestId = 0;
    std::uint64_t m_nextId = 0;
    Request m_request{};
    Result m_result{};
};

} // namespace gte
