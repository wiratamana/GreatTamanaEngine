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
    DescriptorSetLayoutHandle sceneServicesSetLayout, PresetScene kind, const PresetParameters& parameters)
{
    const HizDemoServices services = m_hizDemoServices ? m_hizDemoServices() : HizDemoServices{};
    if (kind == PresetScene::HizDemo) {
        const HizDemoReport report = OpenHizDemo(game, renderer, sceneServicesSetLayout, parameters, services);
        OpenSceneOutcome outcome;
        outcome.success = report.success;
        outcome.message = report.message;
        if (report.success) {
            outcome.detailsJson = BuildHizDemoDetailsJson(report);
        }
        return outcome;
    }

    OpenSponzaResult result = OpenSponza(game, renderer, sceneServicesSetLayout);
    if (result.success && !ReleaseHizDemoSettings(services)) {
        result.message += " The Hi-Z demo settings were not fully restored (see the log).";
    }
    return { result.success, result.importedThisCall, std::move(result.message), std::move(result.gtaPath), {} };
}

ISceneIOCapability::CloseSceneOutcome EditorSceneIOCapability::ClosePresetScene(Game& game)
{
    const HizDemoServices services = m_hizDemoServices ? m_hizDemoServices() : HizDemoServices{};
    CloseSceneOutcome outcome;
    outcome.success = CloseHizDemo(game, services, outcome.message);
    return outcome;
}

} // namespace gte
