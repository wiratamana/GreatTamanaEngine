#pragma once

namespace gte {

// CPU reference for the froxel slice <-> view depth mapping in src/Shaders/VolumetricFroxelMath.glsl.
// If the two disagree, this file is right.

// slice boundary -> view-space distance, same units as maxDistanceKm.
float FroxelSliceToViewDepth(float slice, float sliceCount, float maxDistanceKm, float depthExponent) noexcept;

// Inverse of FroxelSliceToViewDepth() above - view-space distance -> (fractional) slice boundary.
float ViewDepthToFroxelSlice(float viewDepthKm, float sliceCount, float maxDistanceKm, float depthExponent) noexcept;

} // namespace gte
