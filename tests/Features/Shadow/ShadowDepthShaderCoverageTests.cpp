// Every vertex layout of the depth-only set has a ShadowDepth<suffix>.vert source file.

#include "Core/FeatureKit/SceneShaderSet.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace gte {
namespace {

TEST(ShadowDepthShaderCoverageTest, EveryCoveredLayoutHasAVertexShaderFile)
{
    const std::filesystem::path dir = GTE_TEST_SHADOW_SHADER_DIR;
    ASSERT_TRUE(std::filesystem::is_directory(dir)) << dir.string();

    for (const VertexLayout layout : fx::DepthOnlyLayouts()) {
        const std::string suffix(fx::DepthOnlyShaderSuffix(layout));
        ASSERT_FALSE(suffix.empty());
        const std::filesystem::path file = dir / ("ShadowDepth" + suffix + ".vert");
        EXPECT_TRUE(std::filesystem::is_regular_file(file)) << file.string();
    }
}

} // namespace
} // namespace gte
