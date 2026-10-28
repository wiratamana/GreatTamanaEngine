// Tier-1 tests for the pure part of a scene redraw: the request DrawScene receives.

#include "Game/SceneDrawPass.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(SceneDrawRequestTest, CopiesTheCameraAndTheOverrideTable)
{
    PipelineOverrideSet overrides;
    overrides.byLayout[static_cast<std::size_t>(VertexLayout::PositionNormal)] = PipelineHandle{ 3, 1 };
    const Mat4 camera = Mat4::Translation(Vec3{ 1.0f, 2.0f, 3.0f });

    const SceneDrawRequest request = MakeSceneDrawRequest(camera, overrides);

    ASSERT_TRUE(request.pipelineOverrideSet.has_value());
    const std::optional<PipelineHandle> resolved =
        ResolveOverridePipeline(*request.pipelineOverrideSet, VertexLayout::PositionNormal);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->index, 3u);
    EXPECT_FALSE(ResolveOverridePipeline(*request.pipelineOverrideSet, VertexLayout::PositionColor).has_value());
    EXPECT_EQ(request.viewProjection(0, 3), camera(0, 3));
    EXPECT_EQ(request.viewProjection(2, 3), camera(2, 3));
}

TEST(SceneDrawRequestTest, LeavesEveryOtherFieldAtItsDefault)
{
    const SceneDrawRequest request = MakeSceneDrawRequest(Mat4::Identity(), PipelineOverrideSet{});
    EXPECT_EQ(request.frameDebuggerCapture, nullptr);
    EXPECT_EQ(request.batchedEntities, nullptr);
    EXPECT_FALSE(request.maxDrawCount.has_value());
    EXPECT_EQ(request.sceneServicesSet, DescriptorSetHandle::Invalid);
}

TEST(SceneCullRequestTest, AggregateInitKeepsCountsOptionalAndZeroed)
{
    const SceneCullRequest request{ Mat4::Identity(), 0.05f };
    EXPECT_EQ(request.counts, nullptr);
    EXPECT_FLOAT_EQ(request.paddingFraction, 0.05f);

    const SceneCullCounts counts;
    EXPECT_EQ(counts.tested, 0u);
    EXPECT_EQ(counts.culled, 0u);
    EXPECT_EQ(counts.unbounded, 0u);
}

} // namespace
} // namespace gte
