#include "HierarchyPanel.h"

#include "../EditorContext.h"
#include "../../ECS/Components/Camera.h"
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

// A row's own on-screen rect is split into three vertical bands for
// drag-and-drop purposes (see HierarchyPanel.h's own doc comment): the top
// and bottom kReorderBandFraction of the row mean "insert as a sibling
// before/after this row", the remaining middle band means "reparent as a
// child of this row".
constexpr float kReorderBandFraction = 0.25f;

// Builds this entity's own "Entity %u" (or "Entity %u (Camera)") label -
// the exact same text the old flat list used, just factored out so the
// recursive tree renderer below can reuse it for both the label and the
// drag tooltip.
std::string BuildEntityLabel(Registry& registry, Entity entity)
{
    char label[32];
    if (registry.HasComponent<Camera>(entity)) {
        std::snprintf(label, sizeof(label), "Entity %u (Camera)", entity.index);
    } else {
        std::snprintf(label, sizeof(label), "Entity %u", entity.index);
    }
    return std::string(label);
}

// Recursively renders `entity` and every descendant of it as an indented
// ImGui tree (ImGui::TreeNodeEx()), wiring up selection + the drag-and-drop
// attach/detach/reorder behavior described in HierarchyPanel.h's own doc
// comment. `registry`/`game`/`renderer`/`ctx` are threaded straight through
// from BuildHierarchyPanel() below.
void RenderEntityNode(Game& game, Renderer& renderer, EditorContext& ctx, Registry& registry, Entity entity)
{
    ImGui::PushID(static_cast<int>(entity.index));

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

    // This row's own on-screen rect, captured right after submitting it
    // (before any children get a chance to draw/become "the last item") -
    // used below both to decide which of the three reorder/reparent bands
    // a drop landed in, and as the drag SOURCE's own rect implicitly (via
    // BeginDragDropSource(), which always attaches to the last item).
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
            // Dropping an entity onto itself, or one of the ancestors it
            // could never legally become a child of anyway, is silently a
            // no-op - SetParent() below already rejects both cases (and a
            // stale/no-longer-alive `dragged` handle), so this is purely a
            // safety net against doing needless work, not correctness-load-
            // bearing on its own.
            if (dragged != entity && registry.IsAlive(dragged)) {
                if (relativeY < kReorderBandFraction || relativeY > (1.0f - kReorderBandFraction)) {
                    // Top/bottom band: reorder as a new sibling immediately
                    // before/after `entity`, under `entity`'s OWN parent
                    // (i.e. NOT necessarily reparenting - if `dragged`
                    // already shares that same parent, this is a pure
                    // reorder).
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
        // Same Project-panel-asset drag-and-drop as the panel-wide target
        // in BuildHierarchyPanel() below, but dropped directly onto a row
        // instead - spawns the asset as a CHILD of `entity` rather than at
        // the scene root, Unity's own "drag a model onto another GameObject
        // in Hierarchy to parent it" convention.
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
            const std::string absolutePath(static_cast<const char*>(payload->Data));
            const Entity spawned = game.CreateMeshEntityFromGtaFile(renderer, absolutePath);
            if (spawned.IsValid()) {
                SetParent(registry, spawned, entity, /*worldPositionStays=*/true);
                ctx.selection.SelectEntity(spawned);
            }
        }
#endif

        ImGui::EndDragDropTarget();
    }

    if (opened && !children.empty()) {
        for (const Entity child : children) {
            RenderEntityNode(game, renderer, ctx, registry, child);
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

} // namespace

void BuildHierarchyPanel(Game& game, Renderer& renderer, EditorContext& ctx)
{
    Registry& registry = game.GetRegistry();

    ImGui::Begin("Hierarchy");

    // Root entities: every entity with a Transform whose own parent is
    // kInvalidEntity (or, defensively, dangling - see GetChildren()'s own
    // doc comment) - the top level of the real parent/child tree, walked
    // recursively from here.
    const std::vector<Entity> roots = GetChildren(registry, kInvalidEntity);
    for (const Entity root : roots) {
        RenderEntityNode(game, renderer, ctx, registry, root);
    }

    if (roots.empty()) {
        ImGui::TextDisabled("(no entities)");
    }

    // Drop target for whatever's left of the panel BELOW the tree above -
    // dragging a Hierarchy entity here detaches it back to the scene root
    // (Unity's own "drag onto empty Hierarchy space to unparent"
    // convention); dragging a Project-panel asset here still spawns it at
    // the scene root, exactly as before. An invisible button spanning the
    // rest of the panel gives this a real rect to attach a drop target to -
    // BeginDragDropTarget() alone only ever attaches to the MOST RECENTLY
    // SUBMITTED item, and the entity tree above may be empty (or may not
    // fill the whole panel).
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
                const Entity spawned = game.CreateMeshEntityFromGtaFile(renderer, absolutePath);
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
                    ctx.selection.SelectEntity(game.CreatePrimitiveEntity(renderer, type));
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace gte
