// Compares SceneLightingUniformData with the std140 block in DirectionalLighting.glsl,
// field by field, so a swapped or renamed member fails (a size check alone cannot see that).

#include "Core/FeatureKit/SceneSlot.h"
#include "Features/SceneLighting/SceneLightingTypes.h"
#include "../../Support/Std140Block.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace gte {
namespace {

using test_support::ParseBlock;
using test_support::Std140Block;

TEST(SceneLightingUniformLayoutTest, EveryFieldMatchesTheStd140BlockInName_Order_AndOffset)
{
    const Std140Block block =
        ParseBlock(test_support::ReadTextFile(GTE_TEST_LIGHTING_GLSL_PATH), "SceneGlobalUniformBlock");

#define GTE_EXPECT_MEMBER(name) { #name, offsetof(SceneLightingUniformData, name) }
    const std::vector<std::pair<const char*, std::size_t>> expected = {
        GTE_EXPECT_MEMBER(sunDirectionAndValid),
        GTE_EXPECT_MEMBER(sunIlluminance),
        GTE_EXPECT_MEMBER(skyAmbientAndIntensity),
        GTE_EXPECT_MEMBER(groundAmbientAndMaterialDebugView),
        GTE_EXPECT_MEMBER(shadowParams),
        GTE_EXPECT_MEMBER(shadowBias),
        GTE_EXPECT_MEMBER(cameraPositionAndFadeStart),
        GTE_EXPECT_MEMBER(cameraForwardAndFadeEnd),
        GTE_EXPECT_MEMBER(cascadeSplitFar),
        GTE_EXPECT_MEMBER(cascadeBlendWidth),
        GTE_EXPECT_MEMBER(cascadeTexelWorld),
        GTE_EXPECT_MEMBER(cascadeDepthRange),
        GTE_EXPECT_MEMBER(cascadeViewProjection0),
        GTE_EXPECT_MEMBER(cascadeViewProjection1),
        GTE_EXPECT_MEMBER(cascadeViewProjection2),
        GTE_EXPECT_MEMBER(cascadeViewProjection3),
    };
#undef GTE_EXPECT_MEMBER

    ASSERT_EQ(block.members.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(block.members[i].name, expected[i].first) << "member " << i;
        EXPECT_EQ(block.members[i].offset, expected[i].second) << expected[i].first;
    }
    EXPECT_EQ(block.size, sizeof(SceneLightingUniformData));
    EXPECT_EQ(block.size, 448u);
}

TEST(SceneLightingUniformLayoutTest, BlockFitsTheEngineUniformLimit)
{
    static_assert(sizeof(SceneLightingUniformData) <= fx::kMaxSceneUniformBytes);
    EXPECT_LE(sizeof(SceneLightingUniformData), fx::kMaxSceneUniformBytes);
}

TEST(SceneLightingUniformLayoutTest, ParserComputesStd140OffsetsForMixedTypes)
{
    const Std140Block block = ParseBlock(
        "uniform SceneGlobalUniformBlock { float a; vec3 b; float c; vec2 d; mat4 e; } u;", "SceneGlobalUniformBlock");
    ASSERT_EQ(block.members.size(), 5u);
    EXPECT_EQ(block.members[0].offset, 0u);
    EXPECT_EQ(block.members[1].offset, 16u);
    EXPECT_EQ(block.members[2].offset, 28u);
    EXPECT_EQ(block.members[3].offset, 32u);
    EXPECT_EQ(block.members[4].offset, 48u);
    EXPECT_EQ(block.size, 112u);
}

} // namespace
} // namespace gte
