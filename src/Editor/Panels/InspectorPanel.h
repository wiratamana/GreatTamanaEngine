#pragma once

#include "../ClusterToolUiState.h"
#include "../EditorWindow.h"

#include <string>

namespace gte {

struct EditorContext;
#if GTE_ENABLE_PROJECT_PANEL
class AssetPreviewTexture;
class AssetPreviewMesh;
class BoneViewerWindow;
class ModelRigCache;
#endif

// Shows what is selected: an entity, a Project asset or a Bone Viewer part.
class InspectorPanel final : public EditorWindow {
public:
#if GTE_ENABLE_PROJECT_PANEL
    InspectorPanel(AssetPreviewTexture& assetPreview, AssetPreviewMesh& assetPreviewMesh, BoneViewerWindow& boneViewer,
        ModelRigCache& rigCache)
        : EditorWindow("Inspector", WindowDock::Right)
        , m_assetPreview(assetPreview)
        , m_assetPreviewMesh(assetPreviewMesh)
        , m_boneViewer(boneViewer)
        , m_rigCache(rigCache)
    {
    }
#else
    InspectorPanel() : EditorWindow("Inspector", WindowDock::Right) {}
#endif

protected:
    void OnFrame(EditorFrame& frame) override;
    void OnGui(EditorFrame& frame) override;

private:
#if GTE_ENABLE_PROJECT_PANEL
    AssetPreviewTexture& m_assetPreview;
    AssetPreviewMesh& m_assetPreviewMesh;
    BoneViewerWindow& m_boneViewer;
    ModelRigCache& m_rigCache;
#endif
};

// Starts a cluster build of the mesh asset at `absolutePath` with the options in ctx.clusterTool.
// Used by the "Build Clusters" button and by the HTTP route, so both take the same path.
ClusterBuildStartResult StartInspectorClusterBuild(EditorContext& ctx, const std::string& absolutePath);

} // namespace gte
