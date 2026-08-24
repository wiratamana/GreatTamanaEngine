#pragma once

namespace gte {

class Registry;
class Renderer;
struct EditorContext;
#if GTE_ENABLE_PROJECT_PANEL
class AssetPreviewTexture;
class AssetPreviewMesh;
#endif

// Shows/edits whichever selection is currently "on top" - see
// ctx.selection.Kind() (Selection.h):
//   - Entity (ctx.selection.SelectedEntity(), from Hierarchy) - Transform
//     fields directly editable (position/scale as-is, rotation shown/edited
//     as Euler degrees and converted back to the stored Quat - see
//     Math/Quat.h's FromEulerDegrees()/ToEulerDegrees()); MeshRenderer's
//     handles are shown read-only (no asset-picker UI exists yet to let a
//     user reassign them).
//   - Asset (ctx.selection.SelectedAssetAbsolutePath(), from Project - only
//     reachable when GTE_ENABLE_PROJECT_PANEL is ON) - plain file/folder metadata
//     (name/extension/size/last-modified, via AssetInspectorData.h), PLUS
//     a live texture preview (via AssetPreviewTexture + `renderer`) if the
//     selected file decodes as a supported image, OR a live, auto-rotating
//     3D mesh preview (via AssetPreviewMesh + `renderer`) if it's a *.gta
//     AssetType::Mesh file (the result of importing a .pmx - see
//     src/Assets/AssetImporter.h); falls back to metadata-only for anything
//     else (a folder, a non-image/non-mesh file, or a file that fails to
//     decode/parse).
// Called once per frame by ImGuiEditorLayer::BuildUI(), after
// BuildHierarchyPanel(). `renderer`/`assetPreview`/`assetPreviewMesh` only
// exist in this signature when GTE_ENABLE_PROJECT_PANEL is ON - see
// AGENTS.md, "Editor Module Structure", for why this `#if` guard is one of
// the few GTE_ENABLE_PROJECT_PANEL touch points allowed outside
// ImGuiEditorLayer.cpp/DockLayout.cpp.
#if GTE_ENABLE_PROJECT_PANEL
void BuildInspectorPanel(
    Registry& registry, EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview, AssetPreviewMesh& assetPreviewMesh);
#else
void BuildInspectorPanel(Registry& registry, EditorContext& ctx);
#endif

} // namespace gte
