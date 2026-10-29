#include "EditorSceneIOCapability.h"

#include "SceneIO.h"
#include "SponzaSceneOpener.h"

namespace gte {

bool EditorSceneIOCapability::SaveScene(Game& game, const std::filesystem::path& scenePath, std::string& outErrorMessage)
{
    // gte::SaveScene() returns a plain bool, so a generic message is reported.
    const bool success = gte::SaveScene(game, scenePath);
    if (!success) {
        outErrorMessage = "failed to write scene file (I/O error) - see engine log";
    }
    return success;
}

bool EditorSceneIOCapability::LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath,
    DescriptorSetLayoutHandle sceneServicesSetLayout, std::string& outErrorMessage)
{
    // gte::LoadScene() returns a plain bool, so a generic message is reported.
    const bool success = gte::LoadScene(game, renderer, scenePath, sceneServicesSetLayout);
    if (!success) {
        outErrorMessage = "failed to load scene file - it may not exist, or failed to parse (see engine log)";
    }
    return success;
}

std::filesystem::path EditorSceneIOCapability::DefaultScenePath() const
{
    return gte::DefaultScenePath();
}

ISceneIOCapability::OpenSceneOutcome EditorSceneIOCapability::OpenPresetScene(Game& game, Renderer& renderer,
    DescriptorSetLayoutHandle sceneServicesSetLayout, PresetScene kind, const PresetParameters&)
{
    if (kind != PresetScene::Sponza) {
        return { false, false, "this preset scene is not available yet", {} };
    }
    OpenSponzaResult result = OpenSponza(game, renderer, sceneServicesSetLayout);
    return { result.success, result.importedThisCall, std::move(result.message), std::move(result.gtaPath) };
}

} // namespace gte
