#include "SceneIO.h"

#include "ProjectRootPath.h"
#include "../Assets/AssetDatabase.h"
#include "../Game/Game.h"
#include "../Scene/SceneBuilder.h"
#include "../Scene/SceneJsonFormat.h"

#include <fstream>
#include <sstream>

namespace gte {

std::filesystem::path DefaultScenePath()
{
    return ResolveProjectRootDirectory() / "TestScene.gtscene";
}

bool SaveScene(Game& game, const std::filesystem::path& scenePath)
{
    const std::filesystem::path projectRoot = ResolveProjectRootDirectory();

    AssetDatabase assetDatabase;
    assetDatabase.RefreshFromDirectory(projectRoot); // Safe even if projectRoot doesn't exist yet - returns 0.

    const SceneDocument document = BuildSceneDocumentFromRegistry(game.GetRegistry(), assetDatabase);
    const std::string text = SerializeSceneDocument(document);

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

bool SaveScene(Game& game)
{
    return SaveScene(game, DefaultScenePath());
}

bool LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath)
{
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

    // editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 4), PHASE3 - the recipe-aware reconstruction itself now lives
    // in Scene/SceneBuilder.h/.cpp's ReconstructSceneFromDocument()
    // (gte_core-tier), shared with Project Assembly Hot Reload's own restore
    // path (Core/Plugins/ProjectAssemblyHotReload.cpp's
    // RestoreProjectAssemblyHotReloadState()) - see that function's own doc
    // comment for the full, exact, already-proven algorithm. This wrapper's
    // own remaining job: resolve the project root, scan a fresh
    // AssetDatabase (unchanged behavior from before this refactor - "scanned
    // fresh, right here, against ResolveProjectRootDirectory() - never
    // persisted/cached across calls", this file's own pre-existing doc
    // comment in SceneIO.h, still true), clear the scene, then call the
    // shared function.
    const std::filesystem::path projectRoot = ResolveProjectRootDirectory();
    AssetDatabase assetDatabase;
    assetDatabase.RefreshFromDirectory(projectRoot);

    ClearEntireScene(game.GetRegistry());
    ReconstructSceneFromDocument(game, renderer, *document, assetDatabase);
    return true;
}

bool LoadScene(Game& game, Renderer& renderer)
{
    return LoadScene(game, renderer, DefaultScenePath());
}

} // namespace gte
