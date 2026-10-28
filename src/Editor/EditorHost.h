#pragma once

#include <memory>
#include <string>

#include "../Core/Core.h"
#include "../Core/EditorCapabilities.h"
#include "../Core/EngineContext.h"
#include "../Core/Plugins/IEngineFeatureModule.h"
#include "EditorHostServices.h"
#include "EditorLayer.h"
#include "../Game/Game.h"
#include "../Network/NetworkServer.h"
#include "../Renderer/DeviceIdleGuard.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/VolumeTexturePreviewRenderer.h"
#include "../Window/Window.h"
#include "../Application/AssetImportCommandBridge.h"
#include "../Application/EngineCommandBridge.h"
#include "../Application/EditorUiCommandBridge.h"
#include "../Application/FeatureDebugCommandBridge.h"
#include "../Application/FrameCaptureBridge.h"
#include "../Application/FrameDebuggerCommandBridge.h"
#include "../Application/RenderGraphControlCommandBridge.h"
#include "../Application/ProjectAssemblyHotReloadCommandBridge.h"
#include "../Application/ProjectLifecycleLoadCommandBridge.h"
#include "../Application/ProfilerSnapshotBridge.h"
#include "FrameDebuggerPassMetadataRecorder.h"

namespace gte {

// Editor composition root: owns SDL, the authoring Window, Core, the editor layer, the automation bridges
// and the embedded HTTP server. Run() is the main loop.
class EditorHost {
public:
    // `forceGpuValidation` turns the GPU API validation on in any build type.
    // `presentPolicy` Uncapped removes the vsync cap (benchmarks).
    EditorHost(const std::string& title, int width, int height, bool forceGpuValidation,
        PresentPolicy presentPolicy = PresentPolicy::Capped);
    ~EditorHost() = default;

    EditorHost(const EditorHost&) = delete;
    EditorHost& operator=(const EditorHost&) = delete;
    EditorHost(EditorHost&&) = delete;
    EditorHost& operator=(EditorHost&&) = delete;

    // Runs the main loop until the window is closed. Returns a process exit code.
    int Run();

private:
    // Answers at most one pending feature debug request on the main thread.
    void ServiceFeatureDebugCommand();

    // RAII guard for SDL_Init()/SDL_Quit(). Declared first so SDL outlives every SDL-owning member.
    struct SdlContext {
        SdlContext();
        ~SdlContext();

        SdlContext(const SdlContext&) = delete;
        SdlContext& operator=(const SdlContext&) = delete;
    };

    // Each member depends only on members declared before it; the editor layer hook is set in the constructor body.
    SdlContext m_sdlContext;
    Window m_window;
    EditorHostServices m_hostServices;
    Core m_core;

    // Must stay after m_core and before m_editorLayer: destruction runs in reverse order.
    std::vector<std::unique_ptr<IEngineFeatureModule>> m_builtinFeatureModules;

    // References to objects owned by m_core, for direct use in Run().
    Renderer& m_renderer;
    rg::RenderGraph& m_renderGraph;

    // Whole-session pass metadata sink and provider, installed on m_renderGraph in the constructor body.
    FrameDebuggerPassMetadataRecorder m_passMetadataRecorder;

    // Declared after Renderer/RenderGraph so it is destroyed before the Vulkan device goes away.
    std::unique_ptr<IEditorLayer> m_editorLayer;
    Game& m_game;
    EngineContext& m_engineContext;

    // Volume-texture raymarch preview for GET /get_texture. Lazy: costs nothing until first used.
    VolumeTexturePreviewRenderer m_volumeTexturePreviewRenderer;

    // Automation bridges. Declared before m_networkServer so their addresses can be handed to it.
    FrameCaptureBridge m_captureBridge;
    EngineCommandBridge m_commandBridge;
    EditorUiCommandBridge m_uiCommandBridge;
    FrameDebuggerCommandBridge m_frameDebuggerCommandBridge;
    AssetImportCommandBridge m_assetImportCommandBridge;
    // Backs the GET /render_graph/* mutation and discovery routes.
    RenderGraphControlCommandBridge m_renderGraphControlCommandBridge;
    // Backs the GET /feature_debug/* routes.
    FeatureDebugCommandBridge m_featureDebugCommandBridge;
    // Backs the GET /profiler routes.
    ProfilerSnapshotBridge m_profilerBridge;
    // Cross-thread hand-off for project assembly hot reload; not passed to NetworkServer's constructor.
    ProjectAssemblyHotReloadCommandBridge m_hotReloadCommandBridge;

    // Cross-thread hand-off for loading a project assembly, submitted only by the network-facing open method.
    ProjectLifecycleLoadCommandBridge m_projectLifecycleLoadCommandBridge;

    // Loopback-only HTTP server. Declared last so it is destroyed first.
    Network::NetworkServer m_networkServer;

    // Scene I/O capability wired in the constructor body; used when servicing engine commands.
    ISceneIOCapability* m_sceneIOCapability = nullptr;

    // Declared last, so it idles the GPU before any member above frees GPU objects (feature modules do not wait).
    DeviceIdleGuard m_gpuIdleGuard{ m_renderer };
};

} // namespace gte
