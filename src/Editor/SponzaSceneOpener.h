#pragma once

#include "../Core/GraphicsHandles.h"

#include <string>

namespace gte {

class Game;
class Renderer;

struct OpenSponzaSceneResult {
    bool success = false;
    bool importedThisCall = false;
    std::string message; // Status line on success, error text on failure.
    std::string gtaPath; // Absolute path of the Sponza Mesh *.gta.
};

// Replaces the scene with the Sponza glTF sample, importing it to Project/Sponza on first use.
// The old scene is kept if the import or spawn fails. Main thread only. Never throws.
OpenSponzaSceneResult OpenSponzaScene(
    Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout);

} // namespace gte
