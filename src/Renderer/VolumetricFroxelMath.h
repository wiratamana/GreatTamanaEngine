#pragma once

namespace gte {

// Shared, feature-free CPU oracle for the froxel slice-index <-> view-depth
// mapping any Z-sliced VolumeTexture fill can reuse. Mirrors
// src/Shaders/VolumetricFroxelMath.glsl byte-for-formula - if the two ever
// disagree, this file is right by definition (see AtmosphereMath.h's own
// identical "CPU oracle first" rule).

// slice boundary -> view-space distance, same units as maxDistanceKm.
float FroxelSliceToViewDepth(float slice, float sliceCount, float maxDistanceKm, float depthExponent) noexcept;

// Inverse of FroxelSliceToViewDepth() above - view-space distance -> (fractional) slice boundary.
float ViewDepthToFroxelSlice(float viewDepthKm, float sliceCount, float maxDistanceKm, float depthExponent) noexcept;

} // namespace gte
