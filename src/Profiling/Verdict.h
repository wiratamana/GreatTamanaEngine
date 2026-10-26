#pragma once

// Frame verdict: which resource limits the frame. Pure, medians over a short window, always prints its numbers.

#include "ProfilingTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gte::Profiling {

// Every threshold lives here. Starting values, tuned only with measured data.
struct VerdictConstants {
    static constexpr std::size_t kWindowFrames = 30;
    static constexpr double kUnreliableUntrackedShare = 0.10;
    static constexpr double kStalledOtherShare = 0.20;
    static constexpr double kPresentLimitedShare = 0.30;
    static constexpr double kGpuLimitedShare = 0.40;
    static constexpr double kCpuLimitedShare = 0.60;
    static constexpr std::size_t kMinFrames = 3; // fewer frames than this give no verdict
};

enum class VerdictKind : std::uint8_t {
    Unknown,        // no data or capture off
    Unreliable,     // too much frame time is outside every scope, or a self-check fails
    Stalled,        // blocked on something that is neither the GPU nor presentation
    PresentLimited, // waiting for vsync or the compositor
    GpuLimited,
    CpuLimited,
    Serial, // CPU and GPU work one after the other
};

// One frame in milliseconds, split into the classes the rules use.
struct VerdictFrame {
    double frameMs = 0.0;
    double cpuWorkMs = 0.0;
    double blockedGpuMs = 0.0;     // rendering waits only
    double blockedPresentMs = 0.0; // acquire and present waits
    double blockedOtherMs = 0.0;   // every other Blocked scope, worker waits included
    double untrackedMs = 0.0;
    NameId stallSite = kInvalidNameId; // longest Blocked scope of the frame
    bool checksFailing = false;
};

struct Verdict {
    VerdictKind kind = VerdictKind::Unknown;
    std::uint32_t frames = 0; // frames the medians were taken over
    VerdictFrame median{};    // the numbers behind the decision
    NameId stallSite = kInvalidNameId;
    double stallShare = 0.0; // blockedOther share of the frame
};

// Splits one finished frame. A summary that was never closed gives an all-zero frame.
VerdictFrame MakeVerdictFrame(const FrameSummary& summary, bool checksFailing) noexcept;

// Decides from already-median inputs. Table-testable.
VerdictKind DecideVerdict(const VerdictFrame& median) noexcept;

// Median per field over the newest kWindowFrames entries, then DecideVerdict. At most kWindowFrames are used.
Verdict ComputeVerdict(std::span<const VerdictFrame> frames) noexcept;

const char* ToString(VerdictKind kind) noexcept;

// "GPU is the limit: GPU wait 54% of 27.8 ms (CPU 40%, present 3%, other 2%, untracked 1%)". Always cut to fit,
// always NUL terminated. Returns the length written.
std::size_t FormatVerdict(const Verdict& verdict, std::span<char> out) noexcept;

} // namespace gte::Profiling
