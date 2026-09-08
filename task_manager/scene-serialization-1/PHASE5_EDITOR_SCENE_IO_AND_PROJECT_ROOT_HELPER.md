# PHASE5 — `Editor/ProjectRootPath.h/.cpp` + `Editor/SceneIO.h/.cpp`

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`.
Depends on PHASE2/PHASE3/PHASE4 (`PrimitiveSource`, `SceneDocument`,
`SerializeSceneDocument()`/`DeserializeSceneDocument()`,
`BuildSceneDocumentFromRegistry()`/`ClearSerializableSceneObjects()`).
PHASE6 depends on this phase's `SaveScene()`/`LoadScene()`.

## Step 1: The Goal (Where are we going?)

Deliver the actual, callable `SaveScene(Game&)` / `LoadScene(Game&,
Renderer&)` functions — the first genuinely Tier-2 (Game/Renderer/real
filesystem touching) piece of this campaign — plus a small, shared,
**not** `GTE_ENABLE_PROJECT_PANEL`-gated helper that resolves "the Project
folder next to the .exe," since `File > Save`/`File > Open` (PHASE6) must
work whenever `GTE_ENABLE_EDITOR` is ON, independent of that separate
switch.

## Step 2: The Situation / The Problem (Where are we now?)

- `src/Editor/Panels/ProjectPanel.cpp`'s constructor is, today, the ONLY
  place that resolves `SDL_GetBasePath() + "Project"` — and
  `Panels/ProjectPanel.*`/`Editor/ProjectPanelData.*` are only compiled when
  `GTE_ENABLE_PROJECT_PANEL` is ON (confirmed directly in the root
  `CMakeLists.txt`'s nested `if(GTE_ENABLE_PROJECT_PANEL)` block, lines
  506–528). A Save/Open feature that must work with that switch OFF cannot
  reuse `ProjectPanelData.h`'s `Utf8ToPath()`/`EnsureProjectRootExists()`
  either, for the same reason.
- `src/Assets/AssetDatabase.h`'s `RefreshFromDirectory()`/`ImportAsset()`/
  `FindByGuid()`/`FindByPath()` are confirmed (direct `CMakeLists.txt`
  inspection) to be **unconditionally compiled** — always available
  regardless of `GTE_ENABLE_PROJECT_PANEL`.
- `Game::CreatePrimitiveEntity(Renderer&, PrimitiveType)` and
  `Game::CreateMeshEntityFromGtaFile(Renderer&, const std::string&
  absoluteGtaPath)` are both already public (`src/Game/Game.h`) — this
  phase's glue calls them exactly the way `Panels/HierarchyPanel.cpp`
  already does, never reaching into `Game`'s private internals.
- `Game::GetRegistry()` is already public — `BuildSceneDocumentFromRegistry()`/
  `ClearSerializableSceneObjects()` (PHASE4) are called against
  `game.GetRegistry()` directly; `Game` itself needs **no new public
  method** for this campaign (confirmed by design — see PHASE0, Culprit E).
- `src/Assets/GtaFile.cpp`'s `WriteGtaFile()` is this codebase's existing
  precedent for "create any missing parent directories first, then write" —
  mirrored here for `TestScene.gtscene`.

## Step 3: The Plan (A very detailed strategy)

### 3.1 — `src/Editor/ProjectRootPath.h` (new file)

```cpp
#pragma once

#include <filesystem>

namespace gte {

// Resolves the one "Project" folder every Editor feature that needs a
// stable, on-disk authoring location uses - the directory containing the
// built .exe (via SDL_GetBasePath()), plus a "Project" subfolder, exactly
// matching Panels/ProjectPanel.cpp's own long-standing convention. Pulled
// out as its own small, UNCONDITIONALLY-GTE_ENABLE_EDITOR-compiled helper
// (i.e. NOT gated behind the separate GTE_ENABLE_PROJECT_PANEL switch) so
// any core Editor feature - not just the "Project" panel itself - can
// resolve the same folder consistently. See
// task_manager/scene-serialization-1/PHASE5_EDITOR_SCENE_IO_AND_PROJECT_ROOT_HELPER.md
// for why this had to be extracted rather than reused from
// ProjectPanelData.h directly (that header is GTE_ENABLE_PROJECT_PANEL-only).
// Never throws; always returns SOME path (falls back to "./Project" if
// SDL can't determine the executable's own directory for some reason).
std::filesystem::path ResolveProjectRootDirectory();

} // namespace gte
```

### 3.2 — `src/Editor/ProjectRootPath.cpp` (new file)

```cpp
#include "ProjectRootPath.h"

#include <SDL3/SDL.h>

namespace gte {

std::filesystem::path ResolveProjectRootDirectory()
{
    // SDL_GetBasePath() returns the directory containing the running
    // executable (with a trailing separator), UTF-8 encoded, owned by SDL
    // (never freed by us). Constructed via std::u8string (path's own
    // C++20 UTF-8-aware constructor) rather than a bare
    // std::filesystem::path(std::string) construction, which would
    // otherwise go through the OS's native narrow encoding instead of
    // UTF-8 on Windows - the same reasoning ProjectPanelData.h's own
    // Utf8ToPath() documents, duplicated here (as a small, deliberate,
    // documented exception) rather than reused, since that header is only
    // compiled when GTE_ENABLE_PROJECT_PANEL is ON and this helper must
    // work regardless of that switch.
    const char* basePath = SDL_GetBasePath();
    const std::string basePathUtf8 = (basePath != nullptr) ? basePath : "./";
    return std::filesystem::path(std::u8string(basePathUtf8.begin(), basePathUtf8.end())) / "Project";
}

} // namespace gte
```

### 3.3 — Refactor `ProjectPanel.cpp` to use the shared helper (remove duplication)

In `src/Editor/Panels/ProjectPanel.cpp`, `ProjectPanel::ProjectPanel()`
(currently lines 35–47), replace the body with a call to the new shared
helper instead of its own inline `SDL_GetBasePath()`/`Utf8ToPath()`
computation:

```cpp
ProjectPanel::ProjectPanel()
{
    // See ProjectRootPath.h for why this now resolves through a shared
    // helper (also used by Editor/SceneIO.h's Save/Load, which must work
    // even when GTE_ENABLE_PROJECT_PANEL is OFF) rather than its own
    // private computation - this keeps exactly ONE place in the codebase
    // that decides where "Project" lives, instead of two independent
    // copies that could silently drift apart.
    m_rootPath = ResolveProjectRootDirectory();
}
```

Add `#include "../ProjectRootPath.h"` to this file's include list. The
`#include <SDL3/SDL.h>` at the top of `ProjectPanel.cpp` may become unused
after this change — check the rest of the file (`HandleExternalFileDrop()`
doesn't use SDL directly; confirm no other `SDL_*` call remains in this
specific file before removing the include; if none remain, remove it).

### 3.4 — `src/Editor/SceneIO.h` (new file)

```cpp
#pragma once

#include <filesystem>

namespace gte {

class Game;
class Renderer;

// Hardcoded save/load target for this campaign - see
// task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md for why this
// is deliberately a single, fixed path rather than a user-chosen one. Always
// ResolveProjectRootDirectory() / "TestScene.gtscene" - a *.gtscene text
// file (Scene/SceneTextFormat.h) directly under the Project folder, never
// wrapped as a tracked *.gta AssetDatabase asset itself (it does not get
// its own Guid - see PHASE0's "What We Will NOT Do").
std::filesystem::path DefaultScenePath();

// Serializes `game`'s current ECS world (Scene/SceneBuilder.h's
// BuildSceneDocumentFromRegistry()) and writes it, as text
// (Scene/SceneTextFormat.h's SerializeSceneDocument()), to
// DefaultScenePath() - creating the Project folder first if it doesn't
// exist yet (mirrors Assets/GtaFile.cpp's WriteGtaFile()'s own "creates any
// missing parent directories first" convention). An AssetDatabase is
// scanned fresh, right here, against ResolveProjectRootDirectory() - only
// used to resolve each asset-spawned root's gtaPath into a stable Guid (see
// SceneBuilder.h) - never persisted/cached across calls. Always OVERWRITES
// whatever was previously at DefaultScenePath(), with no confirmation
// prompt (per this campaign's own "keep it simple" scope). Returns false
// (and leaves the previous file, if any, untouched where avoidable) on any
// I/O failure - never throws.
bool SaveScene(Game& game);

// Reads DefaultScenePath(), parses it (Scene/SceneTextFormat.h's
// DeserializeSceneDocument()), and - only if that succeeds - replaces
// `game`'s current scene content: Scene/SceneBuilder.h's
// ClearSerializableSceneObjects() destroys every entity this feature owns
// (leaving anything it doesn't own, e.g. the default Camera - see PHASE1 -
// untouched), then every SceneObjectRecord in the parsed document is spawned
// via `game`'s own existing public API
// (Game::CreatePrimitiveEntity()/CreateMeshEntityFromGtaFile()) and its
// Transform/Name are set to match the record. `renderer` is needed because
// both of those spawn methods need one to build/upload GPU mesh data.
//
// An Asset record whose Guid no longer resolves via a fresh
// AssetDatabase::FindByGuid() scan (the referenced *.gta was moved/deleted
// since the scene was last saved) is skipped gracefully - that one object
// is simply not restored, everything else in the file still loads normally.
//
// Returns false or DOES NOT modify `game`'s registry at all when
// DefaultScenePath() doesn't exist or fails to parse (a malformed/missing
// file never partially clears the current scene) - never throws.
bool LoadScene(Game& game, Renderer& renderer);

} // namespace gte
```

### 3.5 — `src/Editor/SceneIO.cpp` (new file)

```cpp
#include "SceneIO.h"

#include "ProjectRootPath.h"
#include "../ECS/Components/Name.h"
#include "../ECS/Components/Transform.h"
#include "../ECS/Registry.h"
#include "../Assets/AssetDatabase.h"
#include "../Game/Game.h"
#include "../Scene/SceneBuilder.h"
#include "../Scene/SceneTextFormat.h"

#include <fstream>
#include <sstream>

namespace gte {

std::filesystem::path DefaultScenePath()
{
    return ResolveProjectRootDirectory() / "TestScene.gtscene";
}

bool SaveScene(Game& game)
{
    const std::filesystem::path projectRoot = ResolveProjectRootDirectory();

    AssetDatabase assetDatabase;
    assetDatabase.RefreshFromDirectory(projectRoot); // Safe even if projectRoot doesn't exist yet - returns 0.

    const SceneDocument document = BuildSceneDocumentFromRegistry(game.GetRegistry(), assetDatabase);
    const std::string text = SerializeSceneDocument(document);

    const std::filesystem::path scenePath = projectRoot / "TestScene.gtscene";

    std::error_code ec;
    std::filesystem::create_directories(scenePath.parent_path(), ec);
    // Deliberately not checked/aborted-on: if scenePath.parent_path() already
    // exists, create_directories() reports an ec that std::ofstream below
    // will simply succeed past anyway - matching WriteGtaFile()'s own
    // tolerant convention.

    std::ofstream file(scenePath, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

bool LoadScene(Game& game, Renderer& renderer)
{
    const std::filesystem::path scenePath = DefaultScenePath();

    std::ifstream file(scenePath, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    const std::optional<SceneDocument> document = DeserializeSceneDocument(buffer.str());
    if (!document.has_value()) {
        return false; // Malformed file - do NOT touch the current scene at all.
    }

    const std::filesystem::path projectRoot = ResolveProjectRootDirectory();
    AssetDatabase assetDatabase;
    assetDatabase.RefreshFromDirectory(projectRoot);

    ClearSerializableSceneObjects(game.GetRegistry());

    for (const SceneObjectRecord& record : document->objects) {
        Entity spawned = kInvalidEntity;
        if (record.kind == SceneObjectKind::Primitive) {
            spawned = game.CreatePrimitiveEntity(renderer, record.primitiveType);
        } else {
            const AssetRecord* asset = assetDatabase.FindByGuid(record.assetGuid);
            if (asset == nullptr) {
                continue; // Asset moved/deleted since last Save - skip this one object gracefully.
            }
            spawned = game.CreateMeshEntityFromGtaFile(renderer, asset->gtaPath);
        }
        if (spawned == kInvalidEntity) {
            continue;
        }

        if (Transform* transform = game.GetRegistry().TryGetComponent<Transform>(spawned); transform != nullptr) {
            transform->position = record.position;
            transform->rotation = record.rotation;
            transform->scale = record.scale;
        }
        if (!record.name.empty()) {
            game.GetRegistry().AddComponent<Name>(spawned, Name{ record.name });
        }
    }

    return true;
}

} // namespace gte
```

Note: `Entity`'s `operator==`/`kInvalidEntity` are already available via
`ECS/Entity.h`, transitively included through `ECS/Registry.h` — no extra
include needed for the `spawned == kInvalidEntity` check.

### 3.6 — `CMakeLists.txt` wiring

Add both new pairs to the `if(GTE_ENABLE_EDITOR)` block's
`target_sources(gte_core PRIVATE ...)` list (currently lines 459–498) —
**outside** the nested `if(GTE_ENABLE_PROJECT_PANEL)` block (lines
506–528), since both files must compile whenever `GTE_ENABLE_EDITOR` is ON
regardless of that separate switch. A sensible insertion point is right
after `src/Editor/EditorContext.h` (line 460) and before `src/Editor/Selection.h`
(line 461):

```
        src/Editor/EditorContext.h
        src/Editor/ProjectRootPath.h
        src/Editor/ProjectRootPath.cpp
        src/Editor/SceneIO.h
        src/Editor/SceneIO.cpp
        src/Editor/Selection.h
        ...
```

No new external library dependency is introduced (`<fstream>`/`<sstream>`
are standard library; `SDL3/SDL.h` is already linked into `gte_core`
unconditionally).

### 3.7 — No new automated test file for `SceneIO.cpp` itself (documented, accepted gap)

`SaveScene()`/`LoadScene()` need a live `Renderer` (via
`Game::CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()`) to actually
exercise end-to-end — the exact same "Tier 2, no automated coverage yet"
bucket `RenderSystem::Draw()`/`PrimitiveGpuCatalog` already fall into (see
`tests/CMakeLists.txt`'s own "Tier 2" note). This is an intentional,
documented gap, not an oversight — every PURE piece this file is built from
(`SerializeSceneDocument()`/`DeserializeSceneDocument()` — PHASE3;
`BuildSceneDocumentFromRegistry()`/`ClearSerializableSceneObjects()` —
PHASE4) is already fully Tier-1-tested; `SceneIO.cpp` itself is reviewed by
direct code inspection instead, following this codebase's own established
precedent (e.g. `RenderSystem::Draw()`, `Panels/ProjectPanel.cpp`'s
own filesystem-touching methods).

## Step 4: What We Will NOT Do (Focus)

- We will **not** add a file-picker/"Save As" — `DefaultScenePath()` is the
  one, single, hardcoded path both `SaveScene()`/`LoadScene()` ever use.
- We will **not** cache/reuse an `AssetDatabase` instance across calls —
  each `SaveScene()`/`LoadScene()` call builds and scans its own fresh,
  throwaway one, exactly mirroring `ProjectPanel`'s own
  `m_assetDatabase.RefreshFromDirectory()` re-scan convention (never a
  stale, long-lived guid↔path index).
- We will **not** give `Game` any new public method for this feature — every
  piece of new glue lives in the Editor layer (`SceneIO.cpp`), calling only
  `Game`'s pre-existing public API (`GetRegistry()`/
  `CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()`).
