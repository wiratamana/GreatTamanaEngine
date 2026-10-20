// Compares SceneLightingUniformData with the std140 block in DirectionalLighting.glsl,
// field by field, so a swapped or renamed member fails (a size check alone cannot see that).

#include "Features/SceneLighting/SceneLightingTypes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace gte {
namespace {

struct GlslMember {
    std::string name;
    std::size_t offset = 0;
};

struct Std140Block {
    std::vector<GlslMember> members;
    std::size_t size = 0;
};

std::size_t RoundUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

// Size and alignment of the scalar and vector types the block may use.
bool TypeLayout(const std::string& type, std::size_t& size, std::size_t& alignment)
{
    if (type == "float" || type == "int" || type == "uint") {
        size = 4;
        alignment = 4;
    } else if (type == "vec2") {
        size = 8;
        alignment = 8;
    } else if (type == "vec3") {
        size = 12;
        alignment = 16;
    } else if (type == "vec4") {
        size = 16;
        alignment = 16;
    } else if (type == "mat4") {
        size = 64;
        alignment = 16;
    } else {
        return false;
    }
    return true;
}

Std140Block ParseBlock(const std::string& source)
{
    Std140Block block;
    const std::size_t nameAt = source.find("SceneGlobalUniformBlock");
    EXPECT_NE(nameAt, std::string::npos);
    const std::size_t open = source.find('{', nameAt);
    const std::size_t close = source.find('}', open);
    EXPECT_TRUE(open != std::string::npos && close != std::string::npos);
    if (open == std::string::npos || close == std::string::npos) {
        return block;
    }

    // Drop line comments, then read one declaration per ';'.
    std::istringstream lines(source.substr(open + 1, close - open - 1));
    std::string line;
    std::string body;
    while (std::getline(lines, line)) {
        body += line.substr(0, line.find("//"));
        body += '\n';
    }

    std::istringstream declarations(body);
    std::string text;
    std::size_t cursor = 0;
    while (std::getline(declarations, text, ';')) {
        std::istringstream declaration(text);
        std::string type;
        std::string name;
        if (!(declaration >> type >> name)) {
            continue;
        }
        std::size_t size = 0;
        std::size_t alignment = 0;
        EXPECT_TRUE(TypeLayout(type, size, alignment)) << "unsupported type '" << type << "'";
        cursor = RoundUp(cursor, alignment);
        block.members.push_back({ name, cursor });
        cursor += size;
    }
    block.size = RoundUp(cursor, 16);
    return block;
}

std::string ReadShader()
{
    std::ifstream file(GTE_TEST_LIGHTING_GLSL_PATH, std::ios::binary);
    EXPECT_TRUE(file.good()) << "cannot read " << GTE_TEST_LIGHTING_GLSL_PATH;
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

TEST(SceneLightingUniformLayoutTest, EveryFieldMatchesTheStd140BlockInName_Order_AndOffset)
{
    const Std140Block block = ParseBlock(ReadShader());

    const std::vector<std::pair<const char*, std::size_t>> expected = {
        { "sunDirectionAndValid", offsetof(SceneLightingUniformData, sunDirectionAndValid) },
        { "sunIlluminance", offsetof(SceneLightingUniformData, sunIlluminance) },
        { "skyAmbientAndIntensity", offsetof(SceneLightingUniformData, skyAmbientAndIntensity) },
        { "groundAmbientAndExposure", offsetof(SceneLightingUniformData, groundAmbientAndExposure) },
    };

    ASSERT_EQ(block.members.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(block.members[i].name, expected[i].first) << "member " << i;
        EXPECT_EQ(block.members[i].offset, expected[i].second) << expected[i].first;
    }
    EXPECT_EQ(block.size, sizeof(SceneLightingUniformData));
}

TEST(SceneLightingUniformLayoutTest, ParserComputesStd140OffsetsForMixedTypes)
{
    const Std140Block block = ParseBlock("uniform SceneGlobalUniformBlock { float a; vec3 b; float c; vec2 d; mat4 e; } u;");
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
