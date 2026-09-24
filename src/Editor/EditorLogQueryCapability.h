#pragma once

#include "../Core/EditorCapabilities.h"

namespace gte {

// editor-core-separation-2 campaign, PHASE3
// (PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md) - the REAL,
// gte_editor-owned implementation of ILogQueryCapability
// (Core/EditorCapabilities.h), delegating to src/Editor/Logger.h's real
// static Logger::Query()/Clear()/EntryCount()/IsEnabled()/LatestEntryId()
// methods. Constructed once, as a function-local `static` inside
// EditorHost.cpp (mirroring EditorSceneIOCapability's own exact wiring
// precedent), and passed straight into NetworkServer's constructor as its
// sixth argument.
//
// This HEADER deliberately carries ZERO dependency on Editor/Logger.h
// itself (only this class's own .cpp does) - mirrors
// EditorSceneIOCapability.h's own identical "the real body needing the
// complete type lives in a gte_editor-only .cpp, never in a header a
// gte_core-destined file might include" precedent exactly.
class EditorLogQueryCapability : public ILogQueryCapability {
public:
    std::vector<LogEntry> Query(const LogQueryFilter& filter) override;
    void Clear() override;
    std::size_t EntryCount() const override;
    bool IsEnabled() const override;
    std::uint64_t LatestEntryId() const override;
};

} // namespace gte
