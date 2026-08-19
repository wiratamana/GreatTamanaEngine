#include "HierarchyPanel.h"

#include "../EditorContext.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"

#include <imgui.h>

#include <cstddef>
#include <cstdio>

namespace gte {

void BuildHierarchyPanel(Registry& registry, EditorContext& ctx)
{
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

    ImGui::End();
}

} // namespace gte
