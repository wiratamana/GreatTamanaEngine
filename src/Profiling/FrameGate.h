#pragma once

#include <atomic>
#include <cstdint>

namespace gte::Profiling {

enum class ScopeAdmit : std::uint8_t {
    Record,  // belongs to the open frame
    Ignore,  // capture was off when the scope started
    Late,    // the scope's frame already ended
    Foreign, // finished on a thread other than the main thread
};

struct GateCounters {
    std::uint32_t lateScopes = 0;
    std::uint32_t foreignScopes = 0;
};

// Decides whether a finished scope belongs to the open frame.
// A scope reads the frame token when it starts and is stored only if the token still matches when it ends.
class FrameGate {
public:
    // Main thread. A token is never 0 for a captured frame. Capture off gives token 0.
    void BeginFrame(bool captureOn) noexcept
    {
        ++m_frameCounter;
        m_openToken.store(captureOn ? m_frameCounter : 0, std::memory_order_release);
    }

    void EndFrame() noexcept { m_openToken.store(0, std::memory_order_release); }

    // Any thread. 0 means nothing will be recorded.
    std::uint64_t ScopeToken() const noexcept { return m_openToken.load(std::memory_order_acquire); }

    // Counts late and foreign scopes.
    ScopeAdmit Admit(std::uint64_t startToken) noexcept
    {
        if (startToken == 0) {
            return ScopeAdmit::Ignore;
        }
        if (!IsMainThread()) {
            m_foreign.fetch_add(1, std::memory_order_relaxed);
            return ScopeAdmit::Foreign;
        }
        if (startToken != m_openToken.load(std::memory_order_acquire)) {
            m_late.fetch_add(1, std::memory_order_relaxed);
            return ScopeAdmit::Late;
        }
        return ScopeAdmit::Record;
    }

    // Returns and clears the drop counters.
    GateCounters TakeCounters() noexcept
    {
        return { m_late.exchange(0, std::memory_order_relaxed), m_foreign.exchange(0, std::memory_order_relaxed) };
    }

    // The calling thread becomes the one thread allowed to append scopes.
    static void MarkMainThread() noexcept { MainThreadFlag() = true; }
    static bool IsMainThread() noexcept { return MainThreadFlag(); }

private:
    static bool& MainThreadFlag() noexcept
    {
        static thread_local bool isMain = false;
        return isMain;
    }

    std::uint64_t m_frameCounter = 0; // main thread only
    std::atomic<std::uint64_t> m_openToken{ 0 };
    std::atomic<std::uint32_t> m_late{ 0 };
    std::atomic<std::uint32_t> m_foreign{ 0 };
};

} // namespace gte::Profiling
