#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace gte {

// Cross-thread request/result handoff: one request pending at a time.
// Submit blocks the caller; Fulfill wakes it. Never allocates.
// Every concrete bridge below is a plain, empty, public-inheriting
// subclass of this - never used polymorphically, so nothing here is
// virtual.
template <typename Request, typename Result>
class SyncCommandBridge {
public:
    // Named SubmitResult (not SubmitOutcome) on purpose - every real
    // call site across this codebase already spells out
    // `FooCommandBridge::SubmitResult` by name; keeping this name
    // means a derived class below needs zero caller-visible changes.
    struct SubmitResult {
        std::optional<Result> result;
        bool alreadyPending = false;
        bool timedOut = false;
    };

    // Caller thread: blocks until FulfillCommand() runs or timeout hits.
    SubmitResult SubmitAndWait(Request request, int timeoutMilliseconds = 3000)
    {
        std::unique_lock lock(m_mutex);
        if (m_requested) {
            return { std::nullopt, true, false };
        }
        m_request = std::move(request);
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

    // Owner thread: non-blocking poll, call once per frame.
    bool IsCommandPending() const
    {
        std::lock_guard lock(m_mutex);
        return m_requested && !m_fulfilled;
    }

    std::optional<Request> TryPeekPendingCommandRequest() const
    {
        std::lock_guard lock(m_mutex);
        if (!m_requested || m_fulfilled) {
            return std::nullopt;
        }
        return m_request;
    }

    void FulfillCommand(Result result)
    {
        {
            std::lock_guard lock(m_mutex);
            m_result = std::move(result);
            m_fulfilled = true;
        }
        m_conditionVariable.notify_one();
    }

    SyncCommandBridge() = default;
    SyncCommandBridge(const SyncCommandBridge&) = delete;
    SyncCommandBridge& operator=(const SyncCommandBridge&) = delete;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    Request m_request{};
    Result m_result{};
};

} // namespace gte
