// Tier-1 text checks that ClusterCull.comp keeps the constants and layouts of ClusterCullMath.h.

#include "Renderer/Culling/ClusterCullMath.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

namespace gte {
namespace {

std::string ReadShader()
{
    std::ifstream file(GTE_TEST_CLUSTER_CULL_SHADER_PATH, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

std::string UintConstant(const char* name, std::uint32_t value)
{
    return std::string("const uint ") + name + " = " + std::to_string(value) + "u;";
}

TEST(ClusterShaderText, FileIsReadable)
{
    EXPECT_FALSE(ReadShader().empty());
}

TEST(ClusterShaderText, WorkGroupSizeMatchesTheDispatchConstant)
{
    EXPECT_TRUE(Contains(ReadShader(), "layout(local_size_x = " + std::to_string(kClusterCullGroupSize) + ") in;"));
}

TEST(ClusterShaderText, FlagBitsEqualTheCppConstants)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, UintConstant("kClusterFlagUniformScale", kClusterFlagUniformScale)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterFlagPositiveHand", kClusterFlagPositiveHand)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterFlagMoved", kClusterFlagMoved)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagFrustum", kClusterCullFlagFrustum)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagBackfaceCone", kClusterCullFlagBackfaceCone)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagOcclusion", kClusterCullFlagOcclusion)));
    EXPECT_TRUE(Contains(text, UintConstant("kClusterCullFlagOrthographic", kClusterCullFlagOrthographic)));
}

TEST(ClusterShaderText, EveryBlockHasItsInstanceNameAndQualifier)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 0) readonly buffer ParamsBlock {"));
    EXPECT_TRUE(Contains(text, "} params;"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 1) readonly buffer InstancesBlock {"));
    EXPECT_TRUE(Contains(text, "} instances;"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 2) readonly buffer ClustersBlock {"));
    EXPECT_TRUE(Contains(text, "} clusters;"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 3) writeonly buffer VisibleBlock {"));
    EXPECT_TRUE(Contains(text, "} visible;"));
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 4) writeonly buffer CommandsBlock {"));
    EXPECT_TRUE(Contains(text, "} commands;"));
    // The counter takes atomics, so it has no readonly or writeonly qualifier.
    EXPECT_TRUE(Contains(text, "layout(std430, binding = 5) buffer CountBlock {"));
    EXPECT_TRUE(Contains(text, "} count;"));
}

TEST(ClusterShaderText, StructFieldsFollowTheCppRecords)
{
    const std::string text = ReadShader();
    // ClusterCullParams, in declaration order.
    const std::size_t planes = text.find("vec4 planes[6];");
    const std::size_t eye = text.find("vec4 eyeOrDirection;");
    const std::size_t prev = text.find("mat4 prevViewProjection;");
    const std::size_t counts = text.find("uvec4 counts;");
    const std::size_t hiz = text.find("vec4 hiz;");
    ASSERT_NE(planes, std::string::npos);
    ASSERT_NE(eye, std::string::npos);
    ASSERT_NE(prev, std::string::npos);
    ASSERT_NE(counts, std::string::npos);
    ASSERT_NE(hiz, std::string::npos);
    EXPECT_LT(planes, eye);
    EXPECT_LT(eye, prev);
    EXPECT_LT(prev, counts);
    EXPECT_LT(counts, hiz);

    // ClusterInstanceGpu: mat4 world + uvec4 info.
    EXPECT_TRUE(Contains(text, "mat4 world;"));
    EXPECT_TRUE(Contains(text, "uvec4 info;"));

    // MeshCluster: sphere, cone, aabbMin + firstIndex, aabbMax + indexCount.
    EXPECT_TRUE(Contains(text, "vec4 sphere;"));
    EXPECT_TRUE(Contains(text, "vec4 cone;"));
    EXPECT_TRUE(Contains(text, "vec3 aabbMin;"));
    EXPECT_TRUE(Contains(text, "uint firstIndex;"));
    EXPECT_TRUE(Contains(text, "vec3 aabbMax;"));
    EXPECT_TRUE(Contains(text, "uint indexCount;"));
}

TEST(ClusterShaderText, CommandsAreAFlatUintArrayOfFivePerSlot)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, "uint values[];"));
    EXPECT_TRUE(Contains(text, "slot * 5u"));
    EXPECT_EQ(kClusterCommandBytes, 5u * sizeof(std::uint32_t));
}

TEST(ClusterShaderText, HizConstantsAndBindingMatchTheCppOracle)
{
    const std::string text = ReadShader();
    EXPECT_TRUE(Contains(text, "layout(binding = 6) uniform sampler2D hizAtlas;"));
    EXPECT_TRUE(Contains(text, UintConstant("kHizMaxLevel", kHizMaxLevel)));
    EXPECT_TRUE(Contains(text, "const float kHizDepthBias = 0.0001;"));
    EXPECT_TRUE(Contains(text, "const float kHizRectMarginTexels = 1.0;"));
    EXPECT_FLOAT_EQ(kHizDepthBias, 0.0001f);
    EXPECT_FLOAT_EQ(kHizRectMarginTexels, 1.0f);
}

TEST(ClusterShaderText, OcclusionSkipsMovedInstances)
{
    EXPECT_TRUE(Contains(ReadShader(), "(instance.info.x & kClusterFlagMoved) == 0u"));
}

TEST(ClusterShaderText, HizBuildShaderPushBlockAndBindings)
{
    std::ifstream file(GTE_TEST_HIZ_BUILD_SHADER_PATH, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(text.empty());
    EXPECT_TRUE(Contains(text, "layout(local_size_x = 8, local_size_y = 8) in;"));
    EXPECT_TRUE(Contains(text, "layout(binding = 0) uniform sampler2D sceneDepth;"));
    EXPECT_TRUE(Contains(text, "layout(binding = 1, r32f) uniform writeonly image2D hizOutput;"));
    EXPECT_TRUE(Contains(text, "uvec4 levelInfo;"));
    EXPECT_TRUE(Contains(text, "uvec4 sizes;"));
}

} // namespace
} // namespace gte
