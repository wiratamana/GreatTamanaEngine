#pragma once

// Constants shared with the cull and pyramid shaders. The build turns every `inline constexpr` line
// below into ClusterCullConstants.glsl, so keep each line in one of these forms:
//   std::uint32_t NAME = <decimal> | 1u << <bit>;      float NAME = <digits>.<digits>[e<exp>]f;

#include <cstdint>

namespace gte {

// Bits of ClusterInstanceGpu::info[0].
inline constexpr std::uint32_t kClusterFlagUniformScale = 1u << 0;
inline constexpr std::uint32_t kClusterFlagPositiveHand = 1u << 1;

// Bits of ClusterCullParams::counts[3].
inline constexpr std::uint32_t kClusterCullFlagFrustum = 1u << 0;
inline constexpr std::uint32_t kClusterCullFlagBackfaceCone = 1u << 1;
inline constexpr std::uint32_t kClusterCullFlagOcclusion = 1u << 2; // Single-phase test against a same-pose pyramid.
inline constexpr std::uint32_t kClusterCullFlagOrthographic = 1u << 3; // Set from the camera, never by a caller.
inline constexpr std::uint32_t kClusterCullFlagPhaseFirst = 1u << 5; // Draws last frame's survivors.
inline constexpr std::uint32_t kClusterCullFlagPhaseSecond = 1u << 6; // Tests every item against this frame's pyramid.
inline constexpr std::uint32_t kClusterCullFlagHistoryValid = 1u << 7; // The history words are trustworthy.
inline constexpr std::uint32_t kClusterCullFlagRawDepth = 1u << 8; // Set when no depth map exists.

// Bits of one history word.
inline constexpr std::uint32_t kClusterHistoryVisible = 1u << 0; // Visible after the last second phase.
inline constexpr std::uint32_t kClusterHistoryDrawnFirst = 1u << 1; // Drawn by the first phase of this frame.

// The projected rect grows by this many level-0 texels on every side.
inline constexpr float kHizRectMarginTexels = 1.0f;

// Occlusion margins on view depth (perspective) and on raw depth (no depth map).
inline constexpr float kHizRelMargin = 0.002f;
inline constexpr float kHizAbsMargin = 0.01f;
inline constexpr float kHizDepthGuard = 4.8e-7f; // About eight depth steps near 1.0.
inline constexpr float kHizRawBias = 0.001f;

// Largest pyramid: level 0 up to 8192 texels per side (scene depth up to 16384), levels 0..13.
inline constexpr std::uint32_t kHizMaxLevels = 14;

// The base build pass writes levels 0..4; the top pass writes level 5 and above.
inline constexpr std::uint32_t kHizTopFirstLevel = 5;

} // namespace gte
