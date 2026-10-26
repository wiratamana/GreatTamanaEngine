#pragma once

#include "FrameProfiler.h"
#include "NameTable.h"
#include "ScopeEvent.h"

#include <cstdint>

#define GTE_PROFILE_CONCAT_INNER(a, b) a##b
#define GTE_PROFILE_CONCAT(a, b) GTE_PROFILE_CONCAT_INNER(a, b)

// Interns a literal once per call site and returns its id.
#define GTE_PROFILE_NAME_ID(name) ([]() noexcept { \
    static const ::gte::Profiling::NameId id = ::gte::Profiling::InternName(name); \
    return id; \
}())

#if GTE_ENABLE_PROFILER

// Times the rest of the enclosing block as one CPU scope. `name` must be a string literal.
#define GTE_PROFILE_SCOPE(name) \
    ::gte::Profiling::ScopeTimer GTE_PROFILE_CONCAT(gteProfileScope_, __LINE__)(GTE_PROFILE_NAME_ID(name))

// Same, for a name interned earlier with InternName().
#define GTE_PROFILE_SCOPE_ID(nameId) \
    ::gte::Profiling::ScopeTimer GTE_PROFILE_CONCAT(gteProfileScope_, __LINE__)(nameId)

// Times the rest of the block as time spent waiting, not working.
#define GTE_PROFILE_BLOCKED(name) \
    ::gte::Profiling::ScopeTimer GTE_PROFILE_CONCAT(gteProfileScope_, __LINE__)( \
        GTE_PROFILE_NAME_ID(name), ::gte::Profiling::ScopeKind::Blocked)

// Records an instant event.
#define GTE_PROFILE_MARKER(name) \
    ::gte::Profiling::FrameProfiler::Instance().RecordMarker(GTE_PROFILE_NAME_ID(name))

#else

#define GTE_PROFILE_SCOPE(name) static_assert(true)
#define GTE_PROFILE_SCOPE_ID(nameId) static_assert(true)
#define GTE_PROFILE_BLOCKED(name) static_assert(true)
#define GTE_PROFILE_MARKER(name) static_cast<void>(0)

#endif // GTE_ENABLE_PROFILER

namespace gte::Profiling {

#if GTE_ENABLE_PROFILER

// RAII CPU scope. Does not read the clock outside a captured frame.
class ScopeTimer {
public:
    explicit ScopeTimer(NameId name, ScopeKind kind = ScopeKind::Work, ViewSlot view = ViewSlot::Shared) noexcept
        : m_name(name)
        , m_kind(kind)
        , m_view(view)
        , m_start(FrameProfiler::Instance().BeginScope())
    {
    }

    ~ScopeTimer() { FrameProfiler::Instance().EndScope(m_start, m_name, m_kind, m_view); }

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    NameId m_name;
    ScopeKind m_kind;
    ViewSlot m_view;
    ScopeStart m_start;
};

#else

// Compiled-out form, so code that names the type still builds.
class ScopeTimer {
public:
    explicit ScopeTimer(NameId, ScopeKind = ScopeKind::Work, ViewSlot = ViewSlot::Shared) noexcept { }

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
};

#endif // GTE_ENABLE_PROFILER

} // namespace gte::Profiling
