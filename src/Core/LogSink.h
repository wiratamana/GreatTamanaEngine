#pragma once

#include "Logging.h"

#include <string_view>

// Global, registrable log sink that GTE_LOG_* routes through. Core never implements a sink; the editor's
// Logger installs one at startup. A standalone host that installs none gets a silent no-op.

namespace gte {

// Implemented by a log consumer. Core only holds a nullable pointer to it, never a concrete editor type.
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Log(LogLevel level, std::string_view category, std::string_view message, bool isBlocking = false) = 0;
};

// Installs the process-global sink; the last call wins. nullptr uninstalls (logging becomes a no-op).
void InstallLogSink(ILogSink* sink) noexcept;

// True while a non-null sink is installed. GTE_LOG_* checks it to skip building the message when nobody listens.
bool IsLogSinkInstalled() noexcept;

// Routes one entry to the installed sink; a silent no-op when none is. Call through the GTE_LOG_* macros.
void LogToActiveSink(LogLevel level, std::string_view category, std::string_view message, bool isBlocking);

} // namespace gte
