#include "GamePanel.h"

#include "../EditorContext.h"
#include "OverlayLabel.h"

#include <imgui.h>

#include <cstdint>

namespace gte {

void BuildGamePanel(EditorContext& ctx)
{
    // No scrollbars: the image fills the content size. A hidden tab (Begin() false) is reported via
    // ctx.gameViewVisible so the next frame skips the view.
    const bool isVisible =
        ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ctx.gameViewVisible = isVisible;

    if (isVisible) {
        // The panel's content size; ImGuiEditorLayer::PrimaryViewTarget() resizes the texture to it next frame.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            ctx.desiredExtent.width = static_cast<std::uint32_t>(avail.x);
            ctx.desiredExtent.height = static_cast<std::uint32_t>(avail.y);

            // Draw at the panel's available size, not the texture's (possibly stale) extent: free aspect, no gaps.
            ImGui::Image(
                static_cast<ImTextureID>(static_cast<std::uint64_t>(ctx.gameViewDescriptor)),
                avail);
            if (ctx.hizDemo.active && ctx.hizDemo.geometryOnly) {
                DrawViewOverlayLabel(ImGui::GetItemRectMin(), ImGui::GetItemRectSize(), kHizDemoOverlayLabel, 0);
            }
        }
    }
    ImGui::End();
}

} // namespace gte
