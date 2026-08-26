#include "ProfilerPanelData.h"

#include <algorithm>
#include <cstdio>

namespace gte {

std::vector<Profiling::CpuScopeSample> BuildSortedCpuScopeRows(const Profiling::FrameSample& frame)
{
    std::vector<Profiling::CpuScopeSample> rows;
    rows.reserve(frame.cpuScopeCount);
    for (std::size_t i = 0; i < frame.cpuScopeCount; ++i) {
        rows.push_back(frame.cpuScopes[i]);
    }

    std::sort(rows.begin(), rows.end(),
        [](const Profiling::CpuScopeSample& a, const Profiling::CpuScopeSample& b) {
            return a.totalMilliseconds > b.totalMilliseconds;
        });

    return rows;
}

std::string FormatDuration(double milliseconds)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f ms", milliseconds);
    return std::string(buffer);
}

std::string FormatFrameTimeSummary(double cpuMilliseconds)
{
    if (cpuMilliseconds <= 0.0) {
        return "N/A";
    }

    const double fps = 1000.0 / cpuMilliseconds;
    const std::string duration = FormatDuration(cpuMilliseconds);

    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s / %.0f FPS", duration.c_str(), fps);
    return std::string(buffer);
}

std::string FormatCount(std::uint64_t value)
{
    const std::string digits = std::to_string(value);

    std::string result;
    result.reserve(digits.size() + digits.size() / 3);

    int sinceLastSeparator = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (sinceLastSeparator != 0 && sinceLastSeparator % 3 == 0) {
            result.push_back(',');
        }
        result.push_back(*it);
        ++sinceLastSeparator;
    }

    std::reverse(result.begin(), result.end());
    return result;
}

GpuPassCountDisplay ResolveGpuPassCounts(const Profiling::FrameSample& frame, Profiling::GpuPass pass)
{
    GpuPassCountDisplay display;

    const std::size_t index = static_cast<std::size_t>(pass);
    if (index >= Profiling::kGpuPassCount) {
        return display; // available stays false.
    }

    const Profiling::GpuPassSample& sample = frame.gpuPasses[index];
    if (sample.countStatus != Profiling::GpuSampleStatus::Present) {
        return display; // available stays false - never collapse Absent into a fake zero.
    }

    display.available = true;
    display.drawCallCount = sample.drawCallCount;
    display.triangleCount = sample.triangleCount;
    return display;
}

const char* ToString(Profiling::GpuPass pass)
{
    switch (pass) {
    case Profiling::GpuPass::GameView:
        return "Game View";
    case Profiling::GpuPass::SceneView:
        return "Scene View";
    case Profiling::GpuPass::Present:
        return "Present";
    }
    return "Unknown Pass";
}

std::string FormatGpuTimingLine(const Profiling::GpuPassSample& pass)
{
    if (pass.timingStatus == Profiling::GpuSampleStatus::Present) {
        return FormatDuration(pass.milliseconds);
    }
    // Absent AND Unsupported both honestly report "N/A" - never a fabricated
    // "0.00 ms" (see ProfilingTypes.h's own tri-state rule). Phase 4 (Vulkan
    // GPU timestamp queries) is what will eventually make this branch
    // actually produce a real value for some pass.
    return "N/A";
}

const char* CpuScopeTableEmptyMessage()
{
    if (!kCpuScopeInstrumentationCompiledIn) {
        return "CPU scope instrumentation is compiled out (GTE_ENABLE_PROFILER=OFF) - "
               "no scope data will ever appear here in this build.";
    }
    return "No CPU scopes recorded yet.";
}

} // namespace gte
