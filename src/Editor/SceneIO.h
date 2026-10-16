#pragma once

#include "../Core/GraphicsHandles.h"

#include <filesystem>

namespace gte {

class Game;
class Renderer;

// Fixed scene file: <project root>/TestScene.gtscene.
std::filesystem::path DefaultScenePath();

// Writes the current ECS world as JSON to `scenePath`, creating parent directories. False on I/O failure.
bool SaveScene(Game& game, const std::filesystem::path& scenePath);

// Same, to DefaultScenePath().
bool SaveScene(Game& game);

// Replaces the whole scene with the document at `scenePath`. False, scene untouched, if the file is
// missing or fails to parse. `sceneServicesSetLayout` is the layout of Core's scene-services set.
bool LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath,
    DescriptorSetLayoutHandle sceneServicesSetLayout);

// Same, from DefaultScenePath().
bool LoadScene(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout);

} // namespace gte
