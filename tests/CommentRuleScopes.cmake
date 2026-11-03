# Scope lists for the CommentRules* tests: paths (relative to the scanned root) that must follow the
# comment rules. A path ending in "/" covers the whole folder.

set(GTE_COMMENT_RULE_SRC
    Renderer/ Core/FeatureKit/ Features/Shadow/ Features/Gtao/ Features/Ssgi/ Features/VolumetricLight/
    Features/ToneMapping/ Shaders/ Math/ToneMapCurves.h Game/Lighting/SunShadowReceiver.h
    Game/Lighting/SunLightModulation.h Application/RenderPasses.cpp Application/RenderPasses.h Core/Core.cpp
    Core/Core.h Core/CapturedPixels.h Core/ClusterClaim.h Core/ClusterClaim.cpp Core/ClusterCandidates.h
    Core/ClusterCandidates.cpp Encoding/CapturedPixelConversion.cpp Encoding/CapturedPixelConversion.h
    Encoding/DepthVisualization.cpp Encoding/DepthVisualization.h Encoding/HdrColorVisualization.cpp
    Encoding/HdrColorVisualization.h Encoding/SingleChannelVisualization.cpp Encoding/SingleChannelVisualization.h
    Editor/EditorHost.cpp Core/Plugins/RenderFeatureBlendPush.h Core/Plugins/RenderFeatureCompositor.cpp
    Core/Plugins/RenderFeatureCompositor.h Core/Plugins/RenderFeatureCompositorBlend.cpp
    Core/Plugins/RenderFeatureCompositorImpl.cpp Core/Plugins/RenderFeatureCompositorImpl.h
    Core/Plugins/RenderFeatureNamePool.h Editor/FrameDebuggerCapturedFrame.cpp Editor/FrameDebuggerCapturedFrame.h
    Editor/FrameDebuggerPreviewProcessing.cpp Editor/FrameDebuggerPreviewProcessing.h
    Editor/FrameDebuggerSideChannelChecker.cpp Editor/FrameDebuggerSideChannelChecker.h
    Editor/Panels/FrameDebuggerPanel.cpp Editor/Panels/FrameDebuggerPanel.h Editor/FrameDebuggerData.h
    Editor/FrameDebuggerData.cpp Editor/FrameDebuggerCoverageChecker.h Editor/FrameDebuggerCoverageChecker.cpp
    Editor/FrameDebuggerCoverageGuard.h Editor/FrameDebuggerCoverageGuard.cpp Editor/FrameDebuggerDrawRecording.cpp
    Editor/FrameDebuggerPassMetadataRecorder.h Editor/FrameDebuggerCaptureHandshake.h
    Application/FrameDebuggerCommandBridge.h Game/Animation/AnimationSystem.cpp Game/Animation/AnimationSystem.h
    Game/Animation/GpuSkinningRigCache.cpp Game/Animation/GpuSkinningRigCache.h Game/RenderSystem.cpp
    Game/RenderSystem.h Game/SceneDrawPass.cpp Game/SceneDrawPass.h Application/EngineCommandDispatch.cpp
    Application/EngineCommandDispatch.h Core/EditorCapabilities.h Core/Plugins/ProjectAssemblyHotReload.cpp
    Editor/DockLayout.cpp Editor/DockLayout.h Editor/EditorLayer.h Editor/EditorSceneIOCapability.cpp
    Editor/EditorSceneIOCapability.h Editor/InstanceGridTestSpawner.cpp Editor/InstanceGridTestSpawner.h
    Editor/ImGuiEditorLayer.cpp Editor/NullEditorLayer.cpp Editor/Panels/HierarchyPanel.cpp
    Editor/Panels/HierarchyPanel.h Editor/Panels/ScenePanel.cpp Editor/Panels/ScenePanel.h
    Editor/Panels/HierarchyRows.h Editor/Panels/HierarchyRows.cpp Editor/HierarchyStateReport.h
    Editor/HierarchyActionRequest.h ECS/HierarchySnapshot.h ECS/HierarchySnapshot.cpp ECS/EntityMarkSet.h
    Editor/HizDemoLayout.h Editor/HizDemoLayout.cpp Editor/HizDemoSceneOpener.h Editor/HizDemoSceneOpener.cpp
    Editor/HizDemoState.h Editor/HizDemoProfile.h Editor/Panels/OverlayLabel.h Core/EditorPanelRegistry.h
    Core/EditorPanelRegistry.cpp Editor/EditorWindow.h Editor/EditorWindow.cpp Editor/EditorFrame.h
    Editor/EditorContext.h Editor/Panels/GamePanel.h Editor/Panels/GamePanel.cpp Editor/Panels/MemoryPanel.h
    Editor/Panels/ProfilerPanel.h Editor/Panels/JobsPanel.h Editor/Panels/LogPanel.h Editor/Panels/InspectorPanel.h
    Editor/Panels/RenderFeaturesPanel.h Editor/Panels/RenderGraphPanel.h Editor/SceneIO.cpp Editor/SceneIO.h
    Game/Game.cpp Game/Game.h Game/Instantiation/MeshAssetGpuCatalog.cpp Game/Instantiation/MeshAssetGpuCatalog.h
    Game/Instantiation/MeshInstantiationSystem.cpp Game/Instantiation/MeshInstantiationSystem.h
    Game/Instantiation/PrimitiveGpuCatalog.cpp Game/Instantiation/PrimitiveGpuCatalog.h Scene/SceneBuilder.cpp
    Scene/SceneBuilder.h Core/FrameDebuggerEventSink.h Core/FrameDebuggerCaptureRecorder.h
    Application/RenderPassViewData.h Editor/FrameDebuggerCapture.cpp Editor/FrameDebuggerCapture.h
    Editor/FrameDebuggerReplayPasses.cpp Editor/AssetPreviewMesh.cpp Editor/AssetPreviewMesh.h
    Editor/BoneViewerWindow.cpp Editor/BoneViewerWindow.h Editor/EditorHost.h Editor/RenderGraphTexturePreview.cpp
    Editor/RenderGraphTexturePreview.h Core/Plugins/ProjectAssemblyHost.cpp Features/Atmosphere/
    Assets/MeshTangents.h Assets/MeshTangents.cpp Assets/AtomicFile.h Assets/AtomicFile.cpp Assets/GtaFile.h
    Assets/GtaFile.cpp Assets/MeshFile.h Assets/MeshFile.cpp Assets/MeshCluster.h Assets/Crc32.h
    Assets/MeshClusterValidation.h Assets/MeshClusterValidation.cpp Assets/MeshClusterRebase.h
    Assets/MeshClusterRebase.cpp Assets/MeshClustering.h Assets/MeshClustering.cpp Assets/MeshClusterPersistence.h
    Assets/MeshClusterPersistence.cpp Assets/MeshMaterialPartitioner.h Assets/MeshMaterialPartitioner.cpp
    Editor/MeshClusterBuildRunner.h Editor/MeshClusterBuildRunner.cpp Editor/ClusterToolUiState.h
    Editor/ClusterToolUiState.cpp Assets/MeshClusterClone.h Assets/MeshClusterClone.cpp Assets/MeshClusterOptions.h
    Editor/ViewportProjection.h Editor/ViewportProjection.cpp Editor/CameraFrustumGizmo.h
    Editor/CameraFrustumGizmo.cpp Editor/CameraPick.h Editor/CameraPick.cpp Editor/OverlayRaster.h
    Editor/OverlayRaster.cpp Editor/OrbitCamera.h Editor/OrbitCamera.cpp Editor/ClusterViewerSettings.h
    Editor/ClusterViewerSettings.cpp Editor/ClusterViewerLoad.h Editor/ClusterViewerLoad.cpp
    Editor/ClusterViewerStats.h Editor/ClusterViewerStats.cpp Editor/ClusterViewerTruth.h
    Editor/ClusterViewerTruth.cpp Editor/ClusterStatusText.h Editor/ClusterStatusText.cpp
    Editor/ClusterViewerControl.h Editor/SceneCameraControl.h Editor/ClusterViewerWindow.h
    Editor/ClusterViewerWindow.cpp Editor/ClusterViewerWindowInternal.h Editor/ClusterViewerWindowUi.cpp
    Editor/ClusterViewerWindowControl.cpp Editor/CameraFrustumOverlay.h Editor/CameraFrustumOverlay.cpp
    Editor/SceneCameraAdapter.h Editor/SceneCameraAdapter.cpp Editor/SceneCameraFrustum.h
    Editor/SceneCameraFrustum.cpp Editor/SceneOverlay.h Editor/SceneOverlay.cpp
    Game/Instantiation/MaterialTextureGpuCache.h Game/Instantiation/MaterialTextureGpuCache.cpp
    Game/Instantiation/MaterialCacheKeys.h Game/Instantiation/MaterialLifetimeRules.h
    Game/Instantiation/MaterialLifetimeRules.cpp Game/Instantiation/MeshAssetPipelineKind.h
    Game/Instantiation/MeshClusterTableSelection.h Game/Instantiation/MeshClusterTableSelection.cpp
    Game/Instantiation/MeshLifetimeRules.h Game/Instantiation/MeshLifetimeRules.cpp
    Game/Instantiation/MeshVertexPacking.h Game/Instantiation/MeshVertexPacking.cpp Math/PbrBrdf.h
    Editor/EditorCamera.h Editor/EditorCamera.cpp Editor/ExitGate.h Network/NetworkRequestGuard.h
    Network/NetworkRequestGuard.cpp Network/NetworkSceneCameraPose.h Network/NetworkSceneCameraPose.cpp
    Network/NetworkCaptureFailure.h Editor/CaptureFailureReason.h Core/FaultInjection.h Core/FaultInjection.cpp
    Core/RenderTimingAdapter.h Core/RenderTimingAdapter.cpp Core/GpuViewMetrics.h Core/GpuViewMetrics.cpp
    Network/NetworkFaultRoutes.h Network/NetworkFaultRoutes.cpp Network/NetworkProfilerRoutes.h
    Network/NetworkProfilerRoutes.cpp Network/NetworkProfilerTimeline.h Network/NetworkProfilerTimeline.cpp
    Application/ProfilerSnapshotBridge.h Application/ProfilerSnapshotBridge.cpp Application/GpuTimingText.h
    Editor/EditorViewNames.h Editor/RenderGraphDisplayText.h Editor/RenderGraphDisplayText.cpp
    Application/ProfilerScopeCatalog.h Application/ProfilerScopeCatalog.cpp Network/NetworkProfilerGraph.h
    Network/NetworkProfilerGraph.cpp Editor/ProfilerTableModel.h Editor/ProfilerTableModel.cpp
    Editor/ProfilerGraphModel.h Editor/ProfilerGraphModel.cpp Editor/ProfilerPanelData.h
    Editor/ProfilerPanelData.cpp Editor/Panels/ProfilerPanel.h Editor/Panels/ProfilerPanel.cpp
    Editor/Panels/ProfilerGraphWidget.h Editor/Panels/ProfilerGraphWidget.cpp Editor/Panels/ProfilerTableWidget.h
    Editor/Panels/ProfilerTableWidget.cpp Network/RenderGraphMetadataJson.h Network/RenderGraphMetadataJson.cpp
    Network/ViewWireName.h Editor/SceneViewAxes.h Editor/SceneViewAxes.cpp Editor/Panels/SceneViewGizmo.h
    Editor/Panels/SceneViewGizmo.cpp Editor/TransformGizmo.h Editor/TransformGizmo.cpp Profiling/
    Features/SceneLighting/ Features/GpuDrivenScene/ Features/Bloom/ Features/ToonCharacter/ Game/Lighting/ToonLook.h
    Game/Lighting/ToonClass.h Game/Instantiation/ToonMaterialClass.h Game/Instantiation/ToonMaterialClass.cpp
    Editor/FurinaShowcaseLayout.h Editor/FurinaShowcaseLayout.cpp Editor/FurinaShowcaseSceneOpener.h
    Editor/FurinaShowcaseSceneOpener.cpp Editor/PresetFeatureSettings.h Editor/PresetFeatureSettings.cpp
    Editor/PresetSceneSupport.h Editor/PresetSceneSupport.cpp ECS/Components/ToonShading.h)

set(GTE_COMMENT_RULE_TESTS
    Editor/SceneViewAxesTests.cpp Core/FeatureKit/ Core/GpuViewMetricsTests.cpp
    Editor/FrameDebuggerSideChannelCheckerTests.cpp Editor/FrameDebuggerCaptureHandshakeTests.cpp
    Editor/FrameDebuggerCapturedFrameTests.cpp Editor/FrameDebuggerCaptureTests.cpp
    Features/Shadow/SceneLightingUniformLayoutTests.cpp Features/Shadow/ShadowMaskParamsTests.cpp
    Features/Shadow/ShadowMathTests.cpp Features/Shadow/ShadowClusterLaneTests.cpp Game/SceneDrawPassTests.cpp
    Features/Gtao/GtaoMathTests.cpp Features/Gtao/GtaoParamsTests.cpp Features/Gtao/GtaoShaderNamesTests.cpp
    Features/Ssgi/ Features/VolumetricLight/ Features/ToneMapping/ Math/ToneMapCurvesTests.cpp
    Features/Shadow/ShadowReceiverTests.cpp Encoding/HdrColorVisualizationTests.cpp
    Editor/FrameDebuggerPreviewProcessingTests.cpp Renderer/RenderGraph/CommandBufferTests.cpp
    Renderer/RenderGraph/FullscreenPassTests.cpp Renderer/RenderGraph/FullscreenRecordTests.cpp
    Renderer/RenderGraph/GpuBufferTests.cpp Renderer/RenderGraph/OneShotTests.cpp
    Renderer/RenderGraph/PassKindRecordTests.cpp Renderer/RenderGraph/RealTargetBindingTests.cpp
    Renderer/RenderGraph/RecordedPassTests.cpp Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp
    Renderer/RenderGraph/ShaderBindingsTests.cpp Renderer/RenderGraph/TransferReadbackGpuTests.cpp
    Renderer/Vulkan/ShaderReflectionTests.cpp Core/Plugins/RenderFeatureBlendTests.cpp Tools/CppScannerTests.cpp
    Tools/HeaderPurityProbe.cpp Renderer/MaterialParamsTests.cpp Support/Std140Block.h
    Renderer/GpuSkinning/GpuSkinningPassTests.cpp Renderer/RenderGraph/RenderGraphComputeBindingKeyTests.cpp
    Renderer/RenderGraph/PassIdentityTests.cpp Renderer/RenderGraph/ResourcePoolEvictionTests.cpp
    Assets/MeshTangentsTests.cpp Renderer/UploadChunkerTests.cpp Renderer/MaterialImageRoleTests.cpp
    Game/Instantiation/MaterialLifetimeRulesTests.cpp Game/Instantiation/MaterialCacheKeysTests.cpp
    Game/Instantiation/MeshClusterTableSelectionTests.cpp Game/Instantiation/MeshLifetimeRulesTests.cpp
    Game/Instantiation/MeshAssetPipelineKindTests.cpp Game/MeshVertexPackingTests.cpp
    Core/FeatureKit/DepthOnlyLayoutTests.cpp Features/Shadow/ShadowDepthShaderCoverageTests.cpp
    Math/PbrBrdfTests.cpp Features/SceneLighting/ Features/GpuDrivenScene/
    Renderer/RenderGraph/DescriptorWriteIdentityTests.cpp Renderer/RenderGraph/TrackedBufferGpuTests.cpp
    Renderer/Culling/ClusterCullMathTests.cpp Assets/AtomicFileTests.cpp Encoding/R32FloatFormatTests.cpp
    Assets/MeshClusterTests.cpp Assets/MeshClusteringTests.cpp Assets/MeshMaterialPartitionerTests.cpp
    Assets/MeshClusterImportTests.cpp Editor/MeshClusterBuildRunnerTests.cpp Editor/ClusterToolUiStateTests.cpp
    Network/NetworkRoutesInspectorTests.cpp Network/NetworkRoutesClusterViewerTests.cpp
    Editor/HierarchyRowsTests.cpp ECS/HierarchySnapshotTests.cpp ECS/EntityMarkSetTests.cpp
    Scene/SceneHierarchyBulkTests.cpp Editor/HizDemoLayoutTests.cpp Core/EditorPanelRegistryTests.cpp
    Editor/EditorWindowTests.cpp Network/NetworkRoutesSceneCameraTests.cpp Core/ClusterClaimTests.cpp
    Core/ClusterPartSkipTests.cpp Core/Plugins/RenderFeatureCompositorOpaqueTests.cpp
    Core/ClusterCandidatesTests.cpp Core/ClusterDrawStateTests.cpp Core/ClusterDrawVerbTests.cpp
    Renderer/ClusterShaderTextTests.cpp Renderer/RenderGraph/IndirectDrawRecordTests.cpp
    Assets/MeshClusterCloneTests.cpp Renderer/ClusterTintTests.cpp Renderer/ClusterViewShaderTextTests.cpp
    Editor/CameraFrustumGizmoTests.cpp Editor/CameraPickTests.cpp Editor/OverlayRasterTests.cpp
    Editor/ViewportProjectionTests.cpp Editor/OrbitCameraTests.cpp Editor/ClusterViewerStatsTests.cpp
    Editor/ClusterViewerTruthTests.cpp Editor/ClusterViewerLoadTests.cpp Editor/ClusterViewerSettingsTests.cpp
    Editor/ClusterStatusTextTests.cpp Editor/SceneOverlayTests.cpp Editor/FrustumFromViewProjectionTests.cpp
    Editor/EditorCameraTests.cpp Editor/ExitGateTests.cpp Network/NetworkRequestGuardTests.cpp
    Renderer/AbandonedWorkTests.cpp Renderer/CaptureStatusTests.cpp Network/NetworkCaptureFailureTests.cpp
    Renderer/PresentRecoveryPlanTests.cpp Renderer/RenderGraph/OffscreenRecordingFaultTests.cpp
    Renderer/GpuStampPlanTests.cpp Renderer/GpuWaitServiceTests.cpp Network/NetworkFaultRoutesTests.cpp
    Renderer/GpuFrameRingTests.cpp Profiling/ Editor/RenderGraphDisplayTextTests.cpp
    Network/NetworkProfilerRoutesTests.cpp Renderer/GpuClockCalibrationTests.cpp
    Network/NetworkProfilerTimelineTests.cpp Application/ProfilerScopeCatalogTests.cpp
    Application/ProfilerScopeCatalogCoverageTests.cpp Network/NetworkProfilerGraphTests.cpp
    Editor/ProfilerTableModelTests.cpp Editor/ProfilerGraphModelTests.cpp Editor/ProfilerPanelDataTests.cpp
    Features/Bloom/ Features/ToonCharacter/ Game/Instantiation/ToonMaterialClassTests.cpp
    Editor/FurinaShowcaseLayoutTests.cpp Editor/FurinaShowcaseSceneOpenerTests.cpp
    ECS/Reflection/ToonShadingReflectionTests.cpp)
