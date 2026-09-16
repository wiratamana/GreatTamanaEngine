# PHASE1_ASSET_IMPORT_COMMAND_BRIDGE_AND_EDITOR_WIRING.md

**Campaign:** `stl-parser-2` (parent: `PHASE0_MASTER_STRATEGY.md` — READ FIRST)
**Branch:** `feature/stl-parser-impl`
**Depends on:** nothing new (builds on `stl-parser-1`'s already-merged
`AssetImporter`/`StlLoader`).
**Feeds into:** `PHASE2` (the actual `POST /import_asset` HTTP route — out of
scope for this phase entirely).

This is this campaign's heaviest, most cross-cutting phase — it touches SIX
different layers (`Application`, `Network` (`NetworkServer.h`/`.cpp` — see
3.6 below, NOT just header-only forward-declares), `EditorLayer.h`'s
interface, BOTH concrete `IEditorLayer` implementations, and `ProjectPanel`).
Per `PHASE0`'s own Delegation Flow, this phase gets its own dedicated
double-check pass before the campaign's general double-check. **This
document has already been through that pass** (see the "Double-Check Pass"
note at the very end) — every concrete claim below has been verified
directly against the real source files it references.

## Step 1: The Goal (Where are we going?)

A brand-new, `EditorUiCommandBridge`-shaped bridge
(`AssetImportCommandBridge`) that lets a FUTURE network route handler (built
in `PHASE2`) ask the main thread to import an external file into the
Editor's "Project" folder — using the SAME live `AssetDatabase` instance
`ProjectPanel` already owns, so the Project panel's own UI reflects the
import immediately, with no separate rescan needed. By the end of this
phase, `Application`'s per-frame loop can already fully service a manually-
constructed `AssetImportCommandRequest` (e.g. from a unit test standing in
for the network thread) end to end — the only thing genuinely missing after
this phase is the HTTP route itself (`PHASE2`).

## Step 2: The Situation (Where are we now?)

- `src/Application/EditorUiCommandBridge.h`/`.cpp` is the exact template
  this phase copies the SHAPE of — a single-global-slot, mutex + condition-
  variable request/result bridge with `SubmitAndWait()` (network thread),
  `IsCommandPending()`/`TryPeekPendingCommandRequest()`/`FulfillCommand()`
  (main thread). Read both files in full before writing anything — the new
  bridge's `.cpp` should be near-line-for-line structurally identical (only
  the request/result payload types differ).
- `src/Editor/EditorLayer.h`'s `IEditorLayer::ActivateTab()` (plus its own
  `TabActivationResult` — a tiny, dependency-free plain struct declared
  right above the interface) is the exact template for the new virtual
  method this phase adds. Read `ImGuiEditorLayer.cpp`'s own `ActivateTab()`
  override (`ImGui::SetCurrentContext(m_context); ... return result;`) and
  `NullEditorLayer.cpp`'s own one-line stub override.
- `src/Editor/Panels/ProjectPanel.h`/`.cpp`'s `HandleExternalFileDrop()` is
  the exact template for the new `ProjectPanel::ImportExternalFile()`
  method this phase adds — same `AssetImporter::ImportAssetFile()` call,
  same `MakeUniqueDestinationPath()` collision handling, same
  `m_needsRescan = true` on success — just driven by an explicit
  caller-supplied absolute source path + relative destination folder
  instead of screen-coordinate hit-testing against a drag-and-drop event.
- `src/Application/Application.h`'s **FOUR** existing bridge members
  (`m_captureBridge`/`m_commandBridge`/`m_uiCommandBridge`/
  `m_frameDebuggerCommandBridge`, all declared right before
  `m_networkServer`) and `src/Network/NetworkServer.h`'s constructor
  (`explicit NetworkServer(FrameCaptureBridge* = nullptr,
  EngineCommandBridge* = nullptr, EditorUiCommandBridge* = nullptr,
  FrameDebuggerCommandBridge* = nullptr)`, each pointer forward-declared,
  non-owning, defaulted to `nullptr`) show EXACTLY the pattern a FIFTH
  bridge must follow. Unlike a purely-`Application`-side addition, this
  fifth bridge's own pointer must ALSO become a genuine new (defaulted)
  fifth parameter/member on `NetworkServer` itself THIS PHASE — see 3.6
  below — even though no route touches it until `PHASE2`; the alternative
  (deferring the `NetworkServer` change to `PHASE2`) would silently break
  `Application.cpp`'s own constructor call the moment it tries to pass a
  fifth pointer into a still-four-parameter constructor.

## Step 3: The Plan

### 3.1 — New file: `src/Application/AssetImportCommandBridge.h`

Mirror `EditorUiCommandBridge.h` structurally. Contents:

```cpp
#pragma once

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

// The FIFTH sanctioned cross-thread bridge - see AGENTS.md, "Networking",
// and PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1/#9 for why this
// is its own, new, dedicated bridge type rather than a new EngineCommandKind/
// EditorUiCommandKind value bolted onto an existing one. Structurally
// IDENTICAL to EditorUiCommandBridge (single global slot, mutex +
// condition_variable) - see that class's own .h/.cpp for the exact shape
// this class's own .cpp must copy.
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
```

### 3.2 — New file: `src/Application/AssetImportCommandBridge.cpp`

Copy `EditorUiCommandBridge.cpp`'s implementation verbatim, substituting the
new type names (`AssetImportCommandBridge`/`AssetImportCommandRequest`/
`AssetImportCommandResult`). Every method's actual body (the `SubmitAndWait`
already-pending check, the `wait_for` + `m_fulfilled` predicate, the
timed-out reset, `IsCommandPending`, `TryPeekPendingCommandRequest`,
`FulfillCommand`'s "nobody is waiting → inert no-op" check) is IDENTICAL
logic — only the default `timeoutMilliseconds` value (120000, per 3.1
above) differs from the source file being copied.

### 3.3 — `src/Editor/EditorLayer.h` changes

Add a new, tiny, dependency-free result struct right next to
`TabActivationResult` (same file, same style — plain scalars only, no
`std::filesystem::path`, no `Guid`):

```cpp
// task_manager/stl-parser-2, PHASE1 - result of ImportExternalAssetIntoProject()
// below. Deliberately a SEPARATE, tiny, dependency-free type from
// src/Application/AssetImportCommandBridge.h's own ImportExternalFileOutcome
// (network-impl-7's own TabActivationResult/ActivateTabOutcome split
// precedent, applied here) - EditorLayer.h must never depend on anything
// under src/Application/. Application::Run() (Phase 3.7 below) is the one
// place that converts one of these into the OTHER type, one field at a
// time.
struct ProjectAssetImportResult {
    bool projectAvailable = true;
    bool success = false;
    std::string message;
    std::string finalRelativePath;
    std::string finalAbsolutePath;
    std::string guid;
    bool convertedToMeshAsset = false;
    std::string meshSourceFormat;
    bool convertedToKtx2 = false;
    bool convertedToMotionAsset = false;
    std::uint64_t meshVertexCount = 0;
    std::uint64_t meshTriangleCount = 0;
};
```

(Add `#include <cstdint>` to this file's existing include block — confirmed
NOT already present today: `EditorLayer.h`'s current includes are
`Math/Mat4.h`, `Math/Vec3.h`, `Renderer/Atmosphere/AtmosphereTypes.h`,
`Renderer/RenderTexture.h`, `Renderer/RenderGraph/RenderGraphTypes.h`,
`<memory>`, `<optional>`, `<string>` — nothing here pulls in `<cstdint>`
transitively, and `TabActivationResult`/`FrameDebuggerStateSnapshotView`
(the only other plain result structs in this file) only ever needed
`bool`/`int`/`float`/`std::string`, never a fixed-width integer type.)

Add a new pure virtual method to `IEditorLayer`, placed right after
`ActivateTab()`:

```cpp
// task_manager/stl-parser-2, PHASE1 - imports a single external file
// (anywhere on disk, `sourceAbsolutePath`) into the Editor's "Project"
// folder, at `destinationRelativeFolder` (a path relative to the Project
// root - "" means the Project root itself; auto-created if missing) -
// the programmatic equivalent of dragging a file onto the "Project" panel
// (see Panels/ProjectPanel.h's own HandleExternalFileDrop()). Routes
// through the EXACT SAME AssetImporter::ImportAssetFile() pipeline and the
// SAME live AssetDatabase instance ProjectPanel already owns, so the
// Project panel's own tree/selection reflect this import on its very next
// rendered frame, with no separate rescan needed from the caller. Returns
// projectAvailable == false (every other field meaningless) when this
// build has no "Project" panel at all - always true for NullEditorLayer,
// and for the real ImGuiEditorLayer impl only when GTE_ENABLE_PROJECT_PANEL
// is OFF (a real Editor build with the Project panel compiled out).
virtual ProjectAssetImportResult ImportExternalAssetIntoProject(
    const std::string& sourceAbsolutePath, const std::string& destinationRelativeFolder) = 0;
```

### 3.4 — `src/Editor/NullEditorLayer.cpp` changes

Add the stub override, right after the existing `ActivateTab()` stub:

```cpp
ProjectAssetImportResult ImportExternalAssetIntoProject(
    const std::string& /*sourceAbsolutePath*/, const std::string& /*destinationRelativeFolder*/) override
{
    ProjectAssetImportResult result;
    result.projectAvailable = false;
    return result;
}
```

### 3.5 — `src/Editor/Panels/ProjectPanel.h`/`.cpp` changes

**Confirmed (read in full):** `ProjectPanel.h` today includes only
`../ProjectPanelData.h` and `../../Assets/AssetDatabase.h` — it does **NOT**
already include `../../Assets/AssetImporter.h`, so it has no visibility of
`AssetImportResult` at all yet. Add that include unconditionally (not an
"if not already reachable" hedge — it is confirmed absent):

```cpp
#include "../../Assets/AssetImporter.h"
```

Add a new PUBLIC method declaration to `ProjectPanel.h`, right after
`HandleExternalFileDrop()`'s own declaration:

```cpp
// task_manager/stl-parser-2, PHASE1 - the programmatic equivalent of
// HandleExternalFileDrop() above, driven by an explicit absolute source
// path + relative destination folder instead of a screen-coordinate OS
// drop event - see IEditorLayer::ImportExternalAssetIntoProject()'s own
// doc comment (EditorLayer.h) for the full contract this implements.
// `destinationRelativeFolder` is REJECTED (a failure AssetImportResult with
// success == false, no filesystem operation performed at all) if it is an
// absolute path, a Windows drive/root-relative path, or if its own
// std::filesystem::path::lexically_normal() form starts with a ".."
// component - see this method's own .cpp comment for the exact algorithm
// (PHASE0_MASTER_STRATEGY.md's Risk Register / Locked Design Decision #8).
AssetImportResult ImportExternalFile(
    const std::filesystem::path& sourceAbsolutePath, const std::string& destinationRelativeFolder);
```

Also add a new PUBLIC read-only accessor right after `GetAssetDatabase()`
(needed by 3.8 below — `ImGuiEditorLayer` has no other way to learn the
Project root's own absolute path, and `m_rootPath` is private):

```cpp
// task_manager/stl-parser-2, PHASE1 - the Project root's own absolute
// path, needed by ImGuiEditorLayer::ImportExternalAssetIntoProject() to
// turn ImportExternalFile()'s own AssetImportResult::finalPath (always an
// ABSOLUTE path) into a path relative to "Project" for
// ProjectAssetImportResult::finalRelativePath - mirrors GetAssetDatabase()'s
// own "read-only accessor for a private member" shape immediately above.
const std::filesystem::path& GetRootPath() const noexcept { return m_rootPath; }
```

Implement `ImportExternalFile()` in `ProjectPanel.cpp`, placed right after
`HandleExternalFileDrop()`:

```cpp
AssetImportResult ProjectPanel::ImportExternalFile(
    const std::filesystem::path& sourceAbsolutePath, const std::string& destinationRelativeFolder)
{
    AssetImportResult result;

    // Confirmed (read in full): EnsureRootAndMaybeRescan() is already
    // idempotent and throttled (500ms, via m_lastScanTime/m_needsRescan) -
    // calling it again here, on top of Build()'s own once-per-frame call,
    // is always safe and never double-scans within the same throttle
    // window.
    EnsureRootAndMaybeRescan();
    if (!m_rootExists) {
        result.success = false;
        result.message = "Cannot import - the \"Project\" folder is missing.";
        return result;
    }

    std::error_code existsEc;
    if (!std::filesystem::exists(sourceAbsolutePath, existsEc) || existsEc) {
        result.success = false;
        result.message = "The source file does not exist: \"" + PathToUtf8(sourceAbsolutePath) + "\".";
        return result;
    }
    std::error_code isDirEc;
    if (std::filesystem::is_directory(sourceAbsolutePath, isDirEc) && !isDirEc) {
        result.success = false;
        result.message = "The source path is a directory, not a file - only single-file imports are supported.";
        return result;
    }

    // Path-traversal hardening - PHASE0_MASTER_STRATEGY.md's Risk Register /
    // Locked Design Decision #8. An empty destinationRelativeFolder means
    // "the Project root itself", exactly like m_currentFolderRelativePath's
    // own "" convention elsewhere in this class.
    std::filesystem::path relFolder = Utf8ToPath(destinationRelativeFolder);
    // Rejects BOTH a fully-qualified absolute path (e.g. "C:\Windows") AND a
    // Windows DRIVE-RELATIVE or ROOT-RELATIVE path (e.g. "C:Temp" -
    // relative to whatever the current directory on the C: drive happens
    // to be, or "\Escape" - relative to the current drive's own root).
    // is_absolute() alone returns FALSE for both of those on Windows (it
    // requires BOTH a root name AND a root directory together) -
    // has_root_path() is true if EITHER one is present on its own, which is
    // what actually matters here: "does this path carry any notion of a
    // drive/root at all that could resolve outside the Project folder".
    if (relFolder.is_absolute() || relFolder.has_root_path()) {
        result.success = false;
        result.message = "destination_folder must be a plain path relative to the Project root, not absolute "
                          "or drive/root-relative.";
        return result;
    }
    const std::filesystem::path normalizedRelFolder = relFolder.lexically_normal();
    if (!normalizedRelFolder.empty() && normalizedRelFolder.begin()->wstring() == L"..") {
        result.success = false;
        result.message = "destination_folder must not escape the Project root (no \"..\" segments).";
        return result;
    }

    const std::filesystem::path targetDir = (m_rootPath / normalizedRelFolder).lexically_normal();

    std::error_code dirEc;
    std::filesystem::create_directories(targetDir, dirEc); // best-effort; MakeUniqueDestinationPath()/ImportAssetFile() below are the real check.

    // Same "*.gta"-collision-aware destination naming HandleExternalFileDrop()
    // already uses (see that method's own comment on why this matters for
    // an already-imported "name.gta" existing before "name.png"/"name.pmx"
    // is re-dropped) - EXTENDED here to also cover IsImportableAsMotionAsset()
    // (".vmd"), which HandleExternalFileDrop() itself does not currently
    // check (a narrow, pre-existing, out-of-scope inconsistency in that
    // already-merged stl-parser-1-era method - NOT something this phase
    // fixes there - but there is no reason to knowingly copy it into this
    // brand-new method too).
    std::filesystem::path desiredName = sourceAbsolutePath.filename();
    std::string extension = PathToUtf8(sourceAbsolutePath.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (IsImportableAsKtx2Texture(extension) || IsImportableAsMeshAsset(extension)
        || IsImportableAsMotionAsset(extension)) {
        desiredName.replace_extension(".gta");
    }
    const std::filesystem::path destination = MakeUniqueDestinationPath(targetDir, desiredName);

    result = ImportAssetFile(m_assetDatabase, sourceAbsolutePath, destination);
    if (result.success) {
        m_needsRescan = true;
    }
    return result;
}
```

**Confirmed helper signatures (read in full — no further verification
needed before implementing):**
- `PathToUtf8(const std::filesystem::path&) -> std::string`,
  `Utf8ToPath(const std::string&) -> std::filesystem::path`, and
  `MakeUniqueDestinationPath(const std::filesystem::path& destinationDir, const std::filesystem::path& desiredName) -> std::filesystem::path`
  are all declared in `src/Editor/ProjectPanelData.h`, already reachable
  from `ProjectPanel.cpp` via its existing `#include "../ProjectPanelData.h"`
  (via `ProjectPanel.h`) — no new include needed for these three.
- `IsImportableAsKtx2Texture(const std::string&)`,
  `IsImportableAsMeshAsset(const std::string&)`, and
  `IsImportableAsMotionAsset(const std::string&)` (all `-> bool`, taking a
  lowercase, dot-prefixed extension) come from `src/Assets/AssetImporter.h`,
  already `#include`d by `ProjectPanel.cpp` today — no new include needed in
  the `.cpp` either. The ONE new include actually required is the one in
  `ProjectPanel.h` itself, above.

### 3.6 — `src/Network/NetworkServer.h`/`.cpp` changes

This is the step that makes 3.7's `Application.cpp` constructor call
actually compile — do this FIRST, before 3.7.

In `NetworkServer.h`:
- Add a fifth forward declaration, right after the existing
  `FrameDebuggerCommandBridge` one:

```cpp
// Forward-declared for the same cheap-header reason as FrameCaptureBridge/
// EngineCommandBridge/EditorUiCommandBridge/FrameDebuggerCommandBridge
// above - task_manager/stl-parser-2 campaign, PHASE1. Genuinely unused by
// any route yet - PHASE2 is what actually registers `POST /import_asset`
// against it - stored now purely so PHASE2 never needs to touch
// Application.h/.cpp again (see PHASE0_MASTER_STRATEGY.md's own Locked
// Design Decision #1 and this phase's own Definition of Done).
namespace gte { class AssetImportCommandBridge; }
```

- Extend the constructor with a fifth, defaulted, non-owning parameter
  (every existing call site — including every `tests/Network/*.cpp`
  construction — keeps compiling unchanged, exactly like the previous four
  additions):

```cpp
explicit NetworkServer(FrameCaptureBridge* captureBridge = nullptr,
    EngineCommandBridge* commandBridge = nullptr,
    EditorUiCommandBridge* uiCommandBridge = nullptr,
    FrameDebuggerCommandBridge* frameDebuggerCommandBridge = nullptr,
    AssetImportCommandBridge* assetImportCommandBridge = nullptr);
```

- Add a fifth private member, mirroring the existing four:

```cpp
// Non-owning - same lifetime contract as m_captureBridge above
// (task_manager/stl-parser-2 campaign, PHASE1). Not yet passed into
// RegisterRoutes() below - PHASE2 is what actually wires a real route
// handler against it, when POST /import_asset is registered.
AssetImportCommandBridge* m_assetImportCommandBridge = nullptr;
```

In `NetworkServer.cpp`:
- Update the constructor definition to accept and store the fifth pointer:

```cpp
NetworkServer::NetworkServer(FrameCaptureBridge* captureBridge, EngineCommandBridge* commandBridge,
    EditorUiCommandBridge* uiCommandBridge, FrameDebuggerCommandBridge* frameDebuggerCommandBridge,
    AssetImportCommandBridge* assetImportCommandBridge)
    : m_impl(std::make_unique<Impl>())
    , m_captureBridge(captureBridge)
    , m_commandBridge(commandBridge)
    , m_uiCommandBridge(uiCommandBridge)
    , m_frameDebuggerCommandBridge(frameDebuggerCommandBridge)
    , m_assetImportCommandBridge(assetImportCommandBridge)
{
    RegisterRoutes(m_impl->server, m_captureBridge, m_commandBridge, m_uiCommandBridge, m_frameDebuggerCommandBridge);
    // m_assetImportCommandBridge is deliberately NOT passed into
    // RegisterRoutes() yet - PHASE2 is what extends RegisterRoutes()'s own
    // signature (and its call site here) once a real /import_asset route
    // handler actually needs to read it.
}
```

- **No new `#include` is needed in `NetworkServer.cpp` for this phase** —
  `m_assetImportCommandBridge` is only ever stored/passed as a pointer here,
  never dereferenced, so the forward declaration in `NetworkServer.h` is
  sufficient (unlike `FrameCaptureBridge`/`EngineCommandBridge`/
  `EditorUiCommandBridge`/`FrameDebuggerCommandBridge`, whose full headers
  `NetworkServer.cpp` already includes because `RegisterRoutes()`'s route
  handlers actually call methods on them). `PHASE2` is what adds
  `#include "../Application/AssetImportCommandBridge.h"` here, when it
  starts actually calling `SubmitAndWait()`-adjacent methods on it (from the
  network thread's perspective) or `TryPeekPendingCommandRequest()`/
  `FulfillCommand()` (from `Application::Run()`'s perspective, already
  covered by 3.7 below via `Application.h`'s own include).

### 3.7 — `src/Application/Application.h`/`.cpp` changes

In `Application.h`:
- `#include "AssetImportCommandBridge.h"` alongside the four existing bridge
  includes.
- Add a FIFTH bridge member, `AssetImportCommandBridge m_assetImportCommandBridge;`,
  declared right after `m_frameDebuggerCommandBridge` (same "BEFORE
  `m_networkServer`, so its address can be handed into that constructor"
  reasoning every other bridge member's own comment already states).

In `Application.cpp`'s constructor (find where `m_networkServer` is
constructed with the other four bridge pointers — confirmed at
`, m_networkServer(&m_captureBridge, &m_commandBridge, &m_uiCommandBridge, &m_frameDebuggerCommandBridge)`
in the member-initializer list) — pass `&m_assetImportCommandBridge` as the
fifth argument. This now compiles because 3.6 above already extended
`NetworkServer`'s own constructor to accept it.

`Application.cpp` needs no separate `#include "AssetImportCommandBridge.h"`
of its own — confirmed: `Application.cpp`'s very first include is
`"Application.h"`, which (per this same step) now carries that include
transitively.

In `Application::Run()`, add a new drain block for the new bridge, at the
SAME point in the frame every other bridge's own drain block already lives
(right after `NewFrame()`, before `BuildUI()`) — but placed AFTER the
existing `m_frameDebuggerCommandBridge` drain block specifically (the LAST
of the four existing blocks in that same run of code, confirmed by reading
`Application.cpp` directly), not spliced in between two existing blocks —
this keeps every bridge's drain code appended in the same
chronological/"most-recently-added-bridge-goes-last" order the four
existing blocks already follow (`EditorUiCommandBridge` was added by
network-impl-7, then `FrameDebuggerCommandBridge` was appended after it by
frame-debugger-3 — never inserted in the middle):

```cpp
// task_manager/stl-parser-2, PHASE1 - drains at most ONE pending
// /import_asset request per frame. Unlike EditorUiCommandBridge's own
// pump above, this one does not need ImGui's context to be valid at all -
// ImportExternalAssetIntoProject() only touches ProjectPanel's own
// AssetDatabase/filesystem state, never ImGui - but it is kept at this
// same point in the frame, appended after every other bridge's own drain
// block, for locality with them.
if (const std::optional<AssetImportCommandRequest> importRequest =
        m_assetImportCommandBridge.TryPeekPendingCommandRequest()) {
    GTE_PROFILE_SCOPE("Application::ExecuteAssetImportCommand");
    AssetImportCommandResult importResult;
    importResult.kind = importRequest->kind;
    // Only one AssetImportCommandKind exists today (ImportExternalFile) -
    // a future addition would branch on importRequest->kind here, the
    // same shape ExecuteEngineCommand() already uses for its own bridge.
    const ProjectAssetImportResult layerResult = m_editorLayer->ImportExternalAssetIntoProject(
        importRequest->importExternalFile.sourceAbsolutePath,
        importRequest->importExternalFile.destinationRelativeFolder);
    ImportExternalFileOutcome& outcome = importResult.importExternalFile;
    outcome.projectAvailable = layerResult.projectAvailable;
    outcome.success = layerResult.success;
    outcome.message = layerResult.message;
    outcome.finalRelativePath = layerResult.finalRelativePath;
    outcome.finalAbsolutePath = layerResult.finalAbsolutePath;
    outcome.guid = layerResult.guid;
    outcome.convertedToMeshAsset = layerResult.convertedToMeshAsset;
    outcome.meshSourceFormat = layerResult.meshSourceFormat;
    outcome.convertedToKtx2 = layerResult.convertedToKtx2;
    outcome.convertedToMotionAsset = layerResult.convertedToMotionAsset;
    outcome.meshVertexCount = layerResult.meshVertexCount;
    outcome.meshTriangleCount = layerResult.meshTriangleCount;
    m_assetImportCommandBridge.FulfillCommand(importResult);
}
```

### 3.8 — `ImGuiEditorLayer.cpp` changes

Add the real override, placed right after the existing `ActivateTab()`
override:

```cpp
// task_manager/stl-parser-2, PHASE1 - see
// IEditorLayer::ImportExternalAssetIntoProject()'s own doc comment
// (EditorLayer.h) for the full contract. #if-gated exactly like
// m_projectPanel's own declaration - GTE_ENABLE_PROJECT_PANEL is a
// SEPARATE switch from GTE_ENABLE_EDITOR (see CMakeLists.txt), so a real
// Editor build can still have this panel compiled out.
ProjectAssetImportResult ImportExternalAssetIntoProject(
    const std::string& sourceAbsolutePath, const std::string& destinationRelativeFolder) override
{
    ProjectAssetImportResult result;
#if GTE_ENABLE_PROJECT_PANEL
    const AssetImportResult imported = m_projectPanel.ImportExternalFile(
        Utf8ToPath(sourceAbsolutePath), destinationRelativeFolder);
    result.projectAvailable = true;
    result.success = imported.success;
    result.message = imported.message;

    // std::filesystem::relative()'s error_code overload is used
    // deliberately (never the throwing 2-argument overload) - this class
    // has no surrounding try/catch, and this codebase's "never throws"
    // degrade-gracefully convention (see AssetImportResult's own doc
    // comment) means a relative-path computation failure here should fall
    // back to something still useful, never propagate an exception.
    std::error_code relEc;
    const std::filesystem::path relativePath =
        std::filesystem::relative(imported.finalPath, m_projectPanel.GetRootPath(), relEc);
    result.finalRelativePath = (!relEc) ? PathToUtf8(relativePath) : PathToUtf8(imported.finalPath);
    result.finalAbsolutePath = PathToUtf8(imported.finalPath);

    result.guid = imported.guid.IsValid() ? imported.guid.ToString() : std::string();
    result.convertedToMeshAsset = imported.convertedToMeshAsset;
    result.meshSourceFormat = (imported.meshSourceFormat == MeshSourceFormat::Stl) ? "stl"
        : (imported.meshSourceFormat == MeshSourceFormat::Pmx) ? "pmx" : std::string();
    result.convertedToKtx2 = imported.convertedToKtx2;
    result.convertedToMotionAsset = imported.convertedToMotionAsset;
    result.meshVertexCount = imported.meshVertexCount;
    result.meshTriangleCount = imported.meshTriangleCount;
#else
    result.projectAvailable = false;
#endif
    return result;
}
```

`Utf8ToPath`/`PathToUtf8` are already transitively visible in this file:
confirmed by reading `ImGuiEditorLayer.cpp` in full, it does NOT itself call
either function anywhere today (its `ProcessEvent()`'s
`SDL_EVENT_DROP_FILE` handling just forwards the raw UTF-8 string straight
into `m_projectPanel.HandleExternalFileDrop()` — the actual
path-conversion happens inside `ProjectPanel.cpp`, not here) — but both free
functions ARE already reachable in this translation unit, transitively, via
the existing `#include "Panels/ProjectPanel.h"` (itself gated
`#if GTE_ENABLE_PROJECT_PANEL`, already present at the top of this file) →
`../ProjectPanelData.h`, which declares both. No new include is needed for
them. `std::error_code`/`std::filesystem::path` need
`#include <filesystem>` and `#include <system_error>` added to this file's
own include block if not already present by the time this phase is
implemented — confirmed NOT present today (this file's current includes are
`imgui.h`, the ImGui SDL3/Vulkan backend headers, `<SDL3/SDL.h>`,
`<cstdint>`, `<optional>`, `<stdexcept>` — no `<filesystem>`/`<system_error>`
of its own; `std::filesystem::path` is only reachable today as an opaque
type via other headers' declarations, which is not sufficient to call
`std::filesystem::relative()` here).

### 3.9 — `CMakeLists.txt` changes

Register the two new files, right after the existing
`src/Application/EditorUiCommandBridge.cpp` entry (confirmed: that entry
lives in the root `CMakeLists.txt`'s always-compiled `target_sources(gte_core
PRIVATE ...)` list, NOT inside any `if(GTE_ENABLE_EDITOR)`/
`if(GTE_ENABLE_PROJECT_PANEL)` block — correct, since `AssetImportCommandBridge`
must compile in every configuration per the Risk Register):

```
src/Application/AssetImportCommandBridge.h
src/Application/AssetImportCommandBridge.cpp
```

### 3.10 — Tests

New file `tests/Application/AssetImportCommandBridgeTests.cpp`, mirroring
`tests/Application/EditorUiCommandBridgeTests.cpp` test-for-test (adjusted
for the new payload shape): `SubmitAndWaitTimesOutWhenNeverFulfilled`,
`SubmitAndWaitReturnsFulfilledResult`,
`SubmitAndWaitReturnsAlreadyPendingWhenAnotherRequestIsInFlight`,
`FulfillCommandIsANoOpWhenNothingIsPending`,
`TryPeekPendingCommandRequestReturnsNulloptWhenIdle`,
`LateFulfillmentAfterTimeoutIsInertAndDoesNotCorruptNextRequest`,
`PendingStateIsObservableAndClearsAfterFulfillment`. Use a SHORT explicit
timeout (e.g. `50`ms) in every test that intentionally times out — never
rely on the real 120000ms production default in a test (it would make the
test suite unacceptably slow). Register this file in `tests/CMakeLists.txt`'s
`GTE_TEST_SOURCES` list, in the same (always-compiled) section that already
lists `Application/EditorUiCommandBridgeTests.cpp` (confirmed at that exact
spot in the file today).

**Do NOT add a `tests/Editor/NullEditorLayerImportExternalAssetTests.cpp`
file, or any test that tries to construct `NullEditorLayer` directly.**
Confirmed by reading `src/Editor/NullEditorLayer.cpp` in full:
`NullEditorLayer` is a `class` declared inside an anonymous namespace,
private to that one `.cpp` file — nothing anywhere exposes its name, so a
test file cannot even write `NullEditorLayer{}` (there is no such name to
reference). The ONLY way to obtain an `IEditorLayer*` actually backed by it
is `CreateEditorLayer(Window&, Renderer&)` (`EditorLayer.h`), which needs a
real, live `Window`/`Renderer` (a real SDL window + a real Vulkan device) -
genuinely Tier 2. Confirmed by a full grep of `tests/`: NOTHING anywhere in
this test suite has ever constructed a real `IEditorLayer`/`NullEditorLayer`
this way — every existing Editor-bridge test (e.g.
`tests/Network/ActivateTabEndpointEndToEndTests.cpp`'s own
`FakeEditorUiStandIn`) deliberately implements its OWN small hand-written
`IEditorLayer`-shaped (or bridge-level) stand-in instead, precisely because
of this — the exact same "Tier 2, no automated coverage yet" acceptance this
codebase already documents for `ImGuiEditorLayer::ActivateTab()`'s own real
ImGui docking behavior (see that test file's own header comment). This
phase's own completion report must say the same thing for
`NullEditorLayer::ImportExternalAssetIntoProject()`: its
`projectAvailable == false` contract is correct by inspection of the
one-line override added in 3.4 above (trivial enough that this is an
accepted, explicitly-documented gap, matching this exact precedent), not by
an automated test — and no new test file should be invented just to have
something to register in `CMakeLists.txt`.

Similarly, do not add a new `ProjectPanel`-class-level test file either.
Confirmed by browsing `tests/Editor/`: no `ProjectPanelTests.cpp` exists
today — only `ProjectPanelDataTests.cpp`, which covers the pure,
ImGui-free helper functions in `ProjectPanelData.h` (`PathToUtf8`/
`Utf8ToPath`/`MakeUniqueDestinationPath`/etc.), never the `ProjectPanel`
class itself. `ProjectPanel` is an ImGui-window-owning class (constructing
one is cheap, but its meaningful behavior is entangled with `Build()`'s own
ImGui calls) that has never been unit-tested directly, and this phase does
not change that — `ImportExternalFile()`'s own NEW logic (the path-traversal
guard, `m_needsRescan`/`AssetDatabase` plumbing) is exercised only by manual
verification (see this phase's own Definition of Done: "manually verify
with a small throwaway PNG/text file"), mirroring `MeshAssetGpuCatalog`'s
own accepted "Tier 2, no automated coverage yet" precedent — say so
explicitly in the completion report, same as above. `ImportAssetFile()`
itself (the pure function `ImportExternalFile()` calls into) already has
full coverage from `stl-parser-1` and is entirely unchanged by this phase.

## Definition of Done (this phase's slice)

- `AssetImportCommandBridge` exists, compiles, and its own test suite passes
  (mirroring `EditorUiCommandBridgeTests.cpp` exactly).
- `IEditorLayer::ImportExternalAssetIntoProject()` exists; `NullEditorLayer`'s
  stub returns `projectAvailable == false`; `ImGuiEditorLayer`'s real
  implementation forwards to a new, working `ProjectPanel::ImportExternalFile()`.
- `ProjectPanel::ImportExternalFile()` correctly imports a real external
  file (manually verify with a small throwaway PNG/text file, NOT
  `terrain.stl` yet — that is `PHASE5`'s job), rejects a path-traversal OR
  drive/root-relative `destinationRelativeFolder`, and creates a missing
  destination sub-folder.
- `NetworkServer` (`src/Network/NetworkServer.h`/`.cpp`) has a real, fifth,
  defaulted `AssetImportCommandBridge*` constructor parameter and matching
  private member, wired into (but not yet consulted by) `RegisterRoutes()`.
- `Application` owns and correctly wires the new bridge (constructor
  injection into `NetworkServer` — even though `PHASE2` hasn't registered
  any route against it yet, the pointer must already be threaded through so
  `PHASE2` only needs to touch `NetworkServer.h`/`.cpp` — never
  `Application.h`/`.cpp` again).
- `cmake --build build` succeeds for BOTH `GreatTamanaEngine` and
  `GreatTamanaEngineTests`, in a configuration with `GTE_ENABLE_EDITOR=ON`
  AND `GTE_ENABLE_PROJECT_PANEL=ON` (to genuinely compile the real
  `ImGuiEditorLayer`/`ProjectPanel` changes) — a SEPARATE compile check with
  `GTE_ENABLE_EDITOR=OFF` is also required (confirms `NullEditorLayer.cpp`'s
  new stub compiles/links cleanly and nothing in `Application`/`Network`
  gained a hard Editor-only dependency).
- `ctest -C Debug --output-on-failure` passes with zero regressions.
- No `POST /import_asset` HTTP route exists yet — that is explicitly out of
  scope for this phase (`PHASE2`'s job).

---

## Double-Check Pass (stl-parser-2 campaign, Iteration 1.5)

This document was re-verified line-by-line against the real, current source
files it references (`EditorUiCommandBridge.h`/`.cpp`, `EditorLayer.h`,
`NullEditorLayer.cpp`, `ImGuiEditorLayer.cpp`, `Panels/ProjectPanel.h`/`.cpp`,
`ProjectPanelData.h`/`.cpp`, `Application.h`/`.cpp`, `Network/NetworkServer.h`/
`.cpp`, `Assets/AssetImporter.h`, `Assets/AssetTypes.h`, and the relevant
`CMakeLists.txt`/`tests/CMakeLists.txt` sections) before this campaign's five
implementation tasks were delegated. Corrections made during this pass:

1. **Fixed an off-by-one bridge count.** The original draft said
   `Application.h` has "five existing bridge members" and told the
   implementer to add a "sixth" — both wrong; there are FOUR existing
   bridges today, and this phase's new one is the FIFTH. Both mentions are
   now corrected (Step 2, and 3.7).
2. **Closed a real compile-breaking gap.** The original draft told
   `Application.cpp` to pass a fifth pointer into `NetworkServer`'s
   constructor (3.6, now 3.7) but never instructed anyone to actually add
   that fifth parameter to `NetworkServer.h`/`.cpp` — which would not
   compile. New Step 3.6 adds the concrete forward-declaration/constructor-
   parameter/member instructions this needed.
3. **Resolved the Section 3.7 "known gap" (now 3.8) definitively**, per this
   phase's own request: added `ProjectPanel::GetRootPath()` (option (a) from
   the original draft) instead of leaving two options open, and fixed the
   placeholder `GetProjectRootPathForDisplay()` name (which never existed)
   to the real accessor.
4. **Resolved the Section 3.5 "verify the helper signatures" hedge**: all
   three helpers (`PathToUtf8`/`Utf8ToPath`/`MakeUniqueDestinationPath`) and
   all three `AssetImporter.h` predicates are now confirmed, by direct
   reading of the real files, to have exactly the signatures/reachability
   this phase assumes.
5. **Hardened the path-traversal check** against a Windows-specific gap the
   original algorithm missed: a drive-relative (`"C:Temp"`) or root-relative
   (`"\Escape"`) `destination_folder` is NOT caught by `is_absolute()` alone
   (which requires both a root name AND a root directory together on
   Windows) — added a `has_root_path()` check alongside it. `PHASE0`'s
   Locked Design Decision #8 already implicitly relied on this being caught;
   this phase's algorithm now actually catches it.
6. **Corrected an inaccurate claim** that `ImGuiEditorLayer.cpp` "already
   handles UTF-8/`std::filesystem::path` conversions elsewhere" — it does
   not; that conversion happens inside `ProjectPanel.cpp`, not
   `ImGuiEditorLayer.cpp`. The functions are still reachable there
   (transitively, via the existing `Panels/ProjectPanel.h` include), just
   not for the reason originally stated.
7. **Fixed the new bridge's drain-block placement** in `Application::Run()`
   to be appended after the LAST existing bridge-drain block
   (`m_frameDebuggerCommandBridge`'s), not spliced in between two existing
   blocks, keeping every bridge's own drain code in the same
   append-only/chronological order the four existing blocks already follow.
8. **Removed an infeasible test proposal** (`NullEditorLayerImportExternalAssetTests.cpp`):
   `NullEditorLayer` is a private, anonymous-namespace class with no
   reachable name outside `NullEditorLayer.cpp`, and obtaining a real one
   needs a live `Window`/`Renderer` — confirmed nothing in this test suite
   has ever done this. Replaced with an explicit "accepted gap, verified by
   inspection" note, consistent with this codebase's own established
   precedent for the equivalent `ImGuiEditorLayer::ActivateTab()` case.
9. **Small robustness/consistency additions**: `ImportExternalFile()`'s
   `*.gta`-collision-aware renaming now also checks
   `IsImportableAsMotionAsset()` (`.vmd`), which the pre-existing
   `HandleExternalFileDrop()` this phase mirrors does not — a narrow,
   pre-existing, out-of-scope gap in that already-merged method, not
   something this phase touches there, but not worth deliberately copying
   into brand-new code either. `ImGuiEditorLayer.cpp`'s new override uses
   `std::filesystem::relative()`'s `error_code` overload (never the
   throwing one), consistent with this codebase's "never throws" convention.
10. Made `PHASE0_MASTER_STRATEGY.md`'s own Step 3 summary table and Locked
    Design Decision #8 consistent with the above (see that file's own
    change — directly tied to this phase's corrections only).
