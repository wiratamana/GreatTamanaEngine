#pragma once

// Holds objects the CPU has dropped but a frame in flight may still use, until that frame is done.
// Holds no GPU calls itself: an entry is only an owner whose destructor frees the object.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace gte {

class RetireQueue {
public:
    // Keeps `object` alive until the frame with serial `tag` is complete. The object is moved in.
    template <class T>
    void Push(std::uint64_t tag, T&& object)
    {
        m_entries.push_back(Entry{ tag, std::make_shared<std::decay_t<T>>(std::forward<T>(object)) });
    }

    // Frees every entry whose frame is complete. Call only after a wait that really Completed.
    // Entries are moved out first, so a destructor that retires something else cannot invalidate the walk.
    std::size_t Collect(std::uint64_t completedSerial)
    {
        std::vector<Entry> ready;
        std::vector<Entry> kept;
        kept.reserve(m_entries.size());
        for (Entry& entry : m_entries) {
            (entry.tag <= completedSerial ? ready : kept).push_back(std::move(entry));
        }
        m_entries = std::move(kept);
        const std::size_t freed = ready.size();
        ready.clear();
        m_freed += freed;
        return freed;
    }

    // Frees everything. Call only when the GPU is known to be idle.
    std::size_t ReleaseAll()
    {
        std::vector<Entry> all = std::exchange(m_entries, {});
        const std::size_t freed = all.size();
        all.clear();
        m_freed += freed;
        return freed;
    }

    // Hands every entry to the caller, which must keep them alive (the GPU may still use them).
    std::vector<std::shared_ptr<void>> TakeAll()
    {
        std::vector<std::shared_ptr<void>> owners;
        owners.reserve(m_entries.size());
        for (Entry& entry : m_entries) {
            owners.push_back(std::move(entry.owner));
        }
        m_entries.clear();
        return owners;
    }

    std::size_t Pending() const noexcept { return m_entries.size(); }
    std::uint64_t TotalFreed() const noexcept { return m_freed; }

private:
    struct Entry {
        std::uint64_t tag = 0;
        std::shared_ptr<void> owner;
    };

    std::vector<Entry> m_entries;
    std::uint64_t m_freed = 0;
};

} // namespace gte
