#include "TerrainSlotAllocator.h"

namespace gte::terrain::gpu {

SlotAllocator::SlotAllocator(std::uint32_t capacity)
    : m_state(capacity, SlotState::Free)
    , m_generation(capacity, 0)
{
    m_free.reserve(capacity);
    for (std::uint32_t i = capacity; i > 0; --i) {
        m_free.push_back(i - 1);
    }
}

void SlotAllocator::Collect(const SlotFrame& frame)
{
    while (!m_retiring.empty() && SlotReusable(frame, m_retiring.front().serial)) {
        const std::uint32_t slot = m_retiring.front().slot;
        m_retiring.pop_front();
        m_state[slot] = SlotState::Free;
        m_free.push_back(slot);
    }
}

std::optional<std::uint32_t> SlotAllocator::Acquire(const SlotFrame& frame)
{
    Collect(frame);
    if (m_free.empty()) {
        return std::nullopt;
    }
    // Keep the lowest index on top after returning slots out of order.
    std::size_t best = m_free.size() - 1;
    for (std::size_t i = 0; i + 1 < m_free.size(); ++i) {
        if (m_free[i] < m_free[best]) {
            best = i;
        }
    }
    const std::uint32_t slot = m_free[best];
    m_free[best] = m_free.back();
    m_free.pop_back();
    m_state[slot] = SlotState::Reserved;
    ++m_generation[slot];
    ++m_used;
    return slot;
}

bool SlotAllocator::MarkWritten(std::uint32_t slot) noexcept
{
    if (slot >= m_state.size() || m_state[slot] != SlotState::Reserved) {
        return false;
    }
    m_state[slot] = SlotState::Written;
    return true;
}

bool SlotAllocator::Release(std::uint32_t slot, const SlotFrame& frame)
{
    if (slot >= m_state.size() || (m_state[slot] != SlotState::Reserved && m_state[slot] != SlotState::Written)) {
        return false;
    }
    --m_used;
    if (!frame.IsAsync()) {
        m_state[slot] = SlotState::Free;
        m_free.push_back(slot);
        return true;
    }
    m_state[slot] = SlotState::Retiring;
    m_retiring.push_back({ slot, frame.serial });
    return true;
}

bool SlotAllocator::Abandon(std::uint32_t slot) noexcept
{
    if (slot >= m_state.size() || m_state[slot] != SlotState::Reserved) {
        return false;
    }
    --m_used;
    m_state[slot] = SlotState::Free;
    m_free.push_back(slot);
    return true;
}

} // namespace gte::terrain::gpu
