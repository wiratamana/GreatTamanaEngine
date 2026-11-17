#include "GamePanel.h"

#include "../EditorContext.h"
#include "../EditorFeatureHooks.h"
#include "../EditorFrame.h"
#include "OverlayLabel.h"
#include "../../Game/Game.h"
#include "../../Game/RenderSystem.h"

#include <imgui.h>

#include <cstdint>

namespace gte {

// No scrollbars: the image fills the content size.
int GamePanel::ExtraFlags() const
{
    return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
}

void GamePanel::OnGui(EditorFrame& frame)
{
    EditorContext& ctx = frame.ctx;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x >= 1.0f && avail.y >= 1.0f) {
        ctx.desiredExtent.width = static_cast<std::uint32_t>(avail.x);
        ctx.desiredExtent.height = static_cast<std::uint32_t>(avail.y);

        // Draw at the panel's available size, not the texture's (possibly stale) extent: free aspect, no gaps.
        ImGui::Image(static_cast<ImTextureID>(static_cast<std::uint64_t>(ctx.gameViewDescriptor)), avail);
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageSize = ImGui::GetItemRectSize();

        EditorViewContext view;
        view.kind = EditorViewKind::Game;
        view.rectMin = Vec2(imageMin.x, imageMin.y);
        view.rectMax = Vec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
        view.viewProjection = RenderSystem::ResolveActiveCameraViewProjection(frame.game.GetRegistry(), avail.x / avail.y);
        view.hovered = ImGui::IsItemHovered();
        const EditorFeatureHooks& hooks = ctx.extensions.Get<EditorFeatureHooks>();
        hooks.RunViewInput(view);
        hooks.RunViewOverlay(view);
        if (ctx.hizDemo.active && ctx.hizDemo.geometryOnly) {
            DrawViewOverlayLabel(ImGui::GetItemRectMin(), ImGui::GetItemRectSize(), kHizDemoOverlayLabel, 0);
        }
    }
}

} // namespace gte
