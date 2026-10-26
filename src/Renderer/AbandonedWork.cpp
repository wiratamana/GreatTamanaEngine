#include "AbandonedWork.h"

#include <utility>

namespace gte {

void AbandonedWork::Park(std::shared_ptr<void> owner) noexcept
{
    if (!owner) {
        return;
    }
    try {
        const std::lock_guard lock(m_mutex);
        m_items.push_back(owner);
        m_count.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        // Out of memory: pin the owner forever with a reference that is never dropped.
        static_cast<void>(std::make_unique<std::shared_ptr<void>>(std::move(owner)).release());
    }
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
    try {
        // Device lost: parked objects are abandoned.
        static_cast<void>(std::make_unique<std::vector<std::shared_ptr<void>>>(std::move(items)).release());
    } catch (...) {
        // Without the heap box the vector below would free them; pin each owner instead.
        for (std::shared_ptr<void>& item : items) {
            try {
                static_cast<void>(std::make_unique<std::shared_ptr<void>>(item).release());
            } catch (...) {
                // Nothing left to try.
            }
        }
    }
}

} // namespace gte
