#pragma once

#include "../Core/GraphicsHandles.h"

#include "../Core/EditorCapabilities.h"

namespace gte {

// Editor implementation of ISceneIOCapability, delegating to the SceneIO.h free functions.
// The header stays free of SceneIO.h; only the .cpp depends on it.
class EditorSceneIOCapability : public ISceneIOCapability {
public:
    bool SaveScene(Game& game, const std::filesystem::path& scenePath, std::string& outErrorMessage) override;
    bool LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath,
        DescriptorSetLayoutHandle sceneServicesSetLayout, std::string& outErrorMessage) override;
    std::filesystem::path DefaultScenePath() const override;
};

} // namespace gte
