#include "ScenePanel.h"

#include "../EditorCamera.h"
#include "../EditorContext.h"

#include <imgui.h>

#include <cstdint>

namespace gte {

void BuildScenePanel(EditorContext& ctx, EditorCamera& camera)
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

            // Unity-style Scene camera controls (see EditorCamera.h for the
            // actual pan/rotate/dolly math, deliberately kept ImGui-free) -
            // this is the one place that reads ImGui's own mouse state and
            // feeds it in as plain values.
            //
            // Middle/right-mouse drags only ever START responding while the
            // cursor is actually hovering the Scene image itself (so
            // clicking/dragging elsewhere in the Editor never moves this
            // camera), but once started, they keep responding even if the
            // cursor drifts outside the image mid-drag - ctx.sceneCameraPanning/
            // Rotating "captures" this across frames - ending only once the
            // corresponding button is released, exactly like Unity's own
            // Scene view (and like a normal OS drag gesture in general).
            const bool hovered = ImGui::IsItemHovered();

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                ctx.sceneCameraPanning = true;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
                ctx.sceneCameraPanning = false;
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ctx.sceneCameraRotating = true;
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                ctx.sceneCameraRotating = false;
            }

            const ImGuiIO& io = ImGui::GetIO();
            const Vec2 mouseDelta{ io.MouseDelta.x, io.MouseDelta.y };
            // The wheel only dollies the camera while actually hovering the
            // Scene image - otherwise scrolling e.g. the Hierarchy list
            // sitting right next to it would also dolly this camera.
            const float scrollDelta = hovered ? io.MouseWheel : 0.0f;

            camera.Update(mouseDelta, scrollDelta, ctx.sceneCameraPanning, ctx.sceneCameraRotating);
        }
    }
    ImGui::End();
}

} // namespace gte
