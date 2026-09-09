#include "NetworkServer.h"

#include "NetworkRoutes.h"

#include "../Application/FrameCaptureBridge.h"
#include "../Encoding/Base64.h"

#include <httplib.h>

#include <cstdio>

// API verified directly against the actually-vendored
// third_party/httplib/httplib.h (pinned at tag v0.54.1 - see
// third_party/httplib/.gte_fetched_ref) before writing this file, per
// PHASE2_NETWORK_SERVER_BACKGROUND_THREAD_LIFECYCLE.md's own Step 2 caveat:
// bool bind_to_port(host, port, socket_flags = 0); int bind_to_any_port(host,
// socket_flags = 0); bool listen_after_bind(); bool is_running() const; void
// stop() noexcept; - every signature matches what this file assumes exactly,
// no adjustment needed. get_param_value()/get_header_value() (Request) are
// likewise confirmed present (network-impl-2 campaign, Phase 3).

namespace gte::Network {

namespace {

// Loopback-only, always - see PHASE0_MASTER_STRATEGY.md's locked "bind
// address" decision and this file's own class doc comment. The ONLY place
// this literal appears - Start()'s signature has no host parameter to
// smuggle a different value in through.
constexpr const char* kBindHost = "127.0.0.1";
// network-impl-2 campaign, Phase 5
// (PHASE5_GET_SWAPCHAIN_ENDPOINT_AND_FORMAT_NEGOTIATION_REUSE.md) - the one
// shared route-registration helper behind BOTH /get_game_view (Phase 3) and
// /get_swapchain (this phase), extracted once a SECOND, byte-for-byte-
// identical-apart-from-`path`/`kind` call site genuinely existed (see
// PHASE5's own Step 3.2 - the same "extract only once a real second caller
// needs it" convention this codebase already applies elsewhere, e.g.
// AGENTS.md's BonePoseMath.h precedent). Reuses
// ResolveCaptureResponseFormat()/BuildCaptureJsonBody() (NetworkRoutes.h,
// Phase 3) completely unchanged.
void RegisterCaptureRoute(httplib::Server& server, const char* path, FrameCaptureKind kind, FrameCaptureBridge* captureBridge)
{
    server.Get(path, [captureBridge, kind](const httplib::Request& req, httplib::Response& res) {
        if (captureBridge == nullptr) {
            res.status = 503;
            res.set_content("capture bridge not available", "text/plain; charset=utf-8");
            return;
        }
        const FrameCaptureBridge::RequestResult result = captureBridge->RequestCaptureAndWait(kind);
        if (result.alreadyPending) {
            res.status = 503;
            res.set_content("capture already in progress", "text/plain; charset=utf-8");
            return;
        }
        if (result.failure.has_value()) {
            res.status = (*result.failure == FrameCaptureFailureReason::TimedOut) ? 504 : 409;
            res.set_content("capture failed", "text/plain; charset=utf-8");
            return;
        }
        const CapturedPngImage& image = *result.image;
        const CaptureResponseFormat format =
            ResolveCaptureResponseFormat(req.get_param_value("format"), req.get_header_value("Accept"));
        if (format == CaptureResponseFormat::RawPng) {
            res.set_content(
                reinterpret_cast<const char*>(image.pngBytes.data()), image.pngBytes.size(), "image/png");
        } else {
            const std::string base64 = Encoding::EncodeBase64(image.pngBytes);
            res.set_content(BuildCaptureJsonBody(image.width, image.height, base64), "application/json");
        }
    });
}

// The one, hand-written route table for this campaign - see
// PHASE0_MASTER_STRATEGY.md's locked "Endpoint contract". A future endpoint
// is added here as one more server.Get(...)/Post(...) line, forwarding to
// its own NetworkRoutes.h function - never composing response text inline
// in this lambda.
void RegisterRoutes(httplib::Server& server, FrameCaptureBridge* captureBridge)
{
    server.Get("/http_hello_world", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(HandleHelloWorld(), "text/plain; charset=utf-8");
    });

    // network-impl-2 campaign, Phase 3
    // (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) /
    // Phase 5 (PHASE5_GET_SWAPCHAIN_ENDPOINT_AND_FORMAT_NEGOTIATION_REUSE.md)
    // - the two engine-state-touching endpoints, both wired through this one
    // shared helper (identical apart from the path/FrameCaptureKind). See
    // AGENTS.md, "Networking", for why a route handler is allowed to touch
    // FrameCaptureBridge and nothing else engine-side.
    RegisterCaptureRoute(server, "/get_game_view", FrameCaptureKind::GameView, captureBridge);
    RegisterCaptureRoute(server, "/get_swapchain", FrameCaptureKind::Swapchain, captureBridge);
}

} // namespace

struct NetworkServer::Impl {
    httplib::Server server;
};

NetworkServer::NetworkServer(FrameCaptureBridge* captureBridge)
    : m_impl(std::make_unique<Impl>())
    , m_captureBridge(captureBridge)
{
    // Registered exactly ONCE per NetworkServer instance, here in the
    // constructor - never inside Start() - so a Start()/Stop()/Start()
    // restart cycle (or a Start() that overlaps a failed bind retry) can
    // NEVER re-register the same route handler onto the same
    // httplib::Server a second time.
    RegisterRoutes(m_impl->server, m_captureBridge);
}

NetworkServer::~NetworkServer()
{
    Stop();
}

void NetworkServer::Start(int port)
{
    if (m_running.load()) {
        return; // Already running - safe no-op, see header comment.
    }

    const int resolvedPort = (port == 0)
        ? m_impl->server.bind_to_any_port(kBindHost)
        : (m_impl->server.bind_to_port(kBindHost, port) ? port : -1);

    if (resolvedPort < 0) {
        std::fprintf(stderr, "NetworkServer: failed to bind %s:%d - network endpoint disabled this run.\n",
            kBindHost, port);
        return; // Non-fatal - see header comment.
    }

    m_boundPort.store(resolvedPort);
    m_running.store(true);

    m_thread = std::thread([this]() {
        m_impl->server.listen_after_bind();
        m_running.store(false);
    });

    std::fprintf(stdout, "NetworkServer: listening on %s:%d\n", kBindHost, resolvedPort);
}

void NetworkServer::Stop()
{
    if (m_impl) {
        m_impl->server.stop();
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running.store(false);
    m_boundPort.store(0);
}

bool NetworkServer::IsRunning() const noexcept { return m_running.load(); }
int NetworkServer::BoundPort() const noexcept { return m_boundPort.load(); }

} // namespace gte::Network
