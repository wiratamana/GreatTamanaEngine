#include "HierarchyPanel.h"

#include "HierarchyRows.h"
#include "../EditorContext.h"
#include "../EditorFrame.h"
#include "../InstanceGridTestSpawner.h"
#include "../ImGuiUniqueId.h"
#include "../../Core/InstanceGridLimits.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"
#include "../../Game/Game.h"
#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gte {

namespace {

// Shapes offered by "Create 3D Object", in menu order.
constexpr std::array<PrimitiveType, 5> kCreatableShapes = {
    PrimitiveType::Cube,
    PrimitiveType::Sphere,
    PrimitiveType::Capsule,
    PrimitiveType::Cone,
    PrimitiveType::Plane,
};

// Row drop bands: the top and bottom fraction insert a sibling before/after the row; the middle reparents.
constexpr float kReorderBandFraction = 0.25f;

// Deeper rows are drawn at this indent level so a very deep chain keeps the window narrow.
constexpr std::uint32_t kMaxDrawnDepth = 64;

// Everything a row needs; the registry is only read while rows are drawn.
struct RowContext {
    EditorContext& ctx;
    Registry& registry;
    PendingHierarchyAction& pending;
};

// Row index of the entity being dragged out of this panel, if any.
std::optional<int> DraggedRowIndex(const std::vector<HierarchyRow>& rows)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload == nullptr || !payload->IsDataType(kHierarchyEntityDragDropPayloadType)
        || payload->DataSize != static_cast<int>(sizeof(Entity))) {
        return std::nullopt;
    }
    Entity dragged;
    std::memcpy(&dragged, payload->Data, sizeof(Entity));
    const auto found = std::find_if(rows.begin(), rows.end(), [dragged](const HierarchyRow& row) {
        return row.entity == dragged;
    });
    if (found == rows.end()) {
        return std::nullopt;
    }
    return static_cast<int>(found - rows.begin());
}

// Draws one flat row: selection, drag source and drop target. Drops only fill `pending`.
void DrawRow(RowContext& rc, const HierarchyRow& row)
{
    const Entity entity = row.entity;
    ScopedUniqueId idScope(static_cast<int>(entity.index), "HierarchyPanel::DrawRow");

    const std::string label = BuildEntityLabel(rc.registry, entity);
    const float indent = static_cast<float>(std::min(row.depth, kMaxDrawnDepth)) * ImGui::GetStyle().IndentSpacing;
    if (indent > 0.0f) {
        ImGui::Indent(indent);
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (!row.hasChildren) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (rc.ctx.selection.IsEntitySelected(entity)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (row.hasChildren) {
        ImGui::SetNextItemOpen(row.expanded, ImGuiCond_Always);
    }

    ImGui::TreeNodeEx(label.c_str(), flags);

    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();

    if (row.hasChildren && ImGui::IsItemToggledOpen()) {
        ToggleHierarchyCollapsed(rc.ctx.hierarchy, row);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        rc.ctx.selection.SelectEntity(entity);
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(kHierarchyEntityDragDropPayloadType, &entity, sizeof(Entity));
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyEntityDragDropPayloadType)) {
            Entity dragged;
            std::memcpy(&dragged, payload->Data, sizeof(Entity));
            if (dragged != entity && rc.registry.IsAlive(dragged)) {
                const float rowHeight = std::max(1.0f, itemMax.y - itemMin.y);
                const float relativeY = (ImGui::GetMousePos().y - itemMin.y) / rowHeight;

                PendingHierarchyAction action;
                action.dragged = dragged;
                action.target = entity;
                if (relativeY < kReorderBandFraction || relativeY > (1.0f - kReorderBandFraction)) {
                    action.kind = PendingHierarchyAction::Kind::Reorder;
                    action.targetParent = row.parent;
                    action.targetPosition = row.siblingPosition;
                    action.insertAfter = relativeY > 0.5f;
                } else {
                    action.kind = PendingHierarchyAction::Kind::Reparent;
                }
                rc.pending = std::move(action);
            }
        }

#if GTE_ENABLE_PROJECT_PANEL
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
            PendingHierarchyAction action;
            action.kind = PendingHierarchyAction::Kind::SpawnAsset;
            action.target = entity;
            action.assetPath = static_cast<const char*>(payload->Data);
            rc.pending = std::move(action);
        }
#endif

        ImGui::EndDragDropTarget();
    }

    if (indent > 0.0f) {
        ImGui::Unindent(indent);
    }
}

// Applies the drop recorded this frame. Runs after the row loop, so no row sees a half-changed tree.
void ApplyPendingAction(Game& game, Renderer& renderer, EditorContext& ctx, Registry& registry,
    DescriptorSetLayoutHandle sceneServicesSetLayout, const PendingHierarchyAction& action)
{
    switch (action.kind) {
    case PendingHierarchyAction::Kind::None:
        return;
    case PendingHierarchyAction::Kind::Reparent:
    case PendingHierarchyAction::Kind::Reorder:
        ApplyHierarchyAction(registry, ctx.hierarchy, action);
        return;
    case PendingHierarchyAction::Kind::SpawnAsset:
#if GTE_ENABLE_PROJECT_PANEL
        // An animation plays on a rigged model; any other asset spawns as a child of the target.
        if (registry.IsAlive(action.target) && !game.PlayAnimationOnEntity(action.target, action.assetPath)) {
            const Entity spawned = game.CreateMeshEntityFromGtaFile(renderer, action.assetPath, sceneServicesSetLayout);
            if (spawned.IsValid()) {
                SetParent(registry, spawned, action.target, /*worldPositionStays=*/true);
                ctx.selection.SelectEntity(spawned);
            }
        }
#else
        (void)game;
        (void)renderer;
        (void)sceneServicesSetLayout;
#endif
        return;
    }
}

} // namespace

void HierarchyPanel::OnHidden(EditorFrame& frame)
{
    NoteHierarchyHidden(frame.ctx.hierarchy);
}

void HierarchyPanel::OnGui(EditorFrame& frame)
{
    EditorContext& ctx = frame.ctx;
    Game& game = frame.game;
    Renderer& renderer = frame.renderer;
    const DescriptorSetLayoutHandle sceneServicesSetLayout = frame.sceneServicesSetLayout;
    HierarchyViewState& view = ctx.hierarchy;
    NoteHierarchyVisible(view);

    Registry& registry = game.GetRegistry();
    UpdateHierarchyRows(registry, view);

    PendingHierarchyAction pending;
    if (view.rows.empty()) {
        view.lastDrawnRows = 0;
        ImGui::TextDisabled("(no entities)");
    } else {
        RowContext rowContext{ ctx, registry, pending };
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(view.rows.size()));
        if (const std::optional<int> draggedRow = DraggedRowIndex(view.rows)) {
            clipper.IncludeItemByIndex(*draggedRow); // keeps the drag source submitted while scrolled away
        }
        std::uint32_t drawnRows = 0;
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                DrawRow(rowContext, view.rows[static_cast<std::size_t>(i)]);
                ++drawnRows;
            }
        }
        view.lastDrawnRows = drawnRows;
    }

    ApplyPendingAction(game, renderer, ctx, registry, sceneServicesSetLayout, pending);

    // Drop target for the empty area below the tree: an entity drop unparents it, an asset drop spawns at the root.
    // The invisible button gives the target a rect even when the tree is empty or short.
    {
        ImVec2 dropTargetSize = ImGui::GetContentRegionAvail();
        dropTargetSize.x = std::max(dropTargetSize.x, 1.0f);
        dropTargetSize.y = std::max(dropTargetSize.y, 1.0f);
        ImGui::InvisibleButton("HierarchyRootDropTarget", dropTargetSize);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kHierarchyEntityDragDropPayloadType)) {
                Entity dragged;
                std::memcpy(&dragged, payload->Data, sizeof(Entity));
                if (registry.IsAlive(dragged)) {
                    SetParent(registry, dragged, kInvalidEntity, /*worldPositionStays=*/true);
                }
            }
#if GTE_ENABLE_PROJECT_PANEL
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectAssetDragDropPayloadType)) {
                const std::string absolutePath(static_cast<const char*>(payload->Data));
                const Entity spawned = game.CreateMeshEntityFromGtaFile(renderer, absolutePath, sceneServicesSetLayout);
                if (spawned.IsValid()) {
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
}

} // namespace gte
