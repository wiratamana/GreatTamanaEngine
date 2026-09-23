#pragma once

#include <string_view>

#include "Logging.h"

namespace gte {

// Core's own diagnostics-reporting hook OUT (design doc, Section 5.2:
// "Committed part of Core's public contract - not deferred. Gives Core a way
// to report diagnostics without ever depending on anything under
// src/Editor/ or src/Network/") - injected into Core's constructor alongside
// ISurfaceProvider&. Deliberately tiny (one method) - mirrors this
// campaign's own "many small, single-purpose interfaces, never one big god
// interface" rule (PHASE0_MASTER_STRATEGY.md, Locked Design Decision #2).
//
// `LogLevel` comes from Core/Logging.h (editor-core-separation-1 campaign,
// PHASE3) - already a plain, gte_core-owned, unconditional type, so this
// header needs no Editor-owned type at all.
//
// EditorHost's own implementation (PHASE15/16) routes this into the
// existing global log-sink mechanism (Core/LogSink.h) - functionally
// equivalent to (but a DISTINCT mechanism from) GTE_LOG_*/LogToActiveSink():
// IHostServices::Log() is Core's own INSTANCE-level hook (an object a caller
// explicitly holds a reference to), while LogToActiveSink() is the process-
// global macro path every OTHER subsystem already uses. A future Player
// host's own implementation can route this to stdout/a file/an OS log,
// whatever that host wants - Core itself never assumes which.
class IHostServices {
public:
    virtual ~IHostServices() = default;

    virtual void Log(LogLevel level, std::string_view message) = 0;
};

} // namespace gte
