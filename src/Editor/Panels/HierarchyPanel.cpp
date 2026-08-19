#include "HierarchyPanel.h"

#include "../EditorContext.h"
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
        std::snprintf(label, sizeof(label), "Entity %u", entity.index);

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
