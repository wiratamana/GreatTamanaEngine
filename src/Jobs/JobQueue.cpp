#include "JobQueue.h"

#include <utility>

namespace gte::Jobs::detail {

JobQueue::JobQueue(std::size_t capacity)
    : m_slots(capacity > 0 ? capacity : 1)
{
}

bool JobQueue::TryPush(JobEntry entry)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_count == m_slots.size()) {
        return false; // Full - caller (JobSystem::Schedule()) handles the fallback.
    }

    m_slots[m_tail] = std::move(entry);
    m_tail = (m_tail + 1) % m_slots.size();
    ++m_count;

    m_condition.notify_one();
    return true;
}

bool JobQueue::WaitAndPop(JobEntry& outEntry)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condition.wait(lock, [this]() { return m_count > 0 || m_shuttingDown; });

    if (m_count == 0) {
        // Only reachable via a shutdown request with nothing left queued -
        // the worker's own signal to exit its loop.
        return false;
    }

    outEntry = std::move(m_slots[m_head]);
    m_slots[m_head] = JobEntry{}; // Release the moved-from entry's own shared_ptr reference promptly.
    m_head = (m_head + 1) % m_slots.size();
    --m_count;
    return true;
}

void JobQueue::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shuttingDown = true;
    }
    m_condition.notify_all();
}

} // namespace gte::Jobs::detail
