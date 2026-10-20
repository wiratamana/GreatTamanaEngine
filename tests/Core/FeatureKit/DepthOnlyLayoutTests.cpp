// Every vertex layout is either covered by the depth-only shader set or explicitly excluded.

#include "Core/FeatureKit/SceneShaderSet.h"
#include "Renderer/VertexLayout.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>

namespace gte {
namespace {

bool IsExcluded(VertexLayout layout)
{
    return layout == VertexLayout::None || layout == VertexLayout::PositionNormalInstanced;
}

TEST(DepthOnlyLayoutTest, EveryLayoutIsCoveredOrExcluded)
{
    const std::span<const VertexLayout> covered = fx::DepthOnlyLayouts();
    for (std::size_t i = 0; i < kVertexLayoutCount; ++i) {
        const VertexLayout layout = static_cast<VertexLayout>(i);
        const bool isCovered = std::find(covered.begin(), covered.end(), layout) != covered.end();
        EXPECT_NE(isCovered, IsExcluded(layout)) << "layout index " << i;
    }
}

TEST(DepthOnlyLayoutTest, SuffixIsNonEmptyExactlyForCoveredLayouts)
{
    const std::span<const VertexLayout> covered = fx::DepthOnlyLayouts();
    for (std::size_t i = 0; i < kVertexLayoutCount; ++i) {
        const VertexLayout layout = static_cast<VertexLayout>(i);
        const bool isCovered = std::find(covered.begin(), covered.end(), layout) != covered.end();
        EXPECT_EQ(!fx::DepthOnlyShaderSuffix(layout).empty(), isCovered) << "layout index " << i;
    }
}

TEST(DepthOnlyLayoutTest, TangentLayoutHasItsOwnSuffix)
{
    EXPECT_EQ(fx::DepthOnlyShaderSuffix(VertexLayout::PositionNormalUvTangent), "PositionNormalUvTangent");
}

} // namespace
} // namespace gte
