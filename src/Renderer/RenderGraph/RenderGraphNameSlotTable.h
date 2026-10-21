#pragma once

// Pure, Vulkan-free name -> GPU-timing-slot table. RenderGraph owns one per timing regime.
// An exhausted budget degrades gracefully: no crash, no aliasing of another pass's slot.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gte::rg {

// No slot could be assigned: the budget is fully used by other names.
inline constexpr std::int32_t kNoNameSlot = -1;

// A persistent name -> slot table with a fixed budget. It keeps its own copy of every name: a name may
// come from a module that is unloaded later, so the caller's pointer is never trusted after the call.
class RenderGraphNameSlotTable {
public:
    explicit RenderGraphNameSlotTable(std::uint32_t slotBudget) noexcept
        : m_slotBudget(slotBudget)
    {
    }

    // The existing slot of `name`, a new one while budget remains, or kNoNameSlot (nullptr name, or a new
    // name with the budget exhausted). A slot never changes or moves to another name.
    std::int32_t AssignOrGetSlot(const char* name) noexcept
    {
        if (name == nullptr) {
            m_lastCallOverflowed = false;
            return kNoNameSlot;
        }
        for (std::size_t i = 0; i < m_names.size(); ++i) {
            if (m_names[i] == name) {
                m_lastCallOverflowed = false;
                return static_cast<std::int32_t>(i);
            }
        }
        if (m_names.size() >= static_cast<std::size_t>(m_slotBudget)) {
            m_lastCallOverflowed = true;
            m_everOverflowed = true;
            return kNoNameSlot;
        }
        m_lastCallOverflowed = false;
        m_names.emplace_back(name);
        return static_cast<std::int32_t>(m_names.size() - 1);
    }

    // True only if the most recent AssignOrGetSlot() returned kNoNameSlot because a new name found the
    // budget exhausted (not for nullptr or an already-assigned name).
    bool JustOverflowed() const noexcept { return m_lastCallOverflowed; }

    // True once any new name was refused because the budget was full. Never resets.
    bool EverOverflowed() const noexcept { return m_everOverflowed; }

    // The name assigned to `slot`, or nullptr when out of range or unassigned. The pointer is owned by
    // this table and valid for its lifetime.
    const char* NameAtSlot(std::int32_t slot) const noexcept
    {
        if (slot < 0 || static_cast<std::size_t>(slot) >= m_names.size()) {
            return nullptr;
        }
        return m_names[static_cast<std::size_t>(slot)].c_str();
    }

    std::uint32_t SlotBudget() const noexcept { return m_slotBudget; }
    std::uint32_t AssignedCount() const noexcept { return static_cast<std::uint32_t>(m_names.size()); }

private:
    std::uint32_t m_slotBudget;
    std::vector<std::string> m_names; // index == assigned slot; owned copies
    bool m_lastCallOverflowed = false;
    bool m_everOverflowed = false;
};

} // namespace gte::rg
