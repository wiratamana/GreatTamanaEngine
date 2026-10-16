// Froxel slice index <-> view depth mapping for Z-sliced volumes.
// Mirrors src/Renderer/VolumetricFroxelMath.h; the CPU side is the reference.

// Slice boundary -> view-space distance, same units as maxDistanceKm.
float FroxelSliceToViewDepth(float slice, float sliceCount, float maxDistanceKm, float depthExponent)
{
    float u = clamp(slice / max(sliceCount, 1e-6), 0.0, 1.0);
    return maxDistanceKm * pow(u, depthExponent);
}

// Inverse of FroxelSliceToViewDepth(): view-space distance -> (fractional) slice boundary.
float ViewDepthToFroxelSlice(float viewDepthKm, float sliceCount, float maxDistanceKm, float depthExponent)
{
    float u = clamp(viewDepthKm / max(maxDistanceKm, 1e-6), 0.0, 1.0);
    return pow(u, 1.0 / max(depthExponent, 1e-6)) * sliceCount;
}
