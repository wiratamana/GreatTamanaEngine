#pragma once

#include "Core/LogSink.h"
#include "Editor/Logger.h"

#include <string>
#include <string_view>
#include <vector>

namespace gte::test {

// Records every log call verbatim.
class RecordingLogSink : public ILogSink {
public:
    struct Recorded {
        LogLevel level;
        std::string category;
        std::string message;
    };

    void Log(LogLevel level, std::string_view category, std::string_view message, bool isBlocking = false) override
    {
        entries.push_back(Recorded{ level, std::string(category), std::string(message) });
        (void)isBlocking;
    }

    std::vector<Recorded> entries;
};

// Installs a sink for the scope and restores the real one on exit, so later tests never see a stale fake.
class ScopedLogSinkInstall {
public:
    explicit ScopedLogSinkInstall(ILogSink* sink) { InstallLogSink(sink); }
    ~ScopedLogSinkInstall() { InstallLogSink(&LoggerLogSink::Instance()); }

    ScopedLogSinkInstall(const ScopedLogSinkInstall&) = delete;
    ScopedLogSinkInstall& operator=(const ScopedLogSinkInstall&) = delete;
};

inline int CountByCategory(const RecordingLogSink& sink, std::string_view category)
{
    int count = 0;
    for (const RecordingLogSink::Recorded& entry : sink.entries) {
        count += entry.category == category ? 1 : 0;
    }
    return count;
}

inline int CountErrorsByCategory(const RecordingLogSink& sink, std::string_view category)
{
    int count = 0;
    for (const RecordingLogSink::Recorded& entry : sink.entries) {
        count += (entry.level == LogLevel::Error && entry.category == category) ? 1 : 0;
    }
    return count;
}

} // namespace gte::test
