#include "NameTable.h"

#include <cstring>

namespace gte::Profiling {
namespace {

constexpr std::string_view kOverflowText = "<overflow>";

std::uint32_t HashText(std::string_view text) noexcept
{
    std::uint32_t h = 2166136261u; // FNV-1a
    for (const char c : text) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

} // namespace

NameTable::NameTable() noexcept
{
    // Id 1 is the overflow marker.
    std::memcpy(m_arena.data(), kOverflowText.data(), kOverflowText.size());
    m_arena[kOverflowText.size()] = '\0';
    m_entries[kOverflowNameId] = { 0, static_cast<std::uint32_t>(kOverflowText.size()) };
    m_arenaUsed = static_cast<std::uint32_t>(kOverflowText.size() + 1);
    m_count.store(kOverflowNameId, std::memory_order_release);
}

NameId NameTable::Intern(const char* text) noexcept
{
    return text ? Intern(std::string_view(text)) : kInvalidNameId;
}

NameId NameTable::Intern(std::string_view text) noexcept
{
    if (text.empty()) {
        return kInvalidNameId;
    }

    const std::uint32_t hash = HashText(text);
    std::lock_guard lock(m_mutex);

    std::size_t index = hash & (kNameSlots - 1);
    // The table is never more than 75% full, so an empty slot always ends the probe.
    while (m_slots[index].id != 0) {
        const Slot& slot = m_slots[index];
        const Entry& entry = m_entries[slot.id];
        if (slot.hash == hash && entry.length == text.size()
            && std::memcmp(m_arena.data() + entry.offset, text.data(), text.size()) == 0) {
            return slot.id;
        }
        index = (index + 1) & (kNameSlots - 1);
    }

    const std::uint32_t id = m_count.load(std::memory_order_relaxed) + 1;
    const std::size_t needed = text.size() + 1;
    if (text.size() > kMaxNameLength || id > kMaxNames || m_arenaUsed + needed > kNameArenaBytes) {
        m_overflow.fetch_add(1, std::memory_order_relaxed);
        return kOverflowNameId;
    }

    std::memcpy(m_arena.data() + m_arenaUsed, text.data(), text.size());
    m_arena[m_arenaUsed + text.size()] = '\0';
    m_entries[id] = { m_arenaUsed, static_cast<std::uint32_t>(text.size()) };
    m_arenaUsed += static_cast<std::uint32_t>(needed);
    m_slots[index] = { id, hash };
    m_count.store(id, std::memory_order_release); // publishes the entry to lock-free readers
    return id;
}

const char* NameTable::Resolve(NameId id) const noexcept
{
    if (id == kInvalidNameId || id > m_count.load(std::memory_order_acquire)) {
        return "";
    }
    return m_arena.data() + m_entries[id].offset;
}

NameTable& GlobalNameTable() noexcept
{
    static NameTable table;
    return table;
}

NameId InternName(const char* text) noexcept
{
    return GlobalNameTable().Intern(text);
}

const char* ResolveName(NameId id) noexcept
{
    return GlobalNameTable().Resolve(id);
}

} // namespace gte::Profiling
