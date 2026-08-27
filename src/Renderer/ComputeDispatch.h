#pragma once

// Phase 4 (COMPUTE_PHASE4_DISPATCH_EXECUTION_STRATEGY_v2.md, part 4 of the
// wider COMPUTE_SHADER_MASTER_STRATEGY_v2.md campaign) - "Dispatch GPU
// workers": the correct, reusable ceiling-division arithmetic for turning
// "I need to process N total items" into "dispatch this many work groups".
//
// Deliberately Vulkan-header-FREE and pure - mirrors DrawStats.h's/
// GpuTiming.h's own "always-compiled, pure logic, Tier-1-testable"
// precedent exactly (see AGENTS.md, "Profiling"). No VkExtent3D is used
// here on purpose - Extent3D below is a plain 3-uint32_t struct so this
// header stays includable from a pure-math test file with zero Vulkan
// dependency.
//
// A ceiling division done carelessly with plain integer division
// (totalItems / localGroupSize) silently TRUNCATES and leaves the last
// partial group of items unprocessed - a correctness bug, not just a
// performance one. ComputeGroupCount()/ComputeGroupCount3D() below are the
// one place this arithmetic is done correctly, so no future compute-pass
// author has to re-derive it (and risk getting it wrong) at every new call
// site.
//
// Every GLSL shader's own declared local work-group size
// (layout(local_size_x = X, ...)) has NO automatic connection to any C++
// constant - this campaign deliberately refuses shader reflection (see
// COMPUTE_SHADER_MASTER_STRATEGY_v2.md's own "What We Will NOT Do"), so
// each concrete compute shader's own local size is restated as a named C++
// constant living NEXT TO the pass that dispatches it (e.g.
// `constexpr std::uint32_t kBoxBlurLocalSizeX = 16;`), with a comment
// pointing back at the matching `layout(local_size_x = ...)` line it must
// stay equal to - never centralized here, since each compute shader's
// optimal group size is a property of that ONE shader, not a shared engine
// convention.
#include <cassert>
#include <cstdint>

namespace gte {

// A plain 3-uint32_t extent - the Vulkan-free sibling of VkExtent3D, used
// only by ComputeGroupCount3D() below so this header never needs to
// include <volk.h>.
struct Extent3D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
};

// Returns ceil(totalItems / localGroupSize) - the number of work groups
// needed so that groupCount * localGroupSize >= totalItems, i.e. every
// item (including a final, partial group) gets processed by at least one
// invocation. `localGroupSize == 0` is treated as a caller error (asserts
// in debug builds; returns 0 in release rather than dividing by zero) -
// mirrors this engine's existing "assert in debug, stay safe in release"
// convention for a condition that should never happen (see AGENTS.md).
//
// Example: 100 items, local group size 64 -> group count 2 (128 total
// slots dispatched, the last 28 doing nothing useful but never dropping
// the remaining 36 items a naive `100 / 64 == 1` would silently lose).
constexpr std::uint32_t ComputeGroupCount(std::uint32_t totalItems, std::uint32_t localGroupSize) noexcept
{
    if (localGroupSize == 0) {
        assert(false && "ComputeGroupCount: localGroupSize must be > 0");
        return 0;
    }
    return (totalItems + localGroupSize - 1) / localGroupSize;
}

// The 3D sibling of ComputeGroupCount() above, for a shader dispatched
// over e.g. an image's width/height rather than a flat 1D buffer element
// count - applies the exact same ceiling-division formula independently
// per axis.
constexpr Extent3D ComputeGroupCount3D(const Extent3D& totalItems, const Extent3D& localGroupSize) noexcept
{
    Extent3D result;
    result.width = ComputeGroupCount(totalItems.width, localGroupSize.width);
    result.height = ComputeGroupCount(totalItems.height, localGroupSize.height);
    result.depth = ComputeGroupCount(totalItems.depth, localGroupSize.depth);
    return result;
}

} // namespace gte
