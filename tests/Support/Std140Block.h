// Parser for a std140 uniform block declared in GLSL source: member names and byte offsets.
#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace gte::test_support {

struct GlslMember {
    std::string name;
    std::size_t offset = 0;
};

struct Std140Block {
    std::vector<GlslMember> members;
    std::size_t size = 0;
};

inline std::size_t RoundUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

// Size and alignment of the scalar and vector types a block may use.
inline bool TypeLayout(const std::string& type, std::size_t& size, std::size_t& alignment)
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

// Parses the block named `blockName` out of `source`.
inline Std140Block ParseBlock(const std::string& source, const std::string& blockName)
{
    Std140Block block;
    const std::size_t nameAt = source.find(blockName);
    EXPECT_NE(nameAt, std::string::npos) << "block '" << blockName << "' not found";
    if (nameAt == std::string::npos) {
        return block;
    }
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

inline std::string ReadTextFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.good()) << "cannot read " << path;
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

} // namespace gte::test_support
