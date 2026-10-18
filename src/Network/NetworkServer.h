#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

// Forward declarations keep this header cheap for consumers that only pass a NetworkServer around.
namespace httplib { class Server; }
namespace gte {
class FrameCaptureBridge;
class EngineCommandBridge;
class EditorUiCommandBridge;
class FrameDebuggerCommandBridge;
class AssetImportCommandBridge;
class ILogQueryCapability;
class RenderGraphControlCommandBridge;
class IHotReloadDebugCapability;
class IProjectLifecycleCapability;
class IAssetScaffoldingCapability;
class FeatureDebugCommandBridge;
} // namespace gte

namespace gte::Network {

// Embedded HTTP server (cpp-httplib), bound to loopback (127.0.0.1) only: no API can express another host.
// Start() returns at once; requests are served on a background thread owned by this class. The destructor
// calls Stop(), so the socket and the thread are never leaked.
class NetworkServer {
public:
    // Every constructor parameter is a defaulted, non-owning pointer; the owner outlives this server.
    // A null pointer makes the routes that need it answer 503 instead of crashing.
    // New parameters are appended last so existing call sites keep compiling.
    explicit NetworkServer(FrameCaptureBridge* captureBridge = nullptr,
        EngineCommandBridge* commandBridge = nullptr,
        EditorUiCommandBridge* uiCommandBridge = nullptr,
        FrameDebuggerCommandBridge* frameDebuggerCommandBridge = nullptr,
        AssetImportCommandBridge* assetImportCommandBridge = nullptr,
        ILogQueryCapability* logQueryCapability = nullptr,
        RenderGraphControlCommandBridge* renderGraphControlCommandBridge = nullptr,
        IHotReloadDebugCapability* hotReloadDebugCapability = nullptr,
        IProjectLifecycleCapability* projectLifecycleCapability = nullptr,
        IAssetScaffoldingCapability* assetScaffoldingCapability = nullptr,
        FeatureDebugCommandBridge* featureDebugCommandBridge = nullptr);
    ~NetworkServer();

    NetworkServer(const NetworkServer&) = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;
    NetworkServer(NetworkServer&&) = delete;
    NetworkServer& operator=(NetworkServer&&) = delete;

    // Binds 127.0.0.1:port and starts the serving thread. port == 0 picks a free port (see BoundPort()).
    // A second call while running is a no-op. A failed bind is logged and non-fatal: IsRunning() stays false.
    // Not thread-safe against itself or Stop(); call both from the main thread only.
    void Start(int port);

    // Stops the server and joins the thread. Safe when never started or already stopped.
    void Stop();

    bool IsRunning() const noexcept;

    // The bound TCP port: valid once IsRunning(), 0 before a successful Start() and after a failed bind.
    int BoundPort() const noexcept;

private:
    // Pimpl keeps httplib out of this header.
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<int> m_boundPort{ 0 };

    // Non-owning; see the constructor.
    FrameCaptureBridge* m_captureBridge = nullptr;
    EngineCommandBridge* m_commandBridge = nullptr;
    EditorUiCommandBridge* m_uiCommandBridge = nullptr;
    FrameDebuggerCommandBridge* m_frameDebuggerCommandBridge = nullptr;
    AssetImportCommandBridge* m_assetImportCommandBridge = nullptr;
    ILogQueryCapability* m_logQueryCapability = nullptr;
    RenderGraphControlCommandBridge* m_renderGraphControlCommandBridge = nullptr;
    IHotReloadDebugCapability* m_hotReloadDebugCapability = nullptr;
    IProjectLifecycleCapability* m_projectLifecycleCapability = nullptr;
    IAssetScaffoldingCapability* m_assetScaffoldingCapability = nullptr;
    FeatureDebugCommandBridge* m_featureDebugCommandBridge = nullptr;
};

} // namespace gte::Network
