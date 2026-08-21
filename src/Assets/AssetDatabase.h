#pragma once

#include "AssetTypes.h"
#include "GtaFile.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

// One tracked *.gta asset, as indexed by AssetDatabase - plain data, rebuilt
// fresh by RefreshFromDirectory() below (same "rebuilt from disk, never a
// long-lived filesystem handle" philosophy as ProjectEntry - see
// ProjectPanelData.h) rather than something callers hold onto across a
// rescan.
struct AssetRecord {
    Guid guid;
    AssetType type = AssetType::Unknown;
    AssetFlags flags = AssetFlags::None;
    std::uint64_t version = 0;
    std::string gtaPath; // Absolute path to the *.gta file itself, UTF-8 (see ProjectPanelData::PathToUtf8()).
    std::uintmax_t fileSizeBytes = 0; // Whole *.gta file's size on disk (header + metadata + payload).
};

// The engine-wide, in-memory registry of every tracked *.gta asset - the
// "AssetDatabase" the top-level task asked for, similar in spirit to
// Unity's AssetDatabase (path <-> stable id lookup, load-by-id) but
// simpler: since every asset's Guid already lives INSIDE its own *.gta
// file's header (see GtaFile.h), there is no separate ".meta" sidecar file
// that can ever drift out of sync with the asset it describes - re-scanning
// a directory tree of *.gta files is enough to fully reconstruct the
// guid<->path index from scratch at any time.
//
// This class only knows about *.gta files - it has no opinion on what
// produced them (a future PNG->KTX2 import pipeline, a hand-authored scene
// file, ...) and no GPU/ImGui/Renderer dependency at all, so it stays
// Tier-1-testable exactly like ProjectPanelData (see AGENTS.md, "Testability
// & Regression Safety").
class AssetDatabase {
public:
    // Drops every currently-tracked record. RefreshFromDirectory() below
    // calls this itself before rescanning - exposed separately for a caller
    // that wants to reset without immediately rescanning.
    void Clear();

    // Recursively scans `rootDirectory` for *.gta files (case-insensitive
    // extension match) and (re)builds the entire in-memory index from what
    // it finds - a full rebuild, not an incremental patch, the same
    // "rebuilt from disk each time" convention ScanProjectDirectory() (see
    // ProjectPanelData.h) already uses, so anything deleted/moved
    // externally between scans is simply no longer present afterwards.
    // Reads ONLY each file's 64-byte header (via ReadGtaHeader(), never the
    // full ReadGtaFile()) - safe/cheap even over a directory containing
    // huge texture/mesh payloads. A *.gta file that fails to parse (bad
    // magic, truncated) is silently skipped, not fatal to the scan. If two
    // files share the same Guid (e.g. one was copy-pasted on disk), the
    // first one encountered wins and the rest are skipped - never throws.
    // Returns the number of valid assets now tracked (== GetAllAssets().size()).
    std::size_t RefreshFromDirectory(const std::filesystem::path& rootDirectory);

    // Writes a brand-new *.gta file at `destinationGtaPath` (header sections
    // built from `type`/`flags`/`metadata`/`payload`), assigns it a fresh
    // Guid (via Guid::Generate() - UNLESS a valid *.gta already exists at
    // that exact path, in which case its existing Guid is reused, so
    // re-importing/overwriting an asset in place never breaks an existing
    // scene's cross-reference to it), and registers/updates it in this
    // database's in-memory index immediately (no separate
    // RefreshFromDirectory() call needed to see it). Returns the asset's
    // Guid, or std::nullopt if the *.gta file itself couldn't be written.
    std::optional<Guid> ImportAsset(const std::filesystem::path& destinationGtaPath, AssetType type,
        const std::vector<std::uint8_t>& metadata, const std::vector<std::uint8_t>& payload,
        AssetFlags flags = AssetFlags::None);

    // Convenience wrapper around ImportAsset() for the common "wrap an
    // existing on-disk source file's raw bytes as this asset's payload"
    // case - reads `sourceFilePath` in full and passes its bytes straight
    // through as the payload, with no metadata. This is a deliberate
    // PASSTHROUGH placeholder for today (see README.md's "future notes":
    // an imported PNG/JPEG is meant to eventually be re-encoded to KTX2
    // before ending up here) - swapping in real image conversion later only
    // ever changes what bytes get passed to ImportAsset() above, not this
    // class's own API shape. Returns std::nullopt if `sourceFilePath`
    // can't be read, or if the resulting ImportAsset() call fails.
    std::optional<Guid> ImportRawFile(const std::filesystem::path& sourceFilePath,
        const std::filesystem::path& destinationGtaPath, AssetType type, AssetFlags flags = AssetFlags::None);

    // Returns nullptr if `guid` isn't currently tracked. The returned
    // pointer is only valid until the next Clear()/RefreshFromDirectory()/
    // ImportAsset() call - callers that need to keep a result around should
    // copy the AssetRecord itself, not hold the pointer.
    const AssetRecord* FindByGuid(const Guid& guid) const;

    // Returns nullptr if `gtaPath` isn't currently tracked. `gtaPath` is
    // compared as given (already-normalized absolute paths are expected -
    // see AssetRecord::gtaPath) - same pointer-lifetime caveat as
    // FindByGuid() above.
    const AssetRecord* FindByPath(const std::filesystem::path& gtaPath) const;

    // Every currently-tracked asset whose type is exactly `type`.
    std::vector<AssetRecord> GetAssetsOfType(AssetType type) const;

    const std::vector<AssetRecord>& GetAllAssets() const noexcept { return m_assets; }
    std::size_t Count() const noexcept { return m_assets.size(); }

private:
    void UpsertRecord(AssetRecord record);

    std::vector<AssetRecord> m_assets;
    std::unordered_map<Guid, std::size_t> m_guidToIndex;
    std::unordered_map<std::string, std::size_t> m_pathToIndex;
};

} // namespace gte
