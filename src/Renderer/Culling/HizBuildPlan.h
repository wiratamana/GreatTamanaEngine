#pragma once

// Pyramid build: push block shared by HizBuildBase.comp and HizBuildTop.comp, pass names and dispatch plan.

#include "ClusterCullMath.h"
#include "ClusterPassCounts.h"

#include <cstdint>

namespace gte {

// Level-0 texels per workgroup side of the base pass (one workgroup reduces a 32 x 32 depth tile).
inline constexpr std::uint32_t kHizBaseTile = 16;
// Threads of the top pass, which is one workgroup. Levels from kHizTopFirstLevel up belong to it.
inline constexpr std::uint32_t kHizTopThreads = 256;

// Pass names are "<base>.Base" and "<base>.Top".
inline constexpr const char* kHizBasePassSuffix = ".Base";
inline constexpr const char* kHizTopPassSuffix = ".Top";

// Push block of both build shaders.
struct HizBuildPush {
    std::uint32_t sizes[4];  // xy = scene depth size, zw = level 0 size.
    std::uint32_t levels[4]; // x = level count.
};
static_assert(sizeof(HizBuildPush) == 32);

constexpr HizBuildPush MakeHizBuildPush(
    std::uint32_t sceneWidth, std::uint32_t sceneHeight, const HizLayout& layout) noexcept
{
    return HizBuildPush{ { sceneWidth, sceneHeight, layout.baseWidth, layout.baseHeight },
        { layout.levelCount, 0, 0, 0 } };
}

// Thread counts of the two dispatches. The top pass is one workgroup and only runs when a level above 4 exists.
struct HizBuildPlan {
    std::uint32_t baseThreadsX = 0;
    std::uint32_t baseThreadsY = 0;
    std::uint32_t topThreadsX = 0;
    bool runTop = false;
};

constexpr HizBuildPlan HizBuildPlanFor(const HizLayout& layout) noexcept
{
    HizBuildPlan plan;
    plan.baseThreadsX = layout.baseWidth;
    plan.baseThreadsY = layout.baseHeight;
    plan.topThreadsX = kHizTopThreads;
    plan.runTop = layout.levelCount > kHizTopFirstLevel;
    return plan;
}

} // namespace gte
