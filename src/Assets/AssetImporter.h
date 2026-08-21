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

    // Where the imported item actually ended up on disk - a *.gta path
    // when convertedToKtx2 is true, otherwise wherever `preferredDestinationPath`
    // (ImportAssetFile()'s own parameter) said to put it.
    std::filesystem::path finalPath;

    // Only meaningful when convertedToKtx2 is true - the fresh (or reused,
    // on re-import - see AssetDatabase::ImportAsset()) Guid this asset is
    // now tracked under. Guid::Invalid() otherwise.
    Guid guid;

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

// Imports `sourcePath` (a single FILE - never a directory; the caller is
// responsible for handling directory imports itself, e.g. a recursive
// filesystem copy) into the Project.
//
// The gating rule this function exists for: if `sourcePath`'s extension is
// one IsImportableAsKtx2Texture() recognizes, its pixels are decoded and
// re-encoded as a KTX2 container (Ktx2Encoder.h's EncodeImageFileToKtx2()),
// then wrapped as a *.gta (AssetType::Texture) at
// `preferredDestinationPath` with its extension replaced by ".gta", via
// `database.ImportAsset()` - this is what makes the engine actually "know
// about" the resulting asset immediately: it's already queryable through
// `database` (FindByGuid()/FindByPath()) by the time this function
// returns, with no separate RefreshFromDirectory() call needed. Every OTHER
// extension is imported completely unchanged - a plain byte-for-byte file
// copy to `preferredDestinationPath` exactly as given, no *.gta wrapping at
// all (see README.md: "text file can stay still for now" - this is what
// keeps that true for every non-image asset).
//
// If `sourcePath` merely LOOKS like a supported image by extension but
// fails to actually decode (corrupt/truncated/not really an image despite
// its extension), this falls back to a plain copy too (at
// `preferredDestinationPath`'s ORIGINAL extension, unchanged), rather than
// failing the whole import outright - exactly like every other degrade-
// gracefully failure mode already established in this Editor (see
// AGENTS.md, "Editor Module Structure"). Never throws.
AssetImportResult ImportAssetFile(AssetDatabase& database, const std::filesystem::path& sourcePath,
    const std::filesystem::path& preferredDestinationPath);

} // namespace gte
