#include "ImGuiIdConflictTracker.h"

namespace gte {

void ImGuiIdConflictTracker::Reset()
{
    m_seenIds.clear();
}

bool ImGuiIdConflictTracker::RegisterAndCheckConflict(std::uint32_t id)
{
    const bool alreadySeen = (m_seenIds.find(id) != m_seenIds.end());
    m_seenIds.insert(id);
    return alreadySeen;
}

std::size_t ImGuiIdConflictTracker::DistinctIdCount() const noexcept
{
    return m_seenIds.size();
}

} // namespace gte
