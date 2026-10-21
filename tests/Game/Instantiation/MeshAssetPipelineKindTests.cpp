// Pipeline kind selection and the tangent-layout rule.

#include "Game/Instantiation/MeshAssetPipelineKind.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(MeshAssetPipelineKindTest, UntexturedIgnoresLayoutAndAlpha)
{
    for (const bool tangent : { false, true }) {
        for (const bool alpha : { false, true }) {
            EXPECT_EQ(ChooseMeshAssetPipelineKind(false, tangent, alpha), MeshAssetPipelineKind::Untextured);
        }
    }
}

TEST(MeshAssetPipelineKindTest, TexturedPicksPipelineByLayoutAndAlpha)
{
    EXPECT_EQ(ChooseMeshAssetPipelineKind(true, false, false), MeshAssetPipelineKind::Textured);
    EXPECT_EQ(ChooseMeshAssetPipelineKind(true, false, true), MeshAssetPipelineKind::TexturedAlphaTest);
    EXPECT_EQ(ChooseMeshAssetPipelineKind(true, true, false), MeshAssetPipelineKind::Pbr);
    EXPECT_EQ(ChooseMeshAssetPipelineKind(true, true, true), MeshAssetPipelineKind::PbrAlphaTest);
}

TEST(MeshAssetPipelineKindTest, TangentLayoutRequiresUnskinnedAlignedAllPbrMesh)
{
    EXPECT_TRUE(CanUseTangentLayout(false, 10, 10, true));
    EXPECT_FALSE(CanUseTangentLayout(true, 10, 10, true));
    EXPECT_FALSE(CanUseTangentLayout(false, 0, 0, true));
    EXPECT_FALSE(CanUseTangentLayout(false, 10, 9, true));
    EXPECT_FALSE(CanUseTangentLayout(false, 10, 0, true));
    EXPECT_FALSE(CanUseTangentLayout(false, 10, 10, false));
}

} // namespace
} // namespace gte
