#include "ScenePanel.h"

#include "../EditorContext.h"

#include <imgui.h>

#include <cstdint>

namespace gte {

void BuildScenePanel(EditorContext& ctx)
{
    // ImGui::Begin() returns false when this panel isn't actually visible
    // right now (e.g. it's an inactive tab behind "Game", or collapsed) -
    // stashed in ctx.sceneViewVisible for
    // ImGuiEditorLayer::SceneViewTarget() to read at the START of NEXT
    // frame, same "act on it next frame" pattern GamePanel.cpp uses for
    // ctx.gameViewVisible/desiredExtent - so a hidden Scene view skips
    // being rendered into at all next frame, see
    // EditorContext::sceneViewVisible.
    const bool isVisible =
        ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ctx.sceneViewVisible = isVisible;

    if (isVisible) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            // Own extent (ctx.desiredSceneExtent), applied by
            // ImGuiEditorLayer::SceneViewTarget() next frame - kept
            // completely separate from ctx.desiredExtent (GamePanel's),
            // so resizing/splitting "Scene" never resizes the Game-view
            // RenderTexture, and vice versa.
            ctx.desiredSceneExtent.width = static_cast<std::uint32_t>(avail.x);
            ctx.desiredSceneExtent.height = static_cast<std::uint32_t>(avail.y);

            // Its own RenderTexture now (ctx.sceneViewDescriptor) - "Scene"
            // no longer displays the same image as "Game".
            ImGui::Image(
                static_cast<ImTextureID>(reinterpret_cast<intptr_t>(ctx.sceneViewDescriptor)),
                avail);
        }
    }
    ImGui::End();
}

} // namespace gte
