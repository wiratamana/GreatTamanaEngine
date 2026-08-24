#pragma once

#include "AssetDatabase.h"

#include <filesystem>
#include <string>

namespace gte {

// The outcome of one ImportAssetFile() call below - always fully populated
// (never partially, and `message` is always set) whether the import
// succeeded or failed, so a caller (e.g. the Editor's "Project" panel) can
// show a single status line either way without its own branching logic.
struct AssetImportResult {
    bool success = false;

    // True if `sourcePath` was gated into the PNG/JPG -> KTX2 -> *.gta
    // pipeline (see ImportAssetFile()'s own doc comment below) - false if
    // it was imported as a plain, byte-for-byte, unmodified file copy
    // instead (every non-image extension, or an image extension that
    // failed to actually decode).
    bool convertedToKtx2 = false;

    // True if `sourcePath` was gated into the .pmx -> MeshData -> *.gta
    // (AssetType::Mesh) pipeline instead (see IsImportableAsMeshAsset()/
    // ImportAssetFile()'s own doc comment below). Mutually exclusive with
    // convertedToKtx2 - a given source file only ever matches ONE of the
    // two gating predicates.
    bool convertedToMeshAsset = false;

    // Where the imported item actually ended up on disk - a *.gta path
    // when convertedToKtx2 or convertedToMeshAsset is true, otherwise
    // wherever `preferredDestinationPath` (ImportAssetFile()'s own
    // parameter) said to put it.
    std::filesystem::path finalPath;

    // Only meaningful when convertedToKtx2 or convertedToMeshAsset is true -
    // the fresh (or reused, on re-import - see AssetDatabase::ImportAsset())
    // Guid this asset is now tracked under. Guid::Invalid() otherwise.
    Guid guid;

    // Only meaningful when convertedToMeshAsset is true - the mesh's raw
    // vertex/triangle counts, for a caller that wants to report them (e.g.
    // the Editor's "Project" panel import status line) without re-reading
    // and re-decoding the *.gta payload it just wrote.
    std::size_t meshVertexCount = 0;
    std::size_t meshTriangleCount = 0;

    // Only meaningful when convertedToMeshAsset is true - counts of the
    // rig data (see RigFile.h) bundled into the same *.gta's metadata
    // section alongside the mesh payload: how many bones/morphs/rigid
    // bodies/joints the source .pmx actually defined (all zero for a
    // boneless/riggless static mesh - that's a normal, successful import,
    // not a failure). skinnedVertexCount is always == meshVertexCount for
    // a PMX import (see PmxLoader.h's PmxLoadResult::mesh doc comment) -
    // reported separately anyway so a caller never needs to assume that.
    std::size_t skinnedVertexCount = 0;
    std::size_t boneCount = 0;
    std::size_t morphCount = 0;
    std::size_t rigidBodyCount = 0;
    std::size_t jointCount = 0;

    std::string message; // Human-readable status - always set, success or failure.
};

// True if `extensionLowercaseWithDot` names a source image format this
// engine's import pipeline knows how to decode+re-encode as KTX2 (see
// Ktx2Encoder.h) - a plain, pure predicate, Tier-1-testable, and the single
// place the "which formats get gated into KTX2" policy lives, so nothing
// else ever needs to hand-roll its own extension allow-list. Matches
// stb_image's own documented supported formats, same list as
// src/Editor/AssetInspectorData.h's IsSupportedImageExtension() -
// duplicated rather than shared, since src/Assets/ (an always-compiled,
// engine-level module) must never depend on src/Editor/ (see AGENTS.md,
// "Coding Guidelines": only Application is allowed to reach across a
// similar layering boundary for SDL - the same "lower layer never depends
// on a higher one" rule applies here).
bool IsImportableAsKtx2Texture(const std::string& extensionLowercaseWithDot);

// True if `extensionLowercaseWithDot` names a source 3D model format this
// engine's import pipeline knows how to parse into a MeshData (see
// MeshData.h) - today just MikuMikuDance's ".pmx" (via PmxLoader.h/saba's
// PMXFile reader - see FetchSaba.cmake). A future OBJ/glTF importer would
// extend this same predicate (and ImportAssetFile()'s matching branch
// below) rather than inventing a separate gating function - this is the
// mesh-import equivalent of IsImportableAsKtx2Texture() above, same
// Tier-1-testable pure-predicate shape.
bool IsImportableAsMeshAsset(const std::string& extensionLowercaseWithDot);

// Imports `sourcePath` (a single FILE - never a directory; the caller is
// responsible for handling directory imports itself, e.g. a recursive
// filesystem copy) into the Project.
//
// The gating rules this function exists for, checked in order:
//   1. If `sourcePath`'s extension is one IsImportableAsMeshAsset()
//      recognizes, it's parsed into a MeshData (PmxLoader.h today),
//      serialized via MeshFile.h's EncodeMeshDataToBytes(), and wrapped as a
//      *.gta (AssetType::Mesh) at `preferredDestinationPath` with its
//      extension replaced by ".gta", via `database.ImportAsset()`. The same
//      PmxLoader.h call also extracts per-vertex skinning weights plus the
//      model's bones/morphs/rigid-body-and-joint physics setup (see
//      SkeletonData.h/MorphData.h/PhysicsData.h) - RigFile.h's
//      EncodeRigDataToBytes() serializes ALL of that into the *.gta's
//      METADATA section (see GtaFile.h), alongside the unchanged MeshFile.h
//      payload in the same file. A boneless/riggless .pmx still imports
//      successfully; its rig section simply encodes as all-empty.
//   2. Otherwise, if it's one IsImportableAsKtx2Texture() recognizes, its
//      pixels are decoded and re-encoded as a KTX2 container (Ktx2Encoder.h's
//      EncodeImageFileToKtx2()), then wrapped as a *.gta (AssetType::Texture)
//      the same way.
// Either branch is what makes the engine actually "know about" the
// resulting asset immediately: it's already queryable through `database`
// (FindByGuid()/FindByPath()) by the time this function returns, with no
// separate RefreshFromDirectory() call needed. Every OTHER extension is
// imported completely unchanged - a plain byte-for-byte file copy to
// `preferredDestinationPath` exactly as given, no *.gta wrapping at all (see
// README.md: "text file can stay still for now" - this is what keeps that
// true for every non-mesh, non-image asset).
//
// If `sourcePath` merely LOOKS like a supported mesh/image by extension but
// fails to actually parse/decode (corrupt/truncated/not really that format
// despite its extension), this falls back to a plain copy too (at
// `preferredDestinationPath`'s ORIGINAL extension, unchanged), rather than
// failing the whole import outright - exactly like every other degrade-
// gracefully failure mode already established in this Editor (see
// AGENTS.md, "Editor Module Structure"). Never throws.
AssetImportResult ImportAssetFile(AssetDatabase& database, const std::filesystem::path& sourcePath,
    const std::filesystem::path& preferredDestinationPath);

} // namespace gte
