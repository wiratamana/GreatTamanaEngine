#include "EditorLogQueryCapability.h"

#include "Logger.h"

namespace gte {

std::vector<LogEntry> EditorLogQueryCapability::Query(const LogQueryFilter& filter)
{
    return Logger::Query(filter);
}

void EditorLogQueryCapability::Clear()
{
    Logger::Clear();
}

std::size_t EditorLogQueryCapability::EntryCount() const
{
    return Logger::EntryCount();
}

bool EditorLogQueryCapability::IsEnabled() const
{
    return Logger::IsEnabled();
}

std::uint64_t EditorLogQueryCapability::LatestEntryId() const
{
    return Logger::LatestEntryId();
}

} // namespace gte
