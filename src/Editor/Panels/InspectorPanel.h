#pragma once

namespace gte {

class Registry;
class Renderer;
struct EditorContext;
#if GTE_ENABLE_PROJECT_PANEL
class AssetPreviewTexture;
#endif

// Shows/edits whichever selection is currently "on top" - see
// EditorContext::inspectorSelectionKind:
//   - Entity (ctx.selectedEntity, from Hierarchy) - Transform fields
//     directly editable (position/scale as-is, rotation shown/edited as
//     Euler degrees and converted back to the stored Quat - see
//     Math/Quat.h's FromEulerDegrees()/ToEulerDegrees()); MeshRenderer's
//     handles are shown read-only (no asset-picker UI exists yet to let a
//     user reassign them).
//   - Asset (ctx.selectedAssetAbsolutePath, from Project - only reachable
//     when GTE_ENABLE_PROJECT_PANEL is ON) - plain file/folder metadata
//     (name/extension/size/last-modified, via AssetInspectorData.h), PLUS
//     a live texture preview (via AssetPreviewTexture + `renderer`) if the
//     selected file decodes as a supported image; falls back to
//     metadata-only for anything else (a folder, a non-image file, or an
//     image file that fails to decode).
// Called once per frame by ImGuiEditorLayer::BuildUI(), after
// BuildHierarchyPanel(). `renderer`/`assetPreview` only exist in this
// signature when GTE_ENABLE_PROJECT_PANEL is ON - see AGENTS.md, "Editor
// Module Structure", for why this `#if` guard is one of the few
// GTE_ENABLE_PROJECT_PANEL touch points allowed outside ImGuiEditorLayer.cpp/
// DockLayout.cpp.
#if GTE_ENABLE_PROJECT_PANEL
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview);
#else
void BuildInspectorPanel(Registry& registry, EditorContext& ctx);
#endif

} // namespace gte
