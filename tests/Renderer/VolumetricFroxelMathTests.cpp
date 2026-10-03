// Unit tests for src/Renderer/VolumetricFroxelMath.h/.cpp - the shared froxel
// slice-index <-> view-depth mapping. No Vulkan/Renderer/live GPU device
// involved at all - every function under test is pure, taking/returning only
// plain float values.

#include "Renderer/VolumetricFroxelMath.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(VolumetricFroxelMathTest, SliceZeroMapsToViewDepthZero)
{
    EXPECT_NEAR(FroxelSliceToViewDepth(0.0f, 32.0f, 500.0f, 2.0f), 0.0f, 1e-5f);
}

TEST(VolumetricFroxelMathTest, SliceAtSliceCountMapsToMaxDistance)
{
    EXPECT_NEAR(FroxelSliceToViewDepth(32.0f, 32.0f, 500.0f, 2.0f), 500.0f, 1e-3f);
}

TEST(VolumetricFroxelMathTest, NonLinearExponentActuallyCurvesTheMapping)
{
    // With depthExponent == 2.0 (quadratic), the midpoint slice must map to
    // 1/4 of maxDistanceKm, not 1/2 - proving the pow() curve is really
    // exercised and this isn't secretly a linear mapping.
    const float midDepth = FroxelSliceToViewDepth(16.0f, 32.0f, 500.0f, 2.0f);
    EXPECT_NEAR(midDepth, 500.0f * 0.25f, 1e-2f);
    EXPECT_LT(midDepth, 250.0f);
}

TEST(VolumetricFroxelMathTest, ForwardThenInverseRoundTripsWithinEpsilon)
{
    struct Case {
        float slice;
        float sliceCount;
        float maxDistanceKm;
        float depthExponent;
    };
    const Case cases[] = {
        {0.0f, 32.0f, 500.0f, 1.0f},
        {8.0f, 32.0f, 500.0f, 1.0f},
        {16.0f, 32.0f, 500.0f, 2.0f},
        {24.0f, 32.0f, 500.0f, 2.0f},
        {5.0f, 16.0f, 100.0f, 3.0f},
    };

    for (const Case& c : cases) {
        const float viewDepthKm = FroxelSliceToViewDepth(c.slice, c.sliceCount, c.maxDistanceKm, c.depthExponent);
        const float roundTrippedSlice = ViewDepthToFroxelSlice(viewDepthKm, c.sliceCount, c.maxDistanceKm, c.depthExponent);
        EXPECT_NEAR(roundTrippedSlice, c.slice, 1e-2f) << "slice=" << c.slice << " depthExponent=" << c.depthExponent;
    }
}

TEST(VolumetricFroxelMathTest, ViewDepthZeroMapsToSliceZero)
{
    EXPECT_NEAR(ViewDepthToFroxelSlice(0.0f, 32.0f, 500.0f, 2.0f), 0.0f, 1e-5f);
}

TEST(VolumetricFroxelMathTest, ViewDepthAtMaxDistanceMapsToSliceCount)
{
    EXPECT_NEAR(ViewDepthToFroxelSlice(500.0f, 32.0f, 500.0f, 2.0f), 32.0f, 1e-2f);
}

} // namespace
} // namespace gte
