#pragma once

#include "Logging.h"

#include <string_view>

// PHASE3 (editor-core-separation-1 campaign,
// PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - the global, registrable
// log-sink mechanism GTE_LOG_* (Core/Logging.h) routes through instead of
// calling ::gte::Logger::Log() directly. gte_core NEVER implements
// ILogSink itself and NEVER includes anything under src/Editor/ - only
// gte_editor's Logger (via its new LoggerLogSink wrapper, Editor/Logger.h)
// installs a real sink, exactly once, at startup. A Player host that never
// calls InstallLogSink() gets a safe, silent no-op automatically - no
// macro/switch needed for that "OFF" behavior at all (Locked Design
// Decision #1, PHASE0_MASTER_STRATEGY.md).

namespace gte {

// One small, single-purpose interface any log CONSUMER implements. Mirrors
// the already-proven FrameDebuggerCaptureContext* opaque-pointer/interface
// shape this campaign reuses everywhere else (PHASE0's Locked Design
// Decision #2) - Core holds/uses only a plain, nullable pointer to this
// interface (via the free functions below), never a concrete Editor type.
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Log(LogLevel level, std::string_view category, std::string_view message) = 0;
};

// Install-once, idempotent (safe to call more than once - see LogSink.cpp
// for exactly what happens then), process-global sink pointer - mirrors
// SdlMemoryTracker's own precedent (AGENTS.md, "CPU Dependency Memory
// Tracking"): call once, before the sink is first used, from whichever
// host composes the app (Application today; EditorHost from Phase 15
// onward). Passing nullptr is legal and means "uninstall" (LogToActiveSink()
// becomes a safe no-op again, and IsLogSinkInstalled() reports false) -
// useful for tests that need a clean slate between cases. A second
// Install call with a DIFFERENT non-null pointer is accepted too (simple
// last-write-wins, never asserted/rejected) - this repo's own single
// composition-root call site only ever calls this once in practice, so the
// only realistic "second call" caller is a test harness swapping in a
// fake sink deliberately.
void InstallLogSink(ILogSink* sink) noexcept;

// True once InstallLogSink() has most recently been called with a
// non-null pointer - the cheap, always-safe-to-call check GTE_LOG_*
// (Core/Logging.h) uses to skip building the category/message expressions
// entirely when nothing is listening.
bool IsLogSinkInstalled() noexcept;

// Routes one log entry to whichever sink is currently installed - a true,
// silent no-op if none is. Never called directly by feature code - see
// the GTE_LOG_* macros (Core/Logging.h), which already guard this call
// behind IsLogSinkInstalled() themselves.
void LogToActiveSink(LogLevel level, std::string_view category, std::string_view message);

} // namespace gte
