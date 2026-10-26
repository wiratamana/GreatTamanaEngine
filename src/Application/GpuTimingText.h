#pragma once

// GPU timing as text for panels and HTTP: a measured value or the status word, never a made-up number.

#include "../Renderer/GpuStampPlan.h"
#include "../Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"

#include <cstdio>
#include <string>

namespace gte {

// Present -> "%.2f ms", Partial -> "~%.2f ms (partial)", any other state -> its status word.
inline std::string FormatGpuTimingText(GpuReadState state, double milliseconds)
{
    char buffer[40];
    if (state == GpuReadState::Present) {
        std::snprintf(buffer, sizeof(buffer), "%.2f ms", milliseconds);
        return std::string(buffer);
    }
    if (state == GpuReadState::Partial) {
        std::snprintf(buffer, sizeof(buffer), "~%.2f ms (partial)", milliseconds);
        return std::string(buffer);
    }
    return std::string(rg::ToString(state));
}

} // namespace gte
