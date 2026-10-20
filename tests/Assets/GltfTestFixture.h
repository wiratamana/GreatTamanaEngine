#pragma once

// Writes a one-triangle external-file .gltf (plus its .bin) for loader and importer tests.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace gte::test {

// Triangle (0,0,0) (1,0,0) (0,1,0), normals +Z, uint16 indices, node scale 2.
// `extraTopLevelJson` is spliced into the root object and must start with a comma
// (for example `,"materials":[...]`). `primitiveHasMaterial` points the primitive at material 0.
inline std::filesystem::path WriteGltfTriangle(const std::filesystem::path& directory,
    std::string_view extraTopLevelJson = {}, bool primitiveHasMaterial = false)
{
    std::vector<std::uint8_t> bin(80, 0);
    const float floats[18] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
    std::memcpy(bin.data(), floats, sizeof(floats));
    const std::uint16_t indices[3] = { 0, 1, 2 };
    std::memcpy(bin.data() + 72, indices, sizeof(indices));
    std::ofstream(directory / "tri.bin", std::ios::binary)
        .write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));

    const std::string primitiveMaterial = primitiveHasMaterial ? R"(,"material":0)" : "";
    const std::string gltf = R"({"asset":{"version":"2.0"},
      "buffers":[{"uri":"tri.bin","byteLength":80}],
      "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},
                     {"buffer":0,"byteOffset":36,"byteLength":36},
                     {"buffer":0,"byteOffset":72,"byteLength":6}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
                   {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
                   {"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}],
      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2)" + primitiveMaterial + R"(}]}],
      "nodes":[{"mesh":0,"scale":[2,2,2]}],
      "scenes":[{"nodes":[0]}],"scene":0)" + std::string(extraTopLevelJson) + "}";

    const std::filesystem::path path = directory / "tri.gltf";
    std::ofstream(path, std::ios::binary).write(gltf.data(), static_cast<std::streamsize>(gltf.size()));
    return path;
}

// A material that uses image "white.png" as base color and normal map, alpha MASK with cutoff 0.25.
inline constexpr std::string_view kMaskedTexturedMaterialJson
    = R"(,"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},
          "normalTexture":{"index":0},"alphaMode":"MASK","alphaCutoff":0.25}],
         "textures":[{"source":0}],"images":[{"uri":"white.png"}])";

// Knobs of WriteGltfQuad().
struct QuadOptions {
    std::string_view nodeJson = R"("scale":[1,1,1])";  // Members of the node object (no braces).
    bool withTangents = false;                         // Adds a TANGENT attribute with `tangent` on every vertex.
    float tangent[4] = { 1, 0, 0, 1 };                 // glTF space.
    bool mirrorSecondTriangleUv = false;               // Second triangle gets uvs of the opposite orientation.
    int primitiveCount = 1;                            // Every primitive reuses the same accessors.
};

// A flat quad in the XY plane: positions (0,0) (1,0) (1,1) (0,1), normals +Z, v pointing down the image,
// two triangles (0,1,2) and (0,2,3). With the default uvs the authored tangent is (1,0,0,+1).
inline std::filesystem::path WriteGltfQuad(const std::filesystem::path& directory, const QuadOptions& options = {})
{
    const float positions[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
    const float normals[12] = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
    const float lastUv = options.mirrorSecondTriangleUv ? 1.0f : 0.0f;
    const float uvs[8] = { 0, 1, 1, 1, 1, 0, lastUv, lastUv };
    float tangents[16];
    for (int i = 0; i < 4; ++i) {
        std::memcpy(tangents + i * 4, options.tangent, sizeof(options.tangent));
    }
    const std::uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };

    std::vector<std::uint8_t> bin;
    const auto append = [&bin](const void* data, std::size_t size) {
        const std::size_t offset = bin.size();
        bin.resize(offset + size);
        std::memcpy(bin.data() + offset, data, size);
        return offset;
    };
    const std::size_t positionOffset = append(positions, sizeof(positions));
    const std::size_t normalOffset = append(normals, sizeof(normals));
    const std::size_t uvOffset = append(uvs, sizeof(uvs));
    const std::size_t tangentOffset = append(tangents, sizeof(tangents));
    const std::size_t indexOffset = append(indices, sizeof(indices));
    bin.resize((bin.size() + 3) / 4 * 4);
    std::ofstream(directory / "quad.bin", std::ios::binary)
        .write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));

    const auto view = [](std::size_t offset, std::size_t length) {
        return R"({"buffer":0,"byteOffset":)" + std::to_string(offset) + R"(,"byteLength":)" + std::to_string(length) + "}";
    };
    std::string primitives;
    for (int i = 0; i < options.primitiveCount; ++i) {
        primitives += std::string(i > 0 ? "," : "") + R"({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2)"
            + (options.withTangents ? R"(,"TANGENT":3)" : "") + R"(},"indices":4})";
    }
    const std::string gltf = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"quad.bin","byteLength":)"
        + std::to_string(bin.size()) + R"(}],"bufferViews":[)" + view(positionOffset, sizeof(positions)) + ","
        + view(normalOffset, sizeof(normals)) + "," + view(uvOffset, sizeof(uvs)) + ","
        + view(tangentOffset, sizeof(tangents)) + "," + view(indexOffset, sizeof(indices)) + R"(],
      "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
                   {"bufferView":1,"componentType":5126,"count":4,"type":"VEC3"},
                   {"bufferView":2,"componentType":5126,"count":4,"type":"VEC2"},
                   {"bufferView":3,"componentType":5126,"count":4,"type":"VEC4"},
                   {"bufferView":4,"componentType":5123,"count":6,"type":"SCALAR"}],
      "meshes":[{"primitives":[)" + primitives + R"(]}],
      "nodes":[{"mesh":0,)" + std::string(options.nodeJson) + R"(}],
      "scenes":[{"nodes":[0]}],"scene":0})";

    const std::filesystem::path path = directory / "quad.gltf";
    std::ofstream(path, std::ios::binary).write(gltf.data(), static_cast<std::streamsize>(gltf.size()));
    return path;
}

} // namespace gte::test
