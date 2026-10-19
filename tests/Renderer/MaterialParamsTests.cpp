// Tier-1 tests for the per-material shader parameter block.

#include "Renderer/MaterialParams.h"

#include <cstddef>
#include <limits>

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(MaterialParamsTest, BlockIs32BytesWithValuesInTheShaderSlots)
{
    static_assert(sizeof(MaterialParams) == 32);
    static_assert(offsetof(MaterialParams, baseColorFactor) == 0);
    static_assert(offsetof(MaterialParams, alphaCutoff) == 16);

    const MaterialParams p = MakeMaterialParams(0.1f, 0.2f, 0.3f, 0.4f, 0.25f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[0], 0.1f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[1], 0.2f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[2], 0.3f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[3], 0.4f);
    EXPECT_FLOAT_EQ(p.alphaCutoff, 0.25f);
}

TEST(MaterialParamsTest, DefaultsAreOpaqueWhiteWithHalfCutoff)
{
    const MaterialParams p;
    for (const float c : p.baseColorFactor) {
        EXPECT_FLOAT_EQ(c, 1.0f);
    }
    EXPECT_FLOAT_EQ(p.alphaCutoff, 0.5f);
    EXPECT_EQ(p, MakeMaterialParams(1.0f, 1.0f, 1.0f, 1.0f, 0.5f));
}

TEST(MaterialParamsTest, ClampsAndReplacesNonFiniteValues)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const MaterialParams p = MakeMaterialParams(-1.0f, 7.0f, nan, std::numeric_limits<float>::infinity(), nan);
    EXPECT_FLOAT_EQ(p.baseColorFactor[0], 0.0f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[1], 1.0f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[2], 1.0f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[3], 1.0f);
    EXPECT_FLOAT_EQ(p.alphaCutoff, 0.5f);
}

} // namespace
} // namespace gte
