#include "EditorSceneIOCapability.h"

#include "SceneIO.h"

namespace gte {

bool EditorSceneIOCapability::SaveScene(Game& game, const std::filesystem::path& scenePath, std::string& outErrorMessage)
{
    // gte::SaveScene() (SceneIO.h) returns a plain bool with no error-message
    // output of its own - this fallback string is byte-for-byte the same one
    // EngineCommandDispatch.cpp's own ON-branch used to set directly, before
    // this phase's conversion (behavior-preserving, not a new message).
    const bool success = gte::SaveScene(game, scenePath);
    if (!success) {
        outErrorMessage = "failed to write scene file (I/O error) - see engine log";
    }
    return success;
}

bool EditorSceneIOCapability::LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath,
    VkDescriptorSetLayout sceneServicesSetLayout, std::string& outErrorMessage)
{
    // Same reasoning as SaveScene() above - byte-for-byte the same fallback
    // string EngineCommandDispatch.cpp's own ON-branch used to set directly.
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

} // namespace gte
