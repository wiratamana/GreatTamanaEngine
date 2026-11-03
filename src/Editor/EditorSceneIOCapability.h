#pragma once

#include "../Core/GraphicsHandles.h"

#include "../Core/EditorCapabilities.h"
#include "FurinaShowcaseSceneOpener.h"
#include "HizDemoSceneOpener.h"

#include <functional>

namespace gte {

// Editor implementation of ISceneIOCapability, delegating to the SceneIO.h free functions.
// The header stays free of SceneIO.h; only the .cpp depends on it.
class EditorSceneIOCapability : public ISceneIOCapability {
public:
    // Supplies the handles the Hi-Z demo scene works through; called on every open. Without one, that preset fails.
    void SetHizDemoServicesProvider(std::function<HizDemoServices()> provider) { m_hizDemoServices = std::move(provider); }

    // Supplies the feature handles the Furina showcase works through; its `state` is filled in here.
    void SetFurinaShowcaseServicesProvider(std::function<FurinaShowcaseServices()> provider)
    {
        m_furinaServices = std::move(provider);
    }

    bool SaveScene(Game& game, const std::filesystem::path& scenePath, std::string& outErrorMessage) override;
    bool LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath,
        DescriptorSetLayoutHandle sceneServicesSetLayout, std::string& outErrorMessage) override;
    std::filesystem::path DefaultScenePath() const override;
    OpenSceneOutcome OpenPresetScene(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
        PresetScene kind, const PresetParameters& parameters) override;
    CloseSceneOutcome ClosePresetScene(Game& game) override;

private:
    FurinaShowcaseServices MakeFurinaServices();

    std::function<HizDemoServices()> m_hizDemoServices;
    std::function<FurinaShowcaseServices()> m_furinaServices;
    FurinaShowcaseState m_furinaState;
};

} // namespace gte
