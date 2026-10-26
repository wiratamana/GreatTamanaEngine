#include "Verdict.h"

#include "NameTable.h"
#include "ProfilingClock.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace gte::Profiling {

namespace {

double Median(std::array<double, VerdictConstants::kWindowFrames>& values, std::size_t count) noexcept
{
    if (count == 0) {
        return 0.0;
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(count / 2);
    std::nth_element(values.begin(), middle, values.begin() + static_cast<std::ptrdiff_t>(count));
    return *middle;
}

double Share(double part, double whole) noexcept
{
    return whole > 0.0 ? part / whole : 0.0;
}

} // namespace

VerdictFrame MakeVerdictFrame(const FrameSummary& summary, bool checksFailing) noexcept
{
    const auto ms = [](std::uint64_t ticks) { return TicksToMilliseconds(ticks); };

    VerdictFrame frame;
    frame.frameMs = summary.cpuMilliseconds;
    frame.untrackedMs = ms(summary.untrackedTicks);
    frame.blockedPresentMs = ms(summary.blockedPresentTicks);
    frame.blockedGpuMs = ms(summary.blockedGpuTicks - std::min(summary.blockedGpuTicks, summary.blockedPresentTicks));
    frame.blockedOtherMs = ms(summary.blockedOtherTicks + summary.blockedWorkerTicks);

    // Work = everything inside top-level scopes that is not a wait.
    const std::uint64_t waits = summary.blockedGpuTicks + summary.blockedOtherTicks + summary.blockedWorkerTicks;
    frame.cpuWorkMs = ms(summary.topLevelTicks - std::min(summary.topLevelTicks, waits));
    frame.stallSite = summary.largestBlockedName;
    frame.checksFailing = checksFailing;
    return frame;
}

VerdictKind DecideVerdict(const VerdictFrame& median) noexcept
{
    const double frame = median.frameMs;
    if (!(frame > 0.0)) {
        return VerdictKind::Unknown;
    }
    if (median.checksFailing || Share(median.untrackedMs, frame) > VerdictConstants::kUnreliableUntrackedShare) {
        return VerdictKind::Unreliable;
    }
    if (Share(median.blockedOtherMs, frame) > VerdictConstants::kStalledOtherShare) {
        return VerdictKind::Stalled;
    }

    const double presentShare = Share(median.blockedPresentMs, frame);
    const double gpuShare = Share(median.blockedGpuMs, frame);
    const bool presentHit = presentShare > VerdictConstants::kPresentLimitedShare;
    const bool gpuHit = gpuShare >= VerdictConstants::kGpuLimitedShare;
    if (presentHit && gpuHit) {
        return presentShare > gpuShare ? VerdictKind::PresentLimited : VerdictKind::GpuLimited;
    }
    if (presentHit) {
        return VerdictKind::PresentLimited;
    }
    if (gpuHit) {
        return VerdictKind::GpuLimited;
    }
    if (Share(median.cpuWorkMs, frame) >= VerdictConstants::kCpuLimitedShare) {
        return VerdictKind::CpuLimited;
    }
    return VerdictKind::Serial;
}

Verdict ComputeVerdict(std::span<const VerdictFrame> frames) noexcept
{
    Verdict verdict;
    const std::size_t count = std::min(frames.size(), VerdictConstants::kWindowFrames);
    if (count < VerdictConstants::kMinFrames) {
        return verdict;
    }
    const std::span<const VerdictFrame> window = frames.last(count);
    verdict.frames = static_cast<std::uint32_t>(count);

    std::array<double, VerdictConstants::kWindowFrames> scratch{};
    const auto medianOf = [&](double VerdictFrame::*field) {
        for (std::size_t i = 0; i < count; ++i) {
            scratch[i] = window[i].*field;
        }
        return Median(scratch, count);
    };

    verdict.median.frameMs = medianOf(&VerdictFrame::frameMs);
    verdict.median.cpuWorkMs = medianOf(&VerdictFrame::cpuWorkMs);
    verdict.median.blockedGpuMs = medianOf(&VerdictFrame::blockedGpuMs);
    verdict.median.blockedPresentMs = medianOf(&VerdictFrame::blockedPresentMs);
    verdict.median.blockedOtherMs = medianOf(&VerdictFrame::blockedOtherMs);
    verdict.median.untrackedMs = medianOf(&VerdictFrame::untrackedMs);

    // A failing self-check on most frames of the window taints the verdict.
    std::size_t failing = 0;
    double worstOther = -1.0;
    for (const VerdictFrame& frame : window) {
        failing += frame.checksFailing ? 1 : 0;
        if (frame.blockedOtherMs > worstOther) {
            worstOther = frame.blockedOtherMs;
            verdict.stallSite = frame.stallSite;
        }
    }
    verdict.median.checksFailing = failing * 2 > count;
    verdict.median.stallSite = verdict.stallSite;
    verdict.stallShare = Share(verdict.median.blockedOtherMs, verdict.median.frameMs);
    verdict.kind = DecideVerdict(verdict.median);
    return verdict;
}

const char* ToString(VerdictKind kind) noexcept
{
    switch (kind) {
    case VerdictKind::Unknown: return "unknown";
    case VerdictKind::Unreliable: return "unreliable";
    case VerdictKind::Stalled: return "stalled";
    case VerdictKind::PresentLimited: return "present_limited";
    case VerdictKind::GpuLimited: return "gpu_limited";
    case VerdictKind::CpuLimited: return "cpu_limited";
    case VerdictKind::Serial: return "serial";
    }
    return "unknown";
}

std::size_t FormatVerdict(const Verdict& verdict, std::span<char> out) noexcept
{
    if (out.empty()) {
        return 0;
    }
    const VerdictFrame& m = verdict.median;
    const double frame = m.frameMs;
    const auto pct = [&](double part) { return Share(part, frame) * 100.0; };

    char head[112];
    switch (verdict.kind) {
    case VerdictKind::Unknown:
        std::snprintf(head, sizeof(head), "No verdict yet: %u frames measured", verdict.frames);
        break;
    case VerdictKind::Unreliable:
        if (m.checksFailing) {
            std::snprintf(head, sizeof(head), "UNRELIABLE: a self-check fails, untracked %.0f%% of %.1f ms", pct(m.untrackedMs), frame);
        } else {
            std::snprintf(head, sizeof(head), "UNRELIABLE: untracked %.0f%% of %.1f ms", pct(m.untrackedMs), frame);
        }
        break;
    case VerdictKind::Stalled:
        std::snprintf(head, sizeof(head), "STALLED by %s %.0f%% of %.1f ms",
            m.stallSite != kInvalidNameId ? ResolveName(m.stallSite) : "other waits", pct(m.blockedOtherMs), frame);
        break;
    case VerdictKind::PresentLimited:
        std::snprintf(head, sizeof(head), "PRESENT limited (vsync / compositor): present wait %.0f%% of %.1f ms",
            pct(m.blockedPresentMs), frame);
        break;
    case VerdictKind::GpuLimited:
        std::snprintf(head, sizeof(head), "GPU is the limit: GPU wait %.0f%% of %.1f ms", pct(m.blockedGpuMs), frame);
        break;
    case VerdictKind::CpuLimited:
        std::snprintf(head, sizeof(head), "CPU is the limit: CPU work %.0f%% of %.1f ms", pct(m.cpuWorkMs), frame);
        break;
    case VerdictKind::Serial:
        std::snprintf(head, sizeof(head), "SERIAL: CPU %.0f%% + GPU %.0f%% of %.1f ms", pct(m.cpuWorkMs), pct(m.blockedGpuMs), frame);
        break;
    }

    int written = 0;
    if (verdict.kind == VerdictKind::Unknown) {
        written = std::snprintf(out.data(), out.size(), "%s", head);
    } else {
        written = std::snprintf(out.data(), out.size(), "%s (CPU %.0f%%, GPU %.0f%%, present %.0f%%, other %.0f%%, untracked %.0f%%)",
            head, pct(m.cpuWorkMs), pct(m.blockedGpuMs), pct(m.blockedPresentMs), pct(m.blockedOtherMs), pct(m.untrackedMs));
    }
    if (written < 0) {
        out[0] = '\0';
        return 0;
    }
    return std::min(static_cast<std::size_t>(written), out.size() - 1);
}

} // namespace gte::Profiling
