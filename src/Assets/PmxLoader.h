#pragma once

#include "MeshData.h"

#include <string>

namespace gte {

// Result of one LoadPmxModel() call below - always fully populated (mirrors
// AssetImportResult's own "always success + message, never a half-filled
// struct" convention - see src/Assets/AssetImporter.h).
struct PmxLoadResult {
    bool success = false;
    MeshData mesh;
    std::string message; // Human-readable status - always set, success or failure.
};

// Parses a MikuMikuDance .pmx model file at `filePath` (a plain filesystem
// path, UTF-8 encoded - matches every other path-taking function in this
// engine, e.g. AssetImporter.h's ImportAssetFile()) and extracts its
// per-vertex positions/normals/UVs plus triangle indices into a plain
// MeshData (src/Assets/MeshData.h) - this engine's own types, never a
// saba:: or glm:: type (see FetchSaba.cmake's header comment for why that
// boundary matters: only PmxLoader.cpp itself includes a saba/glm header).
// Uses saba::ReadPMXFile() (third_party/saba/src/Saba/Model/MMD/PMXFile.h)
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
