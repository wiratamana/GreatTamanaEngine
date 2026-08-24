#pragma once

#include "../Math/Vec2.h"
#include "../Math/Vec3.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Plain, engine-native mesh data extracted from an MMD (MikuMikuDance) .pmx
// model file - positions/normals/UVs/triangle indices only. saba's own
// PMXFile parser (see FetchSaba.cmake) also reads bones/morphs/materials/
// rigid bodies/joints, but none of that is surfaced here yet - this is
// deliberately scoped to exactly what's needed to get vertex positions/
// normals/UVs out of a .pmx file and onto the GPU; a future session adding
// real bone-deformed skinning/animation playback would extend PMXFile's
// curated build (see FetchSaba.cmake's header comment) and add a richer
// struct alongside this one, not grow this one indefinitely.
//
// Deliberately a plain data struct with no behavior of its own, same "ECS
// component"-style philosophy as the rest of this engine (see AGENTS.md,
// "Entity-Component-System") - easy to feed into a future
// Renderer::CreateMesh() call once a caller converts it into a GPU vertex
// layout that actually carries normal/UV attributes (today's gte::Vertex,
// see Vertex.h, is position+color only).
struct PmxMeshData {
    // Index-aligned: positions[i]/normals[i]/uvs[i] together describe vertex
    // i - always the exact same length as each other. Empty (along with
    // every other field) when LoadPmxModel() fails.
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;

    // Triangle list, 3 indices per face, indexing into positions/normals/uvs
    // above - directly usable as a GPU index buffer.
    std::vector<std::uint32_t> indices;
};

// Result of one LoadPmxModel() call below - always fully populated (mirrors
// AssetImportResult's own "always success + message, never a half-filled
// struct" convention - see src/Assets/AssetImporter.h).
struct PmxLoadResult {
    bool success = false;
    PmxMeshData mesh;
    std::string message; // Human-readable status - always set, success or failure.
};

// Parses a MikuMikuDance .pmx model file at `filePath` (a plain filesystem
// path, UTF-8 encoded - matches every other path-taking function in this
// engine, e.g. AssetImporter.h's ImportAssetFile()) and extracts its
// per-vertex positions/normals/UVs plus triangle indices into a plain
// PmxMeshData - this engine's own types, never a saba:: or glm:: type (see
// FetchSaba.cmake's header comment for why that boundary matters: only
// PmxLoader.cpp itself includes a saba/glm header). Uses
// saba::ReadPMXFile() (third_party/saba/src/Saba/Model/MMD/PMXFile.h)
// internally - the same "wrap a third-party format reader behind a small
// engine-native function" shape as Ktx2Decoder.h's DecodeKtx2ToRgba8()
// wrapping libktx. Never throws; a missing/corrupt/unreadable file yields
// success == false with a descriptive message and an empty mesh, exactly
// like ReadGtaFile()'s own failure contract (see GtaFile.h). No axis/
// winding remapping is performed - MMD's own coordinate convention may
// differ from this engine's (see src/Math/MathTypes.h); a future step
// wiring this into the render pipeline should account for that if a
// loaded model's orientation/winding looks wrong.
PmxLoadResult LoadPmxModel(const std::string& filePath);

} // namespace gte
