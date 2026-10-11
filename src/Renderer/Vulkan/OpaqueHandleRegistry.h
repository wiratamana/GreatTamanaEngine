#pragma once

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace gte {

// Thread-safe slot map behind every opaque 64-bit handle this engine hands
// out to callers outside src/Renderer (DescriptorSetHandle, SamplerHandle,
// ImageViewHandle, DescriptorSetLayoutHandle, UiTextureHandle - see
// Core/GraphicsHandles.h, Core/UiTextureHandle.h). The 64-bit handle value
// packs a generational slot index: low 32 bits = index, high 32 bits =
// generation - minted only by Insert() below, never constructed from a raw
// integer by calling code. Renderer-internal only.
template <typename Native>
class OpaqueHandleRegistry {
public:
    std::uint64_t Insert(Native value)
    {
        std::lock_guard lock(m_mutex);
        std::uint32_t index;
        if (!m_freeList.empty()) {
            index = m_freeList.back();
            m_freeList.pop_back();
            Slot& slot = m_slots[index];
            slot.generation += 1;
            if (slot.generation == 0) {
                slot.generation = 1; // Skip the reserved 0 sentinel on wraparound.
            }
            slot.value = value;
            slot.occupied = true;
        } else {
            index = static_cast<std::uint32_t>(m_slots.size());
            m_slots.push_back(Slot{ value, 1, true });
        }
        return Pack(index, m_slots[index].generation);
    }

    // Safe to call with an already-removed or stale handle (no-op). Returns
    // true if a slot was actually freed.
    bool Remove(std::uint64_t handle)
    {
        std::lock_guard lock(m_mutex);
        const auto [index, generation] = Unpack(handle);
        if (!IsValidLocked(index, generation)) {
            return false;
        }
        m_slots[index].occupied = false;
        m_slots[index].value = Native{};
        m_freeList.push_back(index);
        return true;
    }

    // Returns a default-constructed Native{} for an invalid/stale handle -
    // never throws, never asserts (mirrors ResourcePool::TryGet()'s own
    // "safe to query a dead handle" guarantee).
    Native Get(std::uint64_t handle) const
    {
        std::lock_guard lock(m_mutex);
        const auto [index, generation] = Unpack(handle);
        return IsValidLocked(index, generation) ? m_slots[index].value : Native{};
    }

private:
    struct Slot {
        Native value{};
        std::uint32_t generation = 0;
        bool occupied = false;
    };

    static std::uint64_t Pack(std::uint32_t index, std::uint32_t generation) noexcept
    {
        return (static_cast<std::uint64_t>(generation) << 32) | static_cast<std::uint64_t>(index);
    }
    static std::pair<std::uint32_t, std::uint32_t> Unpack(std::uint64_t handle) noexcept
    {
        return { static_cast<std::uint32_t>(handle), static_cast<std::uint32_t>(handle >> 32) };
    }
    bool IsValidLocked(std::uint32_t index, std::uint32_t generation) const noexcept
    {
        return generation != 0 && index < m_slots.size() && m_slots[index].occupied &&
            m_slots[index].generation == generation;
    }

    mutable std::mutex m_mutex;
    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeList;
};

} // namespace gte
