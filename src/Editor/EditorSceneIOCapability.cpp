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

FurinaShowcaseServices EditorSceneIOCapability::MakeFurinaServices()
{
    FurinaShowcaseServices services = m_furinaServices ? m_furinaServices() : FurinaShowcaseServices{};
    services.state = &m_furinaState;
    return services;
}

ISceneIOCapability::OpenSceneOutcome EditorSceneIOCapability::OpenPresetScene(Game& game, Renderer& renderer,
    DescriptorSetLayoutHandle sceneServicesSetLayout, PresetScene kind, const PresetParameters& parameters)
{
    const HizDemoServices hizServices = m_hizDemoServices ? m_hizDemoServices() : HizDemoServices{};
    FurinaShowcaseServices furinaServices = MakeFurinaServices();

    if (kind == PresetScene::FurinaShowcase) {
        furinaServices.releasePrevious = [&hizServices]() { ReleaseHizDemoSettings(hizServices); };
        const FurinaShowcaseReport report = OpenFurinaShowcase(game, renderer, sceneServicesSetLayout, furinaServices);
        OpenSceneOutcome outcome;
        outcome.success = report.success;
        outcome.message = report.message;
        if (report.success) {
            outcome.detailsJson = BuildFurinaShowcaseDetailsJson(report);
        }
        return outcome;
    }

    // A running showcase hands its settings back before the next preset reads or sets any.
    const bool furinaRestored = ReleaseFurinaShowcaseSettings(furinaServices);
    const char* const furinaWarning = " The Furina showcase settings were not fully restored (see the log).";

    if (kind == PresetScene::HizDemo) {
        const HizDemoReport report = OpenHizDemo(game, renderer, sceneServicesSetLayout, parameters, hizServices);
        OpenSceneOutcome outcome;
        outcome.success = report.success;
        outcome.message = report.message;
        if (!furinaRestored) {
            outcome.message += furinaWarning;
        }
        if (report.success) {
            outcome.detailsJson = BuildHizDemoDetailsJson(report);
        }
        return outcome;
    }

    OpenSponzaResult result = OpenSponza(game, renderer, sceneServicesSetLayout);
    if (result.success && !ReleaseHizDemoSettings(hizServices)) {
        result.message += " The Hi-Z demo settings were not fully restored (see the log).";
    }
    if (!furinaRestored) {
        result.message += furinaWarning;
    }
    return { result.success, result.importedThisCall, std::move(result.message), std::move(result.gtaPath), {} };
}

ISceneIOCapability::CloseSceneOutcome EditorSceneIOCapability::ClosePresetScene(Game& game)
{
    CloseSceneOutcome outcome;
    if (m_furinaState.active) {
        outcome.success = CloseFurinaShowcase(game, MakeFurinaServices(), outcome.message);
        return outcome;
    }
    const HizDemoServices services = m_hizDemoServices ? m_hizDemoServices() : HizDemoServices{};
    outcome.success = CloseHizDemo(game, services, outcome.message);
    return outcome;
}

} // namespace gte
