#include "Logger.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <deque>
#include <mutex>

namespace gte {

namespace {

std::mutex s_mutex;
std::deque<LogEntry> s_entries;
std::atomic<std::uint64_t> s_nextId{ 1 };
std::atomic<std::uint64_t> s_currentFrame{ 0 };

// Lowercases a single char safely - std::tolower() is undefined behavior for
// a plain (possibly negative) `char` outside the basic 7-bit character set,
// so every byte must be cast to unsigned char first.
char ToLowerChar(char c) noexcept
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string ToLowerCopy(const std::string& text)
{
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(), ToLowerChar);
    return result;
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    return ToLowerCopy(haystack).find(ToLowerCopy(needle)) != std::string::npos;
}

// Seconds elapsed since this process's very first Logger::Log() call.
// Deliberately independent of gte::Time/EngineContext - see
// PHASE0_MASTER_STRATEGY.md's Step 2 for why a Logger call site needs its
// own wall-clock reference that exists even before the first rendered
// frame.
double SecondsSinceFirstLog() noexcept
{
    static const std::chrono::steady_clock::time_point s_startTime = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - s_startTime).count();
}

} // namespace

// ToString(LogLevel)/TryParseLogLevel() used to be DECLARED in this file's
// own Logger.h but DEFINED here - moved to fully `inline`, always-defined
// definitions in Core/Logging.h instead (editor-core-separation-1
// campaign's own PHASE3, PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md),
// which itself continued the "make it inline, unconditional, no link
// hazard" precedent the logger-1 campaign's own PHASE3
// (PHASE3_NETWORK_ENDPOINTS_GET_LOGS_AND_CLEAR_LOGS.md) originally
// established when these two functions first moved out of Logger.cpp and
// into (what was then) Logger.h. Do NOT re-add definitions of either
// function here - or in this file's own Logger.h - ever again.

void Logger::Log(LogLevel level, const std::string& category, const std::string& message)
{
    const std::uint64_t frameNumber = s_currentFrame.load(std::memory_order_relaxed);
    const double timestampSeconds = SecondsSinceFirstLog();

    std::lock_guard<std::mutex> lock(s_mutex);

    // `id` is assigned only while s_mutex is already held, so "ascending id
    // order" and "push/append order" can never disagree under concurrent
    // callers - see this file's own header comment / PHASE1's plan for why
    // assigning it before acquiring the lock would be a subtle race.
    const std::uint64_t id = s_nextId++;

    LogEntry entry;
    entry.id = id;
    entry.frameNumber = frameNumber;
    entry.timestampSeconds = timestampSeconds;
    entry.level = level;
    entry.category = category;
    entry.message = message;

    s_entries.push_back(std::move(entry));
    if (s_entries.size() > Logger::kCapacity) {
        s_entries.pop_front();
    }
}

void Logger::SetCurrentFrame(std::uint64_t frameNumber) noexcept
{
    s_currentFrame.store(frameNumber, std::memory_order_relaxed);
}

std::vector<LogEntry> Logger::Query(const LogQueryFilter& filter)
{
    std::vector<LogEntry> result;

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        result.reserve(s_entries.size());
        for (const LogEntry& entry : s_entries) {
            if (entry.id <= filter.sinceId) {
                continue;
            }
            if (filter.hasMinLevel
                && static_cast<int>(entry.level) < static_cast<int>(filter.minLevel)) {
                continue;
            }
            if (!filter.category.empty() && entry.category != filter.category) {
                continue;
            }
            if (!filter.keyword.empty() && !ContainsCaseInsensitive(entry.message, filter.keyword)) {
                continue;
            }
            if (filter.hasFrameMin && entry.frameNumber < filter.frameMin) {
                continue;
            }
            if (filter.hasFrameMax && entry.frameNumber > filter.frameMax) {
                continue;
            }
            result.push_back(entry);
        }
    }

    if (filter.limit > 0 && result.size() > filter.limit) {
        result.erase(result.begin(), result.begin() + static_cast<std::ptrdiff_t>(result.size() - filter.limit));
    }

    return result;
}

void Logger::Clear()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_entries.clear();
    // Deliberately does NOT touch s_nextId - see Logger.h's own comment.
}

std::size_t Logger::EntryCount() noexcept
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_entries.size();
}

std::uint64_t Logger::LatestEntryId() noexcept
{
    const std::uint64_t nextId = s_nextId.load(std::memory_order_relaxed);
    return nextId > 1 ? nextId - 1 : 0;
}

} // namespace gte
