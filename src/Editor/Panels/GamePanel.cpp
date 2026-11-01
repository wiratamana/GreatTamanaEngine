#include "GamePanel.h"

#include "../EditorContext.h"
#include "../EditorFrame.h"
#include "OverlayLabel.h"

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
        if (ctx.hizDemo.active && ctx.hizDemo.geometryOnly) {
            DrawViewOverlayLabel(ImGui::GetItemRectMin(), ImGui::GetItemRectSize(), kHizDemoOverlayLabel, 0);
        }
    }
}

} // namespace gte
