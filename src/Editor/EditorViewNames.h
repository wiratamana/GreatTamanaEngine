#pragma once

#include "../Renderer/RenderGraph/RenderGraphHandles.h"

namespace gte {

// Texture names the editor shows for its two views. Tools and docs know these names.
inline constexpr const char* kGameViewResourceName = "GameView";
inline constexpr const char* kSceneViewResourceName = "SceneView";

inline const char* EditorViewResourceName(rg::ViewScope scope) noexcept
{
    return scope == rg::ViewScope::Secondary ? kSceneViewResourceName : kGameViewResourceName;
}

// Display label of a view scope: "Shared" / "Game" / "Scene".
constexpr const char* ViewScopeLabel(rg::ViewScope scope) noexcept
{
    switch (scope) {
    case rg::ViewScope::Primary: return "Game";
    case rg::ViewScope::Secondary: return "Scene";
    case rg::ViewScope::Shared: break;
    }
    return "Shared";
}

} // namespace gte
