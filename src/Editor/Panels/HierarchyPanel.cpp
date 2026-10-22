#include "HierarchyPanel.h"

#include "../EditorContext.h"
#include "../InstanceGridTestSpawner.h"
#include "../ImGuiUniqueId.h"
#include "../../Core/InstanceGridLimits.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/Name.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"
#include "../../Game/Game.h"
#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

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

// Row drop bands: the top and bottom fraction insert as a sibling before/after the row; the middle reparents as a child.
constexpr float kReorderBandFraction = 0.25f;

// Display label: the Name component value when set, else "Entity %u" (with " (Camera)" for cameras).
std::string BuildEntityLabel(Registry& registry, Entity entity)
{
    if (const Name* name = registry.TryGetComponent<Name>(entity); name != nullptr && !name->value.empty()) {
        return registry.HasComponent<Camera>(entity) ? (name->value + " (Camera)") : name->value;
    }

    char label[32];
    if (registry.HasComponent<Camera>(entity)) {
        std::snprintf(label, sizeof(label), "Entity %u (Camera)", entity.index);
    } else {
        std::snprintf(label, sizeof(label), "Entity %u", entity.index);
    }
    return std::string(label);
}

// Renders `entity` and its descendants as an ImGui tree with selection and drag-and-drop reparenting.
void RenderEntityNode(Game& game, Renderer& renderer, EditorContext& ctx, Registry& registry, Entity entity,
    DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    ScopedUniqueId idScope(static_cast<int>(entity.index), "HierarchyPanel::RenderEntityNode");

    const std::string label = BuildEntityLabel(registry, entity);
    const std::vector<Entity> children = GetChildren(registry, entity);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (ctx.selection.IsEntitySelected(entity)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);

    // Row rect, captured before children draw; picks the drop band, and the drag source attaches to this item.
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ctx.selection.SelectEntity(entity);
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(kHierarchyEntityDragDropPayloadType, &entity, sizeof(Entity));
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        const float rowHeight = std::max(1.0f, itemMax.y - itemMin.y);
        const float relativeY = (ImGui::GetMousePos().y - itemMin.y) / rowHeight;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyEntityDragDropPayloadType)) {
            const Entity dragged = *static_cast<const Entity*>(payload->Data);
            // Dropping onto itself is a no-op; SetParent() also rejects cycles and dead handles.
            if (dragged != entity && registry.IsAlive(dragged)) {
                if (relativeY < kReorderBandFraction || relativeY > (1.0f - kReorderBandFraction)) {
                    // Top/bottom band: insert as a sibling before/after `entity` under its parent
                    // (a pure reorder when `dragged` already shares that parent).
                    const Transform* targetTransform = registry.TryGetComponent<Transform>(entity);
                    const Entity newParent = targetTransform != nullptr ? targetTransform->parent : kInvalidEntity;
                    const std::uint32_t targetIndex = targetTransform != nullptr ? targetTransform->siblingIndex : 0;
                    const std::uint32_t desiredIndex = (relativeY < kReorderBandFraction) ? targetIndex : targetIndex + 1;

                    if (SetParent(registry, dragged, newParent, /*worldPositionStays=*/true)) {
                        SetSiblingIndex(registry, dragged, desiredIndex);
                    }
                } else {
                    // Middle band: attach `dragged` as `entity`'s newest
                    // (last) child.
                    SetParent(registry, dragged, entity, /*worldPositionStays=*/true);
                }
            }
        }

#if GTE_ENABLE_PROJECT_PANEL
        // Project-panel asset dropped on a row: an animation plays on a rigged model; otherwise a mesh
        // spawns as a child of `entity`. PlayAnimationOnEntity() is tried first and fails for non-animations.
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
            const std::string absolutePath(static_cast<const char*>(payload->Data));
            if (!game.PlayAnimationOnEntity(entity, absolutePath)) {
                const Entity spawned = game.CreateMeshEntityFromGtaFile(renderer, absolutePath, sceneServicesSetLayout);
                if (spawned.IsValid()) {
                    SetParent(registry, spawned, entity, /*worldPositionStays=*/true);
                    ctx.selection.SelectEntity(spawned);
                }
            }
        }
#endif

        ImGui::EndDragDropTarget();
    }

    if (opened && !children.empty()) {
        for (const Entity child : children) {
            RenderEntityNode(game, renderer, ctx, registry, child, sceneServicesSetLayout);
        }
        ImGui::TreePop();
    }

    // No manual ImGui::PopID(); anymore - ScopedUniqueId's destructor handles it.
}

} // namespace

void BuildHierarchyPanel(Game& game, Renderer& renderer, EditorContext& ctx, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    Registry& registry = game.GetRegistry();

    ImGui::Begin("Hierarchy");

    // Top-level entities: those whose parent is kInvalidEntity (or dangling).
    const std::vector<Entity> roots = GetChildren(registry, kInvalidEntity);
    for (const Entity root : roots) {
        RenderEntityNode(game, renderer, ctx, registry, root, sceneServicesSetLayout);
    }

    if (roots.empty()) {
        ImGui::TextDisabled("(no entities)");
    }

    // Drop target for the empty area below the tree: an entity drop unparents it, an asset drop spawns at the root.
    // The invisible button gives the target a rect even when the tree is empty or short.
    {
        ImVec2 dropTargetSize = ImGui::GetContentRegionAvail();
        dropTargetSize.x = std::max(dropTargetSize.x, 1.0f);
        dropTargetSize.y = std::max(dropTargetSize.y, 1.0f);
        ImGui::InvisibleButton("HierarchyRootDropTarget", dropTargetSize);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyEntityDragDropPayloadType)) {
                const Entity dragged = *static_cast<const Entity*>(payload->Data);
                if (registry.IsAlive(dragged)) {
                    SetParent(registry, dragged, kInvalidEntity, /*worldPositionStays=*/true);
                }
            }
#if GTE_ENABLE_PROJECT_PANEL
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
                const std::string absolutePath(static_cast<const char*>(payload->Data));
                const Entity spawned = game.CreateMeshEntityFromGtaFile(renderer, absolutePath, sceneServicesSetLayout);
                if (spawned.IsValid()) {
                    // Same "select what you just created" convention as
                    // "Create 3D Object" below.
                    ctx.selection.SelectEntity(spawned);
                }
            }
#endif
            ImGui::EndDragDropTarget();
        }
    }

    // Right-click anywhere in the window opens the create menu.
    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu")) {
        if (ImGui::BeginMenu("Create 3D Object")) {
            for (const PrimitiveType type : kCreatableShapes) {
                if (ImGui::MenuItem(ToString(type))) {
                    // Select the freshly spawned entity immediately - same
                    // as Unity, so Inspector shows it without an extra
                    // click in Hierarchy.
                    ctx.selection.SelectEntity(game.CreatePrimitiveEntity(renderer, type, sceneServicesSetLayout));
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Create Directional Light")) {
            ctx.selection.SelectEntity(game.CreateDirectionalLightEntity());
        }
        // Spawns a grid of instances sharing one clustered mesh.
        if (ImGui::MenuItem("Create Instance Grid")) {
            InstanceGridTestSpawner::Spawn(game, renderer, ctx.instanceGrid, kDefaultSpawnGridInstances,
                kDefaultSpawnGridSpacing, sceneServicesSetLayout);
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace gte
