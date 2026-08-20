#include "HierarchyPanel.h"

#include "../EditorContext.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../Game/Game.h"
#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdio>

namespace gte {

namespace {

// Every PrimitiveType offered by "Create 3D Object" below, in menu order -
// see PrimitiveMeshGenerator.h for the shapes themselves.
constexpr std::array<PrimitiveType, 5> kCreatableShapes = {
    PrimitiveType::Cube,
    PrimitiveType::Sphere,
    PrimitiveType::Capsule,
    PrimitiveType::Cone,
    PrimitiveType::Plane,
};

} // namespace

void BuildHierarchyPanel(Game& game, Renderer& renderer, EditorContext& ctx)
{
    Registry& registry = game.GetRegistry();

    ImGui::Begin("Hierarchy");

    ComponentStorage<Transform>& transforms = registry.Storage<Transform>();
    for (std::size_t i = 0; i < transforms.Size(); ++i) {
        const Entity entity = transforms.EntityAt(i);

        char label[32];
        // A small "(Camera)" suffix for entities that also carry a Camera
        // component (see ECS/Components/Camera.h) - purely cosmetic, so the
        // one entity driving the Game/Scene views is easy to spot in a
        // scene with several entities.
        if (registry.HasComponent<Camera>(entity)) {
            std::snprintf(label, sizeof(label), "Entity %u (Camera)", entity.index);
        } else {
            std::snprintf(label, sizeof(label), "Entity %u", entity.index);
        }

        const bool isSelected = (entity == ctx.selectedEntity);
        if (ImGui::Selectable(label, isSelected)) {
            ctx.selectedEntity = entity;
        }
    }

    if (transforms.Size() == 0) {
        ImGui::TextDisabled("(no entities)");
    }

    // Unity-style right-click context menu: BeginPopupContextWindow() with
    // no ImGuiPopupFlags_NoOpenOverItems opens on ANY right-click inside
    // this window - over an existing entry or empty space alike, since
    // there is no separate per-entity context menu yet (a per-entity menu,
    // e.g. "Delete"/"Rename", is a natural follow-up once this engine has a
    // Name component - see TODO.md).
    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu")) {
        if (ImGui::BeginMenu("Create 3D Object")) {
            for (const PrimitiveType type : kCreatableShapes) {
                if (ImGui::MenuItem(ToString(type))) {
                    // Select the freshly spawned entity immediately - same
                    // as Unity, so Inspector shows it without an extra
                    // click in Hierarchy.
                    ctx.selectedEntity = game.CreatePrimitiveEntity(renderer, type);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace gte
