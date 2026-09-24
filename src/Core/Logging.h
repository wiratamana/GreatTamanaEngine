#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// PHASE3 (editor-core-separation-1 campaign,
// PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md; see also
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1) - this header is
// gte_core-owned (lives next to Core/Time.h/EngineContext.h) and carries
// EVERY plain logging type/macro that used to live only in the Editor-only
// src/Editor/Logger.h: LogLevel/LogEntry/LogQueryFilter/ToString()/
// TryParseLogLevel(), and the GTE_LOG_DEBUG/INFO/WARNING/ERROR macros
// themselves. This file, and everything in it, is UNCONDITIONAL - no
// #if GTE_ENABLE_EDITOR branch anywhere, ever (unlike the old Logger.h,
// which used to define two completely different macro bodies depending on
// that macro). See Core/LogSink.h for the ILogSink interface/
// InstallLogSink()/IsLogSinkInstalled()/LogToActiveSink() this file's own
// macros route through instead of calling ::gte::Logger::Log() directly -
// gte_core itself never includes anything under src/Editor/, including
// Logger.h, ever again after this phase.

namespace gte {

enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error };

// "Debug"/"Info"/"Warning"/"Error" - used by the Editor Log panel and by
// GET /get_logs' own JSON "level" field. An out-of-range value falls back
// to "Unknown", never reads out of bounds. Moved verbatim from
// Editor/Logger.h (PHASE3) - already unconditional/inline there, for the
// exact link-safety reason documented at the old location (Network/
// NetworkRoutes.h/.cpp calls this from an always-compiled translation
// unit) - now living in its permanent, gte_core-owned home instead.
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
// used by Network/NetworkRoutes.h's ParseGetLogsQuery(). Returns false
// (leaving *outLevel untouched) for anything else. Moved verbatim from
// Editor/Logger.h (PHASE3).
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

// editor-core-separation-2 campaign, PHASE3
// (PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md) - the real
// VALUE of Editor/Logger.h's own `Logger::kCapacity`, relocated here so
// Network/NetworkRoutes.cpp (a gte_core-tier file) can clamp GET /get_logs'
// own `limit` query parameter against it WITHOUT needing to #include the
// gte_editor-only Logger class just for one compile-time constant.
// `Logger::kCapacity` itself is unchanged in every other respect (same
// qualified name, same value, same call sites) - it simply becomes
// `static constexpr std::size_t kCapacity = kLogCapacity;` instead of
// re-stating the literal `2000` a second time (see Editor/Logger.h).
inline constexpr std::size_t kLogCapacity = 2000;

// Forward declarations only - real definitions live in Core/LogSink.h/.cpp
// (kept in a separate header/translation unit since they need the
// ILogSink interface, which this file deliberately does NOT define, to
// keep Logging.h a pure-data/macro header with no interface/vtable
// concept in it at all). Declared here (not just in LogSink.h) so the
// GTE_LOG_* macros below can reference them without LogSink.h needing to
// be included at every GTE_LOG_* call site in the engine - only whoever
// installs/queries the sink directly needs to include LogSink.h.
bool IsLogSinkInstalled() noexcept;
void LogToActiveSink(LogLevel level, std::string_view category, std::string_view message);

} // namespace gte

// GTE_LOG_* macros - the ONE sanctioned way any call site anywhere in the
// engine talks to logging (see AGENTS.md, "Logging", and
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1). Defined
// UNCONDITIONALLY - no #if GTE_ENABLE_EDITOR branch at all, ever, unlike
// the old Editor/Logger.h - mirroring Profiling/ScopeTimer.h's
// GTE_PROFILE_SCOPE dual-branch PRECEDENT in spirit ("a call site never
// needs its own #ifdef"), but NOT in mechanism: there is no compile-time
// OFF branch here at all anymore. Instead, each macro expands to a
// ternary that checks IsLogSinkInstalled() FIRST, deliberately preserving
// this engine's existing "category/message expressions are never even
// evaluated when nothing is listening" property (AGENTS.md, "Logging":
// "a full release runtime game build pays EXACTLY zero cost... including
// the cost of building the message string itself") for a Player host that
// never calls InstallLogSink() at all - see PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md's
// own flagged ambiguity and PHASE3_COMPLETION_REPORT.md for the full
// reasoning. This is NOT literally free (an atomic load + branch remains,
// unlike the old compile-time-erased `((void)0)`), but it is the closest
// achievable approximation now that "is logging compiled in at all" has
// become a RUNTIME question (whether a sink is installed) instead of a
// COMPILE-TIME one (GTE_ENABLE_EDITOR) - preserving the one property
// (never building the message string for nothing) that actually costs
// real work at a typical call site (e.g. NetworkServer.cpp's
// std::string-concatenation-heavy messages).
#define GTE_LOG_DEBUG(category, message) \
    (::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(::gte::LogLevel::Debug, (category), (message)) : (void)0)
#define GTE_LOG_INFO(category, message) \
    (::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(::gte::LogLevel::Info, (category), (message)) : (void)0)
#define GTE_LOG_WARNING(category, message) \
    (::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(::gte::LogLevel::Warning, (category), (message)) : (void)0)
#define GTE_LOG_ERROR(category, message) \
    (::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(::gte::LogLevel::Error, (category), (message)) : (void)0)
