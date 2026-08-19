#include "GamePanel.h"

#include "../EditorContext.h"

#include <imgui.h>

#include <cstdint>

namespace gte {

void BuildGamePanel(EditorContext& ctx)
{
    // No scrollbars: the image is always drawn at exactly the panel's
    // available content size (see below), so it never overflows the panel
    // and a scrollbar would only ever be visual noise.
    //
    // ImGui::Begin() returns false when this panel isn't actually visible
    // right now (e.g. it's an inactive tab behind "Scene", or collapsed) -
    // stashed in ctx.gameViewVisible for
    // ImGuiEditorLayer::GameViewTarget() to read at the START of NEXT
    // frame (see its comment for why visibility can't safely be acted on
    // mid-frame here), so a hidden Game view skips being rendered into at
    // all next frame - see EditorContext::gameViewVisible.
    const bool isVisible =
        ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ctx.gameViewVisible = isVisible;

    if (isVisible) {
        // The panel's current content-region size, in pixels - this is what
        // the Game-view RenderTexture should become. Stored in ctx for
        // ImGuiEditorLayer::GameViewTarget() to apply at the very start of
        // next frame (see its comment for why it can't safely happen
        // mid-frame here).
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            ctx.desiredExtent.width = static_cast<std::uint32_t>(avail.x);
            ctx.desiredExtent.height = static_cast<std::uint32_t>(avail.y);

            // Always draw at exactly the panel's available size (never the
            // texture's own, possibly one-frame-stale, extent) - this is
            // what makes it behave like Unity's "Free Aspect" Game view:
            // the image fills the panel exactly, whatever size/aspect it's
            // resized to, with no leftover space and nothing overflowing
            // it.
            ImGui::Image(
                static_cast<ImTextureID>(reinterpret_cast<intptr_t>(ctx.gameViewDescriptor)),
                avail);
        }
    }
    ImGui::End();
}

} // namespace gte
