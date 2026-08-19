#include "InspectorPanel.h"

#include "../EditorContext.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/MeshRenderer.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"

#include <imgui.h>

namespace gte {

void BuildInspectorPanel(Registry& registry, EditorContext& ctx)
{
    ImGui::Begin("Inspector");

    if (!registry.IsAlive(ctx.selectedEntity)) {
        ImGui::TextDisabled("No entity selected.");
        ImGui::End();
        return;
    }

    ImGui::Text("Entity %u (generation %u)", ctx.selectedEntity.index, ctx.selectedEntity.generation);
    ImGui::Separator();

    if (Transform* transform = registry.TryGetComponent<Transform>(ctx.selectedEntity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Position", &transform->position.x, 0.01f);

            Vec3 eulerDegrees = transform->rotation.ToEulerDegrees();
            if (ImGui::DragFloat3("Rotation", &eulerDegrees.x, 0.1f)) {
                transform->rotation = Quat::FromEulerDegrees(eulerDegrees.x, eulerDegrees.y, eulerDegrees.z);
            }

            ImGui::DragFloat3("Scale", &transform->scale.x, 0.01f);
        }
    }

    if (MeshRenderer* meshRenderer = registry.TryGetComponent<MeshRenderer>(ctx.selectedEntity)) {
        if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginDisabled();
            ImGui::Text("Mesh handle:     index %u, generation %u",
                meshRenderer->mesh.index, meshRenderer->mesh.generation);
            ImGui::Text("Pipeline handle: index %u, generation %u",
                meshRenderer->pipeline.index, meshRenderer->pipeline.generation);
            ImGui::EndDisabled();
        }
    }

    if (Camera* camera = registry.TryGetComponent<Camera>(ctx.selectedEntity)) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Active", &camera->active);
            ImGui::DragFloat("Field of View (Y)", &camera->fovYDegrees, 0.5f, 1.0f, 179.0f);
            ImGui::DragFloat("Near Z", &camera->nearZ, 0.01f, 0.001f, camera->farZ - 0.01f);
            ImGui::DragFloat("Far Z", &camera->farZ, 1.0f, camera->nearZ + 0.01f);
        }
    }

    ImGui::End();
}

} // namespace gte
