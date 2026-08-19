#include "ScenePanel.h"

#include "../EditorContext.h"

#include <imgui.h>

#include <cstdint>

namespace gte {

void BuildScenePanel(EditorContext& ctx)
{
    ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x >= 1.0f && avail.y >= 1.0f) {
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(ctx.gameViewDescriptor)), avail);
    }

    ImGui::End();
}

} // namespace gte
