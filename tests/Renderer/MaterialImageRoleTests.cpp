#include "Renderer/MaterialImageRole.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace gte {
namespace {

TEST(MaterialImageRoleTest, OnlyBaseColorIsSrgb)
{
    EXPECT_EQ(MaterialImageFormat(MaterialImageRole::BaseColor), PixelFormat::RGBA8_SRGB);
    EXPECT_EQ(MaterialImageFormat(MaterialImageRole::Normal), PixelFormat::RGBA8_UNorm);
    EXPECT_EQ(MaterialImageFormat(MaterialImageRole::MetallicRoughness), PixelFormat::RGBA8_UNorm);
    EXPECT_EQ(MaterialImageFormat(MaterialImageRole::Occlusion), PixelFormat::RGBA8_UNorm);
}

TEST(MaterialImageRoleTest, EveryRoleHasADistinctLabel)
{
    std::set<std::string> labels;
    for (std::size_t i = 0; i < kMaterialImageRoleCount; ++i) {
        const std::string label = MaterialImageRoleLabel(static_cast<MaterialImageRole>(i));
        EXPECT_FALSE(label.empty());
        EXPECT_TRUE(labels.insert(label).second) << label;
    }
    EXPECT_EQ(labels.size(), kMaterialImageRoleCount);
}

} // namespace
} // namespace gte
