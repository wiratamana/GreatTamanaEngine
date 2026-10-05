#include "LogSink.h"

#include <atomic>

namespace gte {

namespace {

// Relaxed/acquire-release atomic - matches Logger.cpp's own existing
// concurrency approach (a plain atomic, no mutex needed for a single
// pointer swap/read) - see PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md.
std::atomic<ILogSink*> s_activeSink{ nullptr };

} // namespace

void InstallLogSink(ILogSink* sink) noexcept
{
    s_activeSink.store(sink, std::memory_order_release);
}

bool IsLogSinkInstalled() noexcept
{
    return s_activeSink.load(std::memory_order_acquire) != nullptr;
}

void LogToActiveSink(LogLevel level, std::string_view category, std::string_view message, bool isBlocking)
{
    ILogSink* sink = s_activeSink.load(std::memory_order_acquire);
    if (sink != nullptr) {
        sink->Log(level, category, message, isBlocking);
    }
}

} // namespace gte
