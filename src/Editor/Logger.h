#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// GTE_LOG_* macros - the ONE sanctioned way any call site anywhere in the
// engine talks to the Logger (see AGENTS.md, "Logging", and
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #13). Defined
// unconditionally so a call site never needs its own #ifdef - see
// Profiling/ScopeTimer.h's GTE_PROFILE_SCOPE for the identical precedent.
//
// Compile to a true empty no-op (`((void)0)` - the `message`/`category`
// expressions are NEVER EVEN EVALUATED, not just discarded) when
// GTE_ENABLE_EDITOR is OFF - a full release runtime game build pays
// EXACTLY zero cost for every GTE_LOG_* call site in the entire engine,
// including the cost of building the message string itself.
#if GTE_ENABLE_EDITOR
#define GTE_LOG_DEBUG(category, message)   ::gte::Logger::Log(::gte::LogLevel::Debug,   category, message)
#define GTE_LOG_INFO(category, message)    ::gte::Logger::Log(::gte::LogLevel::Info,    category, message)
#define GTE_LOG_WARNING(category, message) ::gte::Logger::Log(::gte::LogLevel::Warning, category, message)
#define GTE_LOG_ERROR(category, message)   ::gte::Logger::Log(::gte::LogLevel::Error,   category, message)
#else
#define GTE_LOG_DEBUG(category, message)   ((void)0)
#define GTE_LOG_INFO(category, message)    ((void)0)
#define GTE_LOG_WARNING(category, message) ((void)0)
#define GTE_LOG_ERROR(category, message)   ((void)0)
#endif

namespace gte {

enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error };

// "Debug"/"Info"/"Warning"/"Error" - used by the Editor Log panel and by
// GET /get_logs' own JSON "level" field. An out-of-range value falls back
// to "Unknown", never reads out of bounds.
//
// Defined here, INLINE, UNCONDITIONALLY (not inside any #if
// GTE_ENABLE_EDITOR branch, and NOT merely declared-here-defined-out-of-
// line-in-Logger.cpp the way PHASE1 originally shipped it) - PHASE3 fix,
// flagged as a to-check item by PHASE1_COMPLETION_REPORT.md's own "worth
// flagging" note: Network/NetworkRoutes.cpp (an ALWAYS-compiled translation
// unit, never gated by GTE_ENABLE_EDITOR) calls both this function and
// TryParseLogLevel() below UNCONDITIONALLY at the language level. Logger.cpp
// (where these two used to be DEFINED) is only ever added to the build
// inside CMakeLists.txt's `if(GTE_ENABLE_EDITOR)` block, so a hypothetical
// GTE_ENABLE_EDITOR=OFF build would have failed to LINK
// NetworkRoutes.cpp.obj (an unresolved external symbol) even though neither
// function is ever actually reached at runtime in that configuration
// (Logger::Query() always returns an empty vector, so
// BuildGetLogsResponseJson() never actually calls ToString() in a real OFF
// build; ParseGetLogsQuery() only calls TryParseLogLevel() for a non-empty
// min_level value, but the SYMBOL reference still exists at compile time
// regardless). Making both fully `inline` here, always-defined regardless
// of GTE_ENABLE_EDITOR, sidesteps this link hazard entirely for both
// configurations, with no #ifdef needed at any call site - mirroring this
// same file's own LogEntry/LogQueryFilter (also always-defined, outside any
// #if) rather than Logger the class (genuinely dual-defined per branch,
// since ITS behavior actually differs by configuration - these two free
// functions' behavior does not).
inline const char* ToString(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Debug:
            return "Debug";
        case LogLevel::Info:
            return "Info";
        case LogLevel::Warning:
            return "Warning";
        case LogLevel::Error:
            return "Error";
    }
    return "Unknown";
}

// Case-insensitive parse of "debug"/"info"/"warning"/"error" -> LogLevel,
// used by Network/NetworkRoutes.h's ParseGetLogsQuery() (Phase 3). Returns
// false (leaving *outLevel untouched) for anything else. Inline/always-
// defined for the exact same link-safety reason as ToString() above.
inline bool TryParseLogLevel(const std::string& text, LogLevel* outLevel) noexcept
{
    if (outLevel == nullptr) {
        return false;
    }

    std::string lower;
    lower.reserve(text.size());
    for (char c : text) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (lower == "debug") {
        *outLevel = LogLevel::Debug;
        return true;
    }
    if (lower == "info") {
        *outLevel = LogLevel::Info;
        return true;
    }
    if (lower == "warning") {
        *outLevel = LogLevel::Warning;
        return true;
    }
    if (lower == "error") {
        *outLevel = LogLevel::Error;
        return true;
    }
    return false;
}

struct LogEntry {
    std::uint64_t id = 0;              // Monotonic, never reused, never reset by Clear().
    std::uint64_t frameNumber = 0;     // Logger::SetCurrentFrame()'s last value at record time.
    double timestampSeconds = 0.0;     // Seconds since THIS PROCESS's first Logger::Log() call.
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
};

// Query parameters for Logger::Query() - every field is a filter that is
// SKIPPED (matches everything) when left at its default/empty value. All
// filters that ARE set must ALL match for an entry to be included (logical
// AND), mirroring every other multi-field filter/parse struct already in
// this codebase (e.g. Network/NetworkRoutes.h's request-parsing structs).
struct LogQueryFilter {
    std::uint64_t sinceId = 0;         // Only entries with id > sinceId. 0 = from the very start.
    bool hasMinLevel = false;
    LogLevel minLevel = LogLevel::Debug; // Inclusive-and-above by ordinal value (Debug < Info < Warning < Error).
    std::string category;              // Empty = any. Exact, case-sensitive match otherwise.
    std::string keyword;                // Empty = any. Case-insensitive substring match on `message`.
    bool hasFrameMin = false;
    std::uint64_t frameMin = 0;        // Inclusive.
    bool hasFrameMax = false;
    std::uint64_t frameMax = 0;        // Inclusive.
    std::size_t limit = 0;             // 0 = no limit. Otherwise keep only the NEWEST `limit` matches.
};

#if GTE_ENABLE_EDITOR

// Thread-safe, process-global, Editor-only in-memory log store - see
// AGENTS.md ("Logging") for the full convention. Not an instance/DI type,
// same rationale as SdlMemoryTracker (Memory/SdlMemoryTracker.h) - a
// Logger call site can be anywhere in the engine, on any thread, with no
// natural single owner to thread a reference through. (NOTE:
// GpuMemoryTracker is sometimes loosely grouped with SdlMemoryTracker as a
// similar precedent, but it is actually the OPPOSITE shape - an instance
// class explicitly owned via std::shared_ptr, never static-global - see
// SdlMemoryTracker.h's own comment contrasting itself against
// GpuMemoryTracker directly. Logger follows SdlMemoryTracker's all-static
// shape only.)
//
// EVERY public method here is safe to call concurrently from ANY thread
// (main thread, a Jobs::JobSystem worker, the Network background thread) -
// this is a REQUIRED property, not an incidental one, since real call
// sites exist on all three (see AGENTS.md). Internally guarded by one
// mutex protecting BOTH the ring buffer AND the monotonic id counter's
// assignment (see Logger.cpp - `id` is assigned only while already
// holding that mutex, specifically so "ascending id order" and "push/
// append order" can never disagree under concurrent callers).
// SetCurrentFrame()'s value is a separate, relaxed atomic read/write, not
// mutex-guarded (see Logger.cpp for why that's still correct).
class Logger {
public:
    static constexpr std::size_t kCapacity = 2000;

    // Records one entry, stamping it with the CURRENT SetCurrentFrame()
    // value and a timestamp relative to this process's first ever
    // Logger::Log() call. If the ring buffer is already at kCapacity, the
    // single oldest entry is evicted first (FIFO). Never called directly
    // by feature code - see the GTE_LOG_* macros above. Deliberately NOT
    // noexcept (see this file's own "Important nuances" note in
    // PHASE1_CORE_LOGGER_MODULE.md): it locks a mutex and allocates
    // (std::string/LogEntry copies), either of which can theoretically
    // throw.
    static void Log(LogLevel level, const std::string& category, const std::string& message);

    // Called ONCE per real engine frame, from Application::Run() (Phase 2)
    // - stamps every LogEntry recorded AFTER this call with `frameNumber`
    // until the next call. Safe to call from the main thread only in
    // production, but the method itself has no such restriction baked in.
    static void SetCurrentFrame(std::uint64_t frameNumber) noexcept;

    // Returns every currently-retained entry matching `filter`, in
    // ASCENDING id order (oldest matching first) - EXCEPT that when
    // filter.limit > 0 and more than `limit` entries match, only the
    // NEWEST `limit` of them are kept (still returned in ascending id
    // order among themselves). This "keep the newest N, but return them
    // oldest-first" shape is deliberate: it bounds response size while
    // still reading top-to-bottom in a Console-style UI/log the same way
    // a human expects.
    static std::vector<LogEntry> Query(const LogQueryFilter& filter);

    // Empties the ring buffer. Does NOT reset the monotonic id counter -
    // the next Log() call after Clear() continues from wherever the
    // counter already was, so a caller's stale `since_id` cursor from
    // before the clear is never misread as "still current". Deliberately
    // NOT noexcept, same reasoning as Log() above (locks a mutex).
    static void Clear();

    // Number of entries currently retained (0..kCapacity). O(1).
    static std::size_t EntryCount() noexcept;

    // The highest id ever assigned so far (0 if Log() has never been
    // called). Useful for a caller establishing an initial since_id
    // cursor ("start watching from now onward").
    static std::uint64_t LatestEntryId() noexcept;

    // True in this branch, always - lets Network/NetworkRoutes.h (Phase 3)
    // and Editor/LogPanelData.h (Phase 4) distinguish "this build can
    // record logs, there are just none yet" from "this build can never
    // record anything" (see ProfilerPanelData.h's own free constexpr
    // `kCpuScopeInstrumentationCompiledIn` for the conceptual precedent this
    // mirrors - that one is a free variable rather than a class method, but
    // serves the exact same "is the real thing compiled in" purpose).
    static constexpr bool IsEnabled() noexcept { return true; }
};

#else // !GTE_ENABLE_EDITOR

// Compiled-out form: every method is fully inline and does nothing,
// exactly mirroring Profiling/ScopeTimer.h's own `#else` branch. No
// Logger.cpp is compiled into the build at all in this configuration
// (see CMakeLists.txt) - nothing here needs linking.
class Logger {
public:
    static constexpr std::size_t kCapacity = 2000;

    // NOT noexcept, even though this trivial body obviously cannot throw -
    // see this file's own "Important nuances" note below on why this
    // branch's signature must stay byte-for-byte identical (including
    // noexcept-ness) to the ON branch's real, genuinely-throwing-capable
    // Log().
    static void Log(LogLevel, const std::string&, const std::string&) { }
    static void SetCurrentFrame(std::uint64_t) noexcept { }
    static std::vector<LogEntry> Query(const LogQueryFilter&) { return {}; }
    // NOT noexcept - same reasoning as Log() above.
    static void Clear() { }
    static std::size_t EntryCount() noexcept { return 0; }
    static std::uint64_t LatestEntryId() noexcept { return 0; }
    static constexpr bool IsEnabled() noexcept { return false; }
};

#endif // GTE_ENABLE_EDITOR

} // namespace gte
