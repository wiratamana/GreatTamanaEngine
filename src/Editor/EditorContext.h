#pragma once

#include "../ECS/Entity.h"

#include <volk.h>

namespace gte {

// Plain shared state passed by reference into every Editor panel/dock-layout
// function (see DockLayout.h, Panels/*.h) - the free-function equivalent of
// what used to be ImGuiEditorLayer's own private member variables before the
// panel split (see AGENTS.md, "Editor Module Structure"). Deliberately plain
// data with no behavior of its own, the same "plain data, no virtual
// behavior" philosophy AGENTS.md already applies to ECS components (see
// ECS/Components/Transform.h) - only ImGuiEditorLayer (the composition root)
// and the panel/dock-layout functions it calls ever touch this, in a fixed,
// explicit order every frame.
//
// Holding raw Vulkan types here (VkDescriptorSet, VkExtent2D) is deliberate,
// not an architectural leak: Renderer's own public API
// (RenderTexture::Extent(), Renderer::GetVulkanContextInfo()) already hands
// out plain Vulkan handles on purpose, specifically so "an external
// Vulkan-based rendering backend... owned by the Editor module" (see
// Renderer.h) - i.e. Dear ImGui's own Vulkan backend - can use them
// directly. See AGENTS.md ("Editor Module Structure") for the full
// rationale.
struct EditorContext {
    // The ImGui-side descriptor for the Game-view RenderTexture, created
    // lazily by ImGuiEditorLayer::BuildUI() - shared read-only by
    // ScenePanel and GamePanel (both currently display the same Game view -
    // see Panels/ScenePanel.h's "IMPORTANT LIMITATION" note). Never
    // interpreted by engine code, just handed straight to ImGui::Image() as
    // an opaque texture id.
    VkDescriptorSet gameViewDescriptor = VK_NULL_HANDLE;

    // Size (in pixels) the "Game" panel's content region actually was as of
    // last frame's GamePanel::Build() (Panels/GamePanel.cpp) - what
    // ImGuiEditorLayer::GameViewTarget() resizes the Game-view RenderTexture
    // to at the start of the next frame. Initialized by ImGuiEditorLayer's
    // constructor to the OS window's startup size, so the very first frame
    // (before BuildUI() has ever run) doesn't see a spurious mismatch
    // against the texture's own initial size.
    VkExtent2D desiredExtent{};

    // Hierarchy/Inspector selection state - kInvalidEntity means "nothing
    // selected", shown by InspectorPanel as "No entity selected." Written by
    // HierarchyPanel, read by InspectorPanel.
    Entity selectedEntity = kInvalidEntity;

    // Set by File > Exit (see DockLayout.cpp's BuildDockspaceAndMenuBar());
    // read once per frame by ImGuiEditorLayer::WantsExit().
    bool exitRequested = false;

    // Latches true the first time every one of Hierarchy/Inspector/Scene/
    // Game is confirmed to have a real dock (see DockLayout.cpp's
    // BuildDockspaceAndMenuBar() comment for the full one-shot rationale) -
    // once true, it never touches the docking system again for the rest of
    // the process, which is what lets the user freely drag/split/undock any
    // panel afterwards.
    bool dockLayoutEnsured = false;
};

} // namespace gte
