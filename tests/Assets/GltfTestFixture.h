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

} // namespace gte::test
