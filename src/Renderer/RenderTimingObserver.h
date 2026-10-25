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

// Receives every blocking wait. Calls are noexcept and may come from any thread.
// `detail` is a string literal.
class IRenderTimingObserver {
public:
    virtual void OnBlockBegin(BlockSite site, const char* detail) noexcept = 0;
    virtual void OnBlockEnd(BlockSite site, BlockOutcome outcome) noexcept = 0;

protected:
    ~IRenderTimingObserver() = default;
};

} // namespace gte
