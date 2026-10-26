#include "AbandonedWork.h"

#include <utility>

namespace gte {

// Out-of-memory in Park or Release terminates: the process is already dying.
void AbandonedWork::Park(std::shared_ptr<void> owner) noexcept
{
    if (!owner) {
        return;
    }
    const std::lock_guard lock(m_mutex);
    m_items.push_back(std::move(owner));
    m_count.fetch_add(1, std::memory_order_relaxed);
}

void AbandonedWork::Release(bool idleCompleted) noexcept
{
    std::vector<std::shared_ptr<void>> items;
    {
        const std::lock_guard lock(m_mutex);
        items = std::move(m_items);
        m_items.clear();
        m_count.store(0, std::memory_order_relaxed);
    }
    if (idleCompleted) {
        return; // `items` dies here and frees every owner.
    }
    // Device lost: leak the whole vector on purpose.
    static_cast<void>(std::make_unique<std::vector<std::shared_ptr<void>>>(std::move(items)).release());
}

} // namespace gte
