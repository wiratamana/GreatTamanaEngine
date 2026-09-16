#pragma once

// The FIFTH sanctioned cross-thread bridge - see AGENTS.md, "Networking",
// and task_manager/stl-parser-2/PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision #1/#9 for why this is its own, new, dedicated bridge type rather
// than a new EngineCommandKind/EditorUiCommandKind value bolted onto an
// existing one. Structurally IDENTICAL to EditorUiCommandBridge (single
// global slot, mutex + condition_variable) - mirrors that class's own
// SubmitAndWait()/IsCommandPending()/TryPeekPendingCommandRequest()/
// FulfillCommand() shape exactly.
//
// Lets a future network route handler (POST /import_asset - PHASE2) ask the
// main thread to import an external file into the Editor's "Project"
// folder, using the SAME live AssetDatabase instance ProjectPanel already
// owns - see IEditorLayer::ImportExternalAssetIntoProject()
// (src/Editor/EditorLayer.h) for the real work this bridge's request
// ultimately drives.
//
// Owned by Application (the composition root), constructed alongside the
// four existing bridges, BEFORE NetworkServer (so its address can be handed
// into NetworkServer's constructor) - see Application.h.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace gte {

enum class AssetImportCommandKind {
    ImportExternalFile,
};

// Plain request payload for one ImportExternalFile command.
struct ImportExternalFileCommand {
    std::string sourceAbsolutePath;
    // "" means "import directly into the Project root" - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #2.
    std::string destinationRelativeFolder;
};

struct AssetImportCommandRequest {
    AssetImportCommandKind kind = AssetImportCommandKind::ImportExternalFile;
    ImportExternalFileCommand importExternalFile;
};

// Outcome of one ImportExternalFile command - deliberately PLAIN SCALARS
// ONLY (no std::filesystem::path, no Guid type) so this header stays
// completely free of any src/Assets/ dependency, mirroring
// EditorLayer.h's own "TabActivationResult is a tiny, dependency-free
// mirror, never the real cross-layer type" precedent, applied one layer
// further down this same cross-thread boundary.
//
// - projectAvailable == false: the Editor's "Project" panel does not exist
//   in this build (GTE_ENABLE_EDITOR or GTE_ENABLE_PROJECT_PANEL is OFF) -
//   every other field is meaningless. NetworkServer.cpp (PHASE2) maps this
//   to HTTP 503.
// - projectAvailable == true, success == false: the import itself failed
//   (bad destinationRelativeFolder, source file missing/corrupt/unreadable,
//   etc) - `message` explains why. NetworkServer.cpp maps this to HTTP 400.
// - success == true: every field below is meaningful.
struct ImportExternalFileOutcome {
    bool projectAvailable = true;
    bool success = false;
    std::string message;

    std::string finalRelativePath; // relative to the Project root, forward slashes
    std::string finalAbsolutePath;
    std::string guid; // Guid::ToString() format, or "" if not applicable (e.g. a plain file copy)

    bool convertedToMeshAsset = false;
    std::string meshSourceFormat; // "stl" / "pmx" / "" (meaningful only when convertedToMeshAsset)
    bool convertedToKtx2 = false;
    bool convertedToMotionAsset = false;

    std::uint64_t meshVertexCount = 0;   // meaningful only when convertedToMeshAsset
    std::uint64_t meshTriangleCount = 0; // meaningful only when convertedToMeshAsset
};

struct AssetImportCommandResult {
    AssetImportCommandKind kind = AssetImportCommandKind::ImportExternalFile;
    ImportExternalFileOutcome importExternalFile;
};

class AssetImportCommandBridge {
public:
    AssetImportCommandBridge() = default;
    ~AssetImportCommandBridge() = default;

    AssetImportCommandBridge(const AssetImportCommandBridge&) = delete;
    AssetImportCommandBridge& operator=(const AssetImportCommandBridge&) = delete;

    // --- Called from the NETWORK thread (a route handler) only ----------

    struct SubmitResult {
        std::optional<AssetImportCommandResult> result;
        bool alreadyPending = false;
        bool timedOut = false;
    };
    // PHASE0's Locked Design Decision #6 - default timeout is 120000ms
    // (120 seconds), NOT the 3000ms every other bridge defaults to - a
    // large, real import (the reference terrain.stl) genuinely needs this
    // much headroom parsing synchronously on the main thread (Locked
    // Design Decision #1).
    SubmitResult SubmitAndWait(AssetImportCommandRequest request, int timeoutMilliseconds = 120000);

    // --- Called from the MAIN thread (Application::Run()) only ----------

    bool IsCommandPending() const;
    std::optional<AssetImportCommandRequest> TryPeekPendingCommandRequest() const;
    void FulfillCommand(AssetImportCommandResult result);

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_conditionVariable;
    bool m_requested = false;
    bool m_fulfilled = false;
    AssetImportCommandRequest m_request;
    AssetImportCommandResult m_result;
};

} // namespace gte
