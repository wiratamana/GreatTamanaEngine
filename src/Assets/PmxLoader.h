#pragma once

#include "MaterialData.h"
#include "MeshData.h"
#include "MorphData.h"
#include "PhysicsData.h"
#include "SkeletonData.h"

#include <string>

namespace gte {

// Result of one LoadPmxModel() call below - always fully populated (mirrors
// AssetImportResult's own "always success + message, never a half-filled
// struct" convention - see src/Assets/AssetImporter.h).
struct PmxLoadResult {
    bool success = false;

    // Vertex positions/normals/UVs/triangle indices, PLUS (since this
    // session) per-vertex bone skinning weights - mesh.skinWeights is always
    // populated 1:1 with mesh.positions (every PMX vertex record carries a
    // weight type + bone indices/weights unconditionally, covering all of
    // BDEF1/BDEF2/BDEF4/SDEF/QDEF), unlike a hypothetical future OBJ/glTF
    // importer that might leave it empty - see MeshData::skinWeights's own
    // documented "empty means no skinning info" contract (MeshData.h).
    MeshData mesh;

    // The model's bone hierarchy (see SkeletonData.h) - empty for a boneless
    // model. mesh.skinWeights' bone indices are indices into
    // skeleton.bones.
    SkeletonData skeleton;

    // The model's blend-shape/morph list (see MorphData.h) - empty if the
    // source .pmx defines none.
    MorphData morphs;

    // The model's ragdoll/jiggle-bone rigid body + joint setup (see
    // PhysicsData.h) - empty if the source .pmx defines none. This is DATA
    // only; no physics simulation happens anywhere in this engine yet (see
    // PhysicsData.h's own file comment).
    PhysicsData physics;

    // The model's material list + the (resolved-to-absolute-path) texture
    // files those materials reference (see MaterialData.h) - empty
    // `materials.materials` for a materialless .pmx (rare, but not
    // rejected). Enables Game::EnsureMeshAsset() (src/Game/Game.cpp) to
    // split an imported mesh's triangle-index list into one contiguous,
    // independently-texturable slice per material instead of always
    // drawing the whole thing as one untextured "grey clay" blob.
    MaterialData materials;

    std::string message; // Human-readable status - always set, success or failure.
};

// Parses a MikuMikuDance .pmx model file at `filePath` (a plain filesystem
// path, UTF-8 encoded - matches every other path-taking function in this
// engine, e.g. AssetImporter.h's ImportAssetFile()) and extracts its
// per-vertex positions/normals/UVs/skinning weights, triangle indices,
// bones, morphs, and rigid-body/joint physics setup into plain, engine-
// native structs (src/Assets/MeshData.h, SkeletonData.h, MorphData.h,
// PhysicsData.h) - this engine's own types, never a saba:: or glm:: type
// (see FetchSaba.cmake's header comment for why that boundary matters: only
// PmxLoader.cpp itself includes a saba/glm header). Uses
// saba::ReadPMXFile() (third_party/saba/src/Saba/Model/MMD/PMXFile.h)
// internally - the same "wrap a third-party format reader behind a small
// engine-native function" shape as Ktx2Decoder.h's DecodeKtx2ToRgba8()
// wrapping libktx. Never throws; a missing/corrupt/unreadable file yields
// success == false with a descriptive message and an otherwise-default-
// constructed (empty) result, exactly like ReadGtaFile()'s own failure
// contract (see GtaFile.h). No axis/winding remapping is performed - MMD's
// own coordinate convention may differ from this engine's (see
// src/Math/MathTypes.h); a future step wiring this into the render pipeline
// should account for that if a loaded model's orientation/winding looks
// wrong.
PmxLoadResult LoadPmxModel(const std::string& filePath);

} // namespace gte
