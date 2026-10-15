#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gte {

// One GPU-driven batch's culling readout for the editor, rebuilt every frame by Core.
struct GpuDrivenBatchDebugInfo {
    // Stable batch name (e.g. "GpuDrivenBatch0"); one entry covers all of the batch's passes.
    std::string batchName;
    // Instance count before culling; always > 0.
    std::uint32_t instanceCount = 0;
    // Instances that survived frustum culling, read back from the visible-count buffer.
    // Empty until the batch's buffers exist.
    std::optional<std::uint32_t> visibleCount;
};

} // namespace gte
