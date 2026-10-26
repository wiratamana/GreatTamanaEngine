#pragma once

// Turns one frame's GPU pass records into the profiler's per-view GPU numbers.

#include "../Profiling/ProfilingTypes.h"
#include "../Renderer/GpuStampPlan.h"
#include "../Renderer/RenderGraph/RenderGraphHandles.h"

#include <cstdint>
#include <span>

namespace gte {

// The one place renderer read states become profiler statuses.
Profiling::SampleStatus ToSampleStatus(GpuReadState state) noexcept;

// The one place render graph view scopes become profiler view slots.
Profiling::ViewSlot ToViewSlot(rg::ViewScope scope) noexcept;

struct GpuMetricsInput {
    bool captureEnabled = true;

    // Indexed by ViewSlot. A view without a render target is Inactive, never zero.
    bool viewActive[Profiling::kViewSlotCount] = { true, true, true };

    std::uint32_t declaredPasses = 0;
    std::uint32_t executedPasses = 0;
    std::uint32_t disabledByToggle = 0;
    std::uint32_t droppedFrames = 0;
};

// Sums barrier and work time per view. A view is Present only when every record of it was measured and
// none was folded; a mix is Partial. Nothing measured reads Absent, never 0.
Profiling::GpuFrameMetrics BuildGpuFrameMetrics(
    std::span<const GpuPassRecord> records, const GpuFrameReport& report, const GpuMetricsInput& input) noexcept;

} // namespace gte
