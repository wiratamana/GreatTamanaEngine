// Tier-1 text checks of the cluster viewer shaders.

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

namespace gte {
namespace {

std::string ReadShaderFile(const char* name)
{
    std::ifstream file(std::string(GTE_TEST_SHADERS_DIR) + "/" + name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

TEST(ClusterViewShaderText, NoViewerFragmentShaderDeclaresAPushBlock)
{
    // The cluster draw pushes the vertex stage only.
    for (const char* name : { "ClusterViewClay.frag", "ClusterViewTint.frag", "ClusterViewDim.frag", "ClusterViewId.frag" }) {
        const std::string text = ReadShaderFile(name);
        ASSERT_FALSE(text.empty()) << name;
        EXPECT_FALSE(Contains(text, "push_constant")) << name;
    }
}

TEST(ClusterViewShaderText, LitShadersIncludeTheSharedShading)
{
    for (const char* name : { "ClusterViewClay.frag", "ClusterViewTint.frag" }) {
        EXPECT_TRUE(Contains(ReadShaderFile(name), "#include \"ClusterViewShading.glsl\"")) << name;
    }
}

TEST(ClusterViewShaderText, IdShaderReadsOnlyTheFlatClusterId)
{
    const std::string text = ReadShaderFile("ClusterViewId.frag");
    EXPECT_TRUE(Contains(text, "layout(location = 2) flat in uint inClusterId;"));
    EXPECT_TRUE(Contains(text, "inClusterId + 1u"));
}

TEST(ClusterViewShaderText, ClusterVertexShaderStaysInvariant)
{
    // Equal depth between the survivor draw, the dim draw and the id pass depends on it.
    EXPECT_TRUE(Contains(ReadShaderFile("MeshCluster.vert"), "invariant gl_Position;"));
}

TEST(ClusterViewShaderText, HizBuildPushBlockIsTwoUvec4)
{
    // Matches the renderer's 32-byte push struct: levelInfo[4] and sizes[4] as uints.
    const std::string text = ReadShaderFile("HizBuild.comp");
    const std::size_t block = text.find("layout(push_constant)");
    ASSERT_NE(block, std::string::npos);
    const std::size_t end = text.find("} push;", block);
    ASSERT_NE(end, std::string::npos);
    const std::string body = text.substr(block, end - block);
    EXPECT_TRUE(Contains(body, "uvec4 levelInfo;"));
    EXPECT_TRUE(Contains(body, "uvec4 sizes;"));
}

} // namespace
} // namespace gte
