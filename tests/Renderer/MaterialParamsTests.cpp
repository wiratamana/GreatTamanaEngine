// Tier-1 tests for the per-material shader parameter block.

#include "Renderer/MaterialParams.h"
#include "../Support/Std140Block.h"

#include <cstddef>
#include <cstring>
#include <limits>

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(MaterialParamsTest, BlockIs48BytesWithValuesInTheShaderSlots)
{
    static_assert(sizeof(MaterialParams) == 48);
    static_assert(offsetof(MaterialParams, baseColorFactor) == 0);
    static_assert(offsetof(MaterialParams, alphaCutoff) == 16);
    static_assert(offsetof(MaterialParams, metallicFactor) == 20);
    static_assert(offsetof(MaterialParams, roughnessFactor) == 24);
    static_assert(offsetof(MaterialParams, normalScale) == 28);
    static_assert(offsetof(MaterialParams, occlusionStrength) == 32);

    const MaterialParams p = MakeMaterialParams(0.1f, 0.2f, 0.3f, 0.4f, 0.25f, 0.5f, 0.6f, 2.0f, 0.75f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[0], 0.1f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[1], 0.2f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[2], 0.3f);
    EXPECT_FLOAT_EQ(p.baseColorFactor[3], 0.4f);
    EXPECT_FLOAT_EQ(p.alphaCutoff, 0.25f);
    EXPECT_FLOAT_EQ(p.metallicFactor, 0.5f);
    EXPECT_FLOAT_EQ(p.roughnessFactor, 0.6f);
    EXPECT_FLOAT_EQ(p.normalScale, 2.0f);
    EXPECT_FLOAT_EQ(p.occlusionStrength, 0.75f);
}

TEST(MaterialParamsTest, DefaultsAreOpaqueWhiteWithHalfCutoff)
{
    const MaterialParams p;
    for (const float c : p.baseColorFactor) {
        EXPECT_FLOAT_EQ(c, 1.0f);
    }
    EXPECT_FLOAT_EQ(p.alphaCutoff, 0.5f);
    EXPECT_FLOAT_EQ(p.metallicFactor, 1.0f);
    EXPECT_FLOAT_EQ(p.roughnessFactor, 1.0f);
    EXPECT_FLOAT_EQ(p.normalScale, 1.0f);
    EXPECT_FLOAT_EQ(p.occlusionStrength, 1.0f);
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

TEST(MaterialParamsTest, PbrFactorsClampToTheirRanges)
{
    const MaterialParams high = MakeMaterialParams(1, 1, 1, 1, 0.5f, 5.0f, 5.0f, 100.0f, 5.0f);
    EXPECT_FLOAT_EQ(high.metallicFactor, 1.0f);
    EXPECT_FLOAT_EQ(high.roughnessFactor, 1.0f);
    EXPECT_FLOAT_EQ(high.normalScale, 8.0f);
    EXPECT_FLOAT_EQ(high.occlusionStrength, 1.0f);

    const MaterialParams low = MakeMaterialParams(1, 1, 1, 1, 0.5f, -1.0f, -1.0f, -3.0f, -1.0f);
    EXPECT_FLOAT_EQ(low.metallicFactor, 0.0f);
    EXPECT_FLOAT_EQ(low.roughnessFactor, 0.0f);
    EXPECT_FLOAT_EQ(low.normalScale, 0.0f);
    EXPECT_FLOAT_EQ(low.occlusionStrength, 0.0f);
}

TEST(MaterialParamsTest, NonFinitePbrFactorsFallBackToDefaults)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const MaterialParams p = MakeMaterialParams(1, 1, 1, 1, 0.5f, nan, inf, -inf, nan);
    EXPECT_FLOAT_EQ(p.metallicFactor, 1.0f);
    EXPECT_FLOAT_EQ(p.roughnessFactor, 1.0f);
    EXPECT_FLOAT_EQ(p.normalScale, 1.0f);
    EXPECT_FLOAT_EQ(p.occlusionStrength, 1.0f);
}

TEST(MaterialParamsTest, NegativeZeroAndPositiveZeroGiveEqualBytes)
{
    const MaterialParams a = MakeMaterialParams(-0.0f, 0.0f, -0.0f, 1.0f, -0.0f, -0.0f, -0.0f, -0.0f, -0.0f);
    const MaterialParams b = MakeMaterialParams(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(MaterialParams)), 0);
}

TEST(MaterialParamsTest, ShaderBlockMatchesTheStructLayout)
{
    const test_support::Std140Block block = test_support::ParseBlock(
        test_support::ReadTextFile(GTE_TEST_MATERIAL_PARAMS_GLSL_PATH), "MaterialParamsBlock");
    ASSERT_EQ(block.members.size(), 3u);
    EXPECT_EQ(block.members[0].name, "baseColorFactor");
    EXPECT_EQ(block.members[0].offset, offsetof(MaterialParams, baseColorFactor));
    EXPECT_EQ(block.members[1].name, "params");
    EXPECT_EQ(block.members[1].offset, offsetof(MaterialParams, alphaCutoff));
    EXPECT_EQ(block.members[2].name, "pbr");
    EXPECT_EQ(block.members[2].offset, offsetof(MaterialParams, occlusionStrength));
    EXPECT_EQ(block.size, sizeof(MaterialParams));
}

} // namespace
} // namespace gte
