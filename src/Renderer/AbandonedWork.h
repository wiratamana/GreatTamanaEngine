#pragma once

// Vulkan-free owner of objects the GPU may still touch after a failed wait.
// A failed wait loses the device and later waits fail at once, so only a few items are ever parked.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace gte {

// Thread-safe.
class AbandonedWork {
public:
    // Keeps `owner` alive. Never throws; an owner that cannot be stored is leaked.
    void Park(std::shared_ptr<void> owner) noexcept;

    std::uint32_t Count() const noexcept { return m_count.load(std::memory_order_relaxed); }

    // Destroys every parked owner when the GPU is idle. Otherwise leaks them on purpose.
    void Release(bool idleCompleted) noexcept;

private:
    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<void>> m_items;
    std::atomic<std::uint32_t> m_count{ 0 };
};

} // namespace gte
