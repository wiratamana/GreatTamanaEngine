#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Engine logging types and GTE_LOG_* macros, unconditional (no editor branch). The macros route through
// the sink in Core/LogSink.h; gte_core never includes the editor Logger.

namespace gte {

enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error };

// Level name for the Log panel and GET /get_logs; out-of-range values give "Unknown".
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

// Case-insensitive "debug"/"info"/"warning"/"error" -> LogLevel. Returns false and leaves *outLevel untouched otherwise.
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
    bool isBlocking = false;       // True for a failure an automated caller must treat as blocking (e.g. GET /get_blocking_errors).
};

// Query filter: an unset field matches everything; all set fields must match (AND).
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
    bool onlyBlocking = false;         // True = only entries with isBlocking == true match.
};

// Log buffer capacity; also the upper clamp for GET /get_logs' `limit`.
inline constexpr std::size_t kLogCapacity = 2000;

// Declared here so GTE_LOG_* call sites need not include LogSink.h (which defines ILogSink).
bool IsLogSinkInstalled() noexcept;
void LogToActiveSink(LogLevel level, std::string_view category, std::string_view message, bool isBlocking = false);

} // namespace gte

// GTE_LOG_* is the one way to log. Each macro checks IsLogSinkInstalled() first, so the category and
// message expressions are never evaluated when nothing is listening.
#define GTE_LOG_DEBUG(category, message) \
    (::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(::gte::LogLevel::Debug, (category), (message)) : (void)0)
#define GTE_LOG_INFO(category, message) \
    (::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(::gte::LogLevel::Info, (category), (message)) : (void)0)
#define GTE_LOG_WARNING(category, message) \
    (::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(::gte::LogLevel::Warning, (category), (message)) : (void)0)
#define GTE_LOG_ERROR(category, message) \
    (::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(::gte::LogLevel::Error, (category), (message)) : (void)0)
// Same as GTE_LOG_ERROR but flags the entry isBlocking=true for an automated caller to detect.
#define GTE_LOG_ERROR_BLOCKING(category, message) \
    (::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(::gte::LogLevel::Error, (category), (message), true) : (void)0)
