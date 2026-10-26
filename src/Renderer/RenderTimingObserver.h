#pragma once

// Vulkan-free vocabulary for blocking GPU waits and the observer that sees them.

#include <cstdint>

namespace gte {

// Where the CPU blocks on the GPU. Every wait names its site.
enum class BlockSite : std::uint8_t {
    PreviousOffscreen,
    Offscreen,
    PresentInFlight,
    AcquireImage,
    PresentQueue,
    ImmediateSubmit,
    WarmUp,
    SwapchainRecreate,
    TargetResize,
    CaptureReadback,
    ResourceRelease,
    CodeUnload,
    Shutdown,
    ImGuiBackend,
};

inline constexpr std::uint32_t kBlockSiteCount = static_cast<std::uint32_t>(BlockSite::ImGuiBackend) + 1;

enum class BlockOutcome : std::uint8_t {
    Completed,
    TimedOut,
    DeviceLost,
    Failed,
};

constexpr const char* ToString(BlockSite site) noexcept
{
    switch (site) {
    case BlockSite::PreviousOffscreen: return "PreviousOffscreen";
    case BlockSite::Offscreen: return "Offscreen";
    case BlockSite::PresentInFlight: return "PresentInFlight";
    case BlockSite::AcquireImage: return "AcquireImage";
    case BlockSite::PresentQueue: return "PresentQueue";
    case BlockSite::ImmediateSubmit: return "ImmediateSubmit";
    case BlockSite::WarmUp: return "WarmUp";
    case BlockSite::SwapchainRecreate: return "SwapchainRecreate";
    case BlockSite::TargetResize: return "TargetResize";
    case BlockSite::CaptureReadback: return "CaptureReadback";
    case BlockSite::ResourceRelease: return "ResourceRelease";
    case BlockSite::CodeUnload: return "CodeUnload";
    case BlockSite::Shutdown: return "Shutdown";
    case BlockSite::ImGuiBackend: return "ImGuiBackend";
    }
    return "Unknown";
}

constexpr const char* ToString(BlockOutcome outcome) noexcept
{
    switch (outcome) {
    case BlockOutcome::Completed: return "Completed";
    case BlockOutcome::TimedOut: return "TimedOut";
    case BlockOutcome::DeviceLost: return "DeviceLost";
    case BlockOutcome::Failed: return "Failed";
    }
    return "Unknown";
}

// Stages of one graph execution after the passes were declared.
enum class RenderPhase : std::uint8_t {
    Compile, // dependency sort and culling
    Record,  // barriers and pass bodies
    Finish,  // snapshots and persistent resource bookkeeping
};

constexpr const char* ToString(RenderPhase phase) noexcept
{
    switch (phase) {
    case RenderPhase::Compile: return "Compile";
    case RenderPhase::Record: return "Record";
    case RenderPhase::Finish: return "Finish";
    }
    return "Unknown";
}

// Sees every blocking wait and the CPU stages of graph execution. Calls are noexcept.
// The observer must be thread-safe: waits report from any thread.
class IRenderTimingObserver {
public:
    virtual void OnBlockBegin(BlockSite site, const char* detail) noexcept = 0;
    virtual void OnBlockEnd(BlockSite site, BlockOutcome outcome) noexcept = 0;

    virtual void OnPhaseBegin(RenderPhase phase) noexcept = 0;
    virtual void OnPhaseEnd(RenderPhase phase) noexcept = 0;

    virtual void OnDeclareBegin(const char* provider) noexcept = 0;
    virtual void OnDeclareEnd() noexcept = 0;

    virtual void OnPassRecordBegin(const char* pass) noexcept = 0;
    virtual void OnPassRecordEnd() noexcept = 0;

protected:
    ~IRenderTimingObserver() = default;
};

// Brackets a phase. End() may be called early; the destructor ends it once.
class PhaseScope {
public:
    PhaseScope(IRenderTimingObserver* observer, RenderPhase phase) noexcept
        : m_observer(observer)
        , m_phase(phase)
    {
        if (m_observer != nullptr) {
            m_observer->OnPhaseBegin(m_phase);
        }
    }

    ~PhaseScope() { End(); }

    void End() noexcept
    {
        if (m_observer != nullptr) {
            m_observer->OnPhaseEnd(m_phase);
            m_observer = nullptr;
        }
    }

    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;

private:
    IRenderTimingObserver* m_observer;
    RenderPhase m_phase;
};

// Brackets one provider's declaration.
class DeclareScope {
public:
    DeclareScope(IRenderTimingObserver* observer, const char* provider) noexcept
        : m_observer(observer)
    {
        if (m_observer != nullptr) {
            m_observer->OnDeclareBegin(provider != nullptr ? provider : "");
        }
    }

    ~DeclareScope()
    {
        if (m_observer != nullptr) {
            m_observer->OnDeclareEnd();
        }
    }

    DeclareScope(const DeclareScope&) = delete;
    DeclareScope& operator=(const DeclareScope&) = delete;

private:
    IRenderTimingObserver* m_observer;
};

// Brackets the recording of one pass.
class PassRecordScope {
public:
    PassRecordScope(IRenderTimingObserver* observer, const char* pass) noexcept
        : m_observer(observer)
    {
        if (m_observer != nullptr) {
            m_observer->OnPassRecordBegin(pass != nullptr ? pass : "");
        }
    }

    ~PassRecordScope()
    {
        if (m_observer != nullptr) {
            m_observer->OnPassRecordEnd();
        }
    }

    PassRecordScope(const PassRecordScope&) = delete;
    PassRecordScope& operator=(const PassRecordScope&) = delete;

private:
    IRenderTimingObserver* m_observer;
};

} // namespace gte
