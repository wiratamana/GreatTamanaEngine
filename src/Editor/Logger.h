#pragma once

#include "../Core/LogSink.h"
#include "../Core/Logging.h"

#include <string>
#include <string_view>
#include <vector>

// PHASE3 (editor-core-separation-1 campaign,
// PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - LogLevel/LogEntry/
// LogQueryFilter/ToString()/TryParseLogLevel()/the GTE_LOG_* macros
// themselves all moved OUT of this file and into the new, gte_core-owned
// Core/Logging.h (included above) - see that file's own header comment for
// the full "why". This file now owns ONLY the real ring-buffer store
// (Logger, below) and its bridge into the new global ILogSink mechanism
// (LoggerLogSink, also below) - both genuinely Editor-only concepts.

namespace gte {

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
//
// PHASE3 note: Logger deliberately does NOT itself inherit ILogSink (even
// though it conceptually IS one) - a static Log(LogLevel, const
// std::string&, const std::string&) (this class's own real, tested,
// pre-existing API - see Editor/LoggerTests.cpp) and a virtual
// Log(LogLevel, std::string_view, std::string_view) override
// (ILogSink's required signature) on the SAME class name a caller invokes
// via a bare qualified-id (`Logger::Log(...)`, used throughout this
// engine's own test suite and NetworkRoutes.h's route handlers) genuinely
// AMBIGUATES overload resolution for any string-literal argument (verified
// directly via a small standalone g++ repro during this phase - both
// `const std::string&` and `std::string_view` are equally-ranked
// user-defined conversion sequences from a `const char*` literal). See
// LoggerLogSink below instead - a small, separate forwarding type.
class Logger {
public:
    static constexpr std::size_t kCapacity = 2000;

    // Records one entry, stamping it with the CURRENT SetCurrentFrame()
    // value and a timestamp relative to this process's first ever
    // Logger::Log() call. If the ring buffer is already at kCapacity, the
    // single oldest entry is evicted first (FIFO). Never called directly
    // by feature code - see the GTE_LOG_* macros (Core/Logging.h).
    // Deliberately NOT noexcept (see this file's own "Important nuances"
    // note in PHASE1_CORE_LOGGER_MODULE.md): it locks a mutex and
    // allocates (std::string/LogEntry copies), either of which can
    // theoretically throw.
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

// PHASE3 (editor-core-separation-1 campaign) - the ONE concrete ILogSink
// implementation this engine ships. A small, separate forwarding type
// (see Logger's own class comment above for exactly why it isn't Logger
// itself) - installed once via InstallLogSink(&LoggerLogSink::Instance())
// by Application's constructor today (Phase 16 of this campaign moves that
// one call site into EditorHost instead - see this file's own future
// history). Defined identically regardless of GTE_ENABLE_EDITOR (both
// branches of Logger above share the exact same Log() signature), so this
// type itself needs no #if - a GTE_ENABLE_EDITOR=OFF build's version simply
// forwards into Logger's own no-op Log().
class LoggerLogSink : public ILogSink {
public:
    void Log(LogLevel level, std::string_view category, std::string_view message) override
    {
        Logger::Log(level, std::string(category), std::string(message));
    }

    // Meyers singleton - lazily constructed on first use, destroyed at
    // static-destruction time, exactly like every other process-global
    // singleton accessor already in this codebase.
    static LoggerLogSink& Instance() noexcept
    {
        static LoggerLogSink s_instance;
        return s_instance;
    }
};

} // namespace gte
