// Unit tests for ResolveOverridePipeline() (src/Game/SceneQuery.h) - a pure
// data-in/data-out decision with no Renderer/live Pipeline involved at all,
// mirrors tests/Game/RenderBatchingTests.cpp's own Tier-1 precedent.

#include "Game/SceneQuery.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(ResolveOverridePipelineTest, ReturnsNulloptWhenLayoutHasNoEntry)
{
    PipelineOverrideSet overrides;
    EXPECT_FALSE(ResolveOverridePipeline(overrides, VertexLayout::PositionColor).has_value());
}

TEST(ResolveOverridePipelineTest, ReturnsTheHandleRegisteredForThatLayout)
{
    PipelineOverrideSet overrides;
    const PipelineHandle handle{ 7, 1 };
    overrides.byLayout[static_cast<std::size_t>(VertexLayout::PositionNormalUv)] = handle;

    EXPECT_EQ(ResolveOverridePipeline(overrides, VertexLayout::PositionNormalUv), handle);
    EXPECT_FALSE(ResolveOverridePipeline(overrides, VertexLayout::PositionNormal).has_value());
}

TEST(ResolveOverridePipelineTest, DifferentLayoutsResolveIndependently)
{
    PipelineOverrideSet overrides;
    const PipelineHandle colorHandle{ 1, 1 };
    const PipelineHandle normalHandle{ 2, 1 };
    overrides.byLayout[static_cast<std::size_t>(VertexLayout::PositionColor)] = colorHandle;
    overrides.byLayout[static_cast<std::size_t>(VertexLayout::PositionNormal)] = normalHandle;

    EXPECT_EQ(ResolveOverridePipeline(overrides, VertexLayout::PositionColor), colorHandle);
    EXPECT_EQ(ResolveOverridePipeline(overrides, VertexLayout::PositionNormal), normalHandle);
    EXPECT_FALSE(ResolveOverridePipeline(overrides, VertexLayout::PositionNormalUv).has_value());
    EXPECT_FALSE(ResolveOverridePipeline(overrides, VertexLayout::PositionNormalInstanced).has_value());
}

} // namespace
} // namespace gte
