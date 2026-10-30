#pragma once

// Frame time graph picture drawn on the CPU from snapshot frame rows. It never reads GPU pixels.
// Lines: frame (grey), CPU busy (blue), GPU (orange). A metric that is not Present is a gap.

#include "../Editor/ViewportProjection.h"
#include "../Profiling/ProfilerSnapshot.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte::Network {

inline constexpr int kGraphWidthDefault = 1200;
inline constexpr int kGraphWidthMin = 200;
inline constexpr int kGraphWidthMax = 2400;
inline constexpr int kGraphHeightDefault = 300;
inline constexpr int kGraphHeightMin = 100;
inline constexpr int kGraphHeightMax = 800;
inline constexpr int kGraphFramesDefault = 120;
inline constexpr int kGraphFramesMin = 30;
inline constexpr int kGraphFramesMax = 120;

// Empty text means the default; anything else must be a whole number within [minValue, maxValue].
std::optional<int> ParseGraphParam(const std::string& text, int defaultValue, int minValue, int maxValue);

// Pixel geometry of the graph: image size and the filled rectangles, back to front.
struct GraphPixelPlan {
    int width = 0;
    int height = 0;
    std::vector<OverlayQuad2D> quads;
};

// Plans the newest kGraphFramesMax rows of `frames` (oldest first). Size is clamped to the limits.
GraphPixelPlan PlanGraphPixels(std::span<const Profiling::SnapshotFrameRow> frames, int width, int height);

// PNG bytes of the plan on an opaque background.
std::vector<std::uint8_t> RenderGraphPng(const GraphPixelPlan& plan);

} // namespace gte::Network
