#include "NetworkServer.h"

#include "NetworkRoutes.h"

#include "../Application/AssetImportCommandBridge.h"
#include "../Application/EditorUiCommandBridge.h"
#include "../Application/EngineCommandBridge.h"
#include "../Application/FrameCaptureBridge.h"
#include "../Application/FrameDebuggerCommandBridge.h"
// editor-core-separation-8 campaign, PHASE5
// (PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md) - the new
// RenderGraphControlCommandBridge, and the 6 new GET /render_graph/* routes
// registered below.
#include "../Application/RenderGraphControlCommandBridge.h"
#include "../Core/EditorCapabilities.h" // ILogQueryCapability - editor-core-separation-2 campaign, PHASE3.
#include "../Core/Logging.h"
// editor-core-separation-2 campaign, PHASE3
// (PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md) - this file no
// longer #includes Editor/Logger.h at all. GET /get_logs and POST
// /clear_logs (below) now call through the nullable ILogQueryCapability*
// bridge (Core/EditorCapabilities.h, included above) instead of calling
// gte::Logger::Query()/Clear()/EntryCount()/IsEnabled()/LatestEntryId()
// directly - closing the real, pre-existing gte_core -> gte_editor-only-
// symbol dependency editor-core-separation-1 left open (see that
// campaign's own CAMPAIGN_COMPLETION_REPORT.md, "What remains genuinely
// open", option (b), and this campaign's own PHASE0_MASTER_STRATEGY.md,
// Defect C).
#include "../Encoding/Base64.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

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

// network-impl-4 campaign, Phase 5
// (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md) -
// GET /get_texture. Deliberately NOT shoehorned into RegisterCaptureRoute()
// above - see this phase document's own Step 2 for why the two have
// diverged enough (extra required query param, extra failure mode, richer
// JSON response) that a shared helper would need more parameters/branches
// than it saves.
void RegisterGetTextureRoute(httplib::Server& server, FrameCaptureBridge* captureBridge)
{
    server.Get("/get_texture", [captureBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedGetTextureQuery parsed =
            ParseGetTextureQuery(req.get_param_value("texture_name"), req.get_param_value("channel"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (captureBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("capture bridge not available"), "application/json");
            return;
        }

        const DebugTextureChannel channel = parsed.wantsDepth ? DebugTextureChannel::Depth : DebugTextureChannel::Color;
        const FrameCaptureBridge::RequestResult result =
            captureBridge->RequestCaptureAndWait(FrameCaptureKind::NamedTexture, 3000, parsed.textureName, channel);

        if (result.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson(
                "a /get_texture (or another named-texture) capture is already in progress"), "application/json");
            return;
        }
        if (result.failure.has_value()) {
            // TimedOut -> this exact texture_name never registered (or
            // never rendered again) within the timeout -> 504.
            // TargetNotAvailable -> a positively-known "no depth buffer on
            // this texture" (Phase 4's own fast-fail) or an unrecognized
            // depth format (Phase 3's own accepted narrow risk) -> 409. Each
            // gets its OWN, distinct, actionable message - never one shared
            // ambiguous sentence for both.
            if (*result.failure == FrameCaptureFailureReason::TimedOut) {
                res.status = 504;
                res.set_content(BuildGenericErrorResponseJson(
                    "texture_name '" + parsed.textureName + "' was never registered (or never rendered again) "
                    "within the timeout - see GET /list_textures for the currently known names"),
                    "application/json");
            } else {
                res.status = 409;
                res.set_content(BuildGenericErrorResponseJson(
                    "requested channel is not available for texture_name '" + parsed.textureName +
                    "' - it either has no depth buffer, or its depth format could not be visualized"),
                    "application/json");
            }
            return;
        }

        const CapturedPngImage& image = *result.image;
        const CaptureResponseFormat format =
            ResolveCaptureResponseFormat(req.get_param_value("format"), req.get_header_value("Accept"));
        if (format == CaptureResponseFormat::RawPng) {
            res.set_content(reinterpret_cast<const char*>(image.pngBytes.data()), image.pngBytes.size(), "image/png");
        } else {
            const std::string base64 = Encoding::EncodeBase64(image.pngBytes);
            res.set_content(BuildTextureCaptureJsonBody(image.width, image.height, base64, image.framesSinceUpdate),
                "application/json");
        }
    });
}

// network-impl-4 campaign, Phase 5 - GET /list_textures, the discoverability
// companion to /get_texture. Needs NO RenderGraph/rg::-namespaced type or
// header at all - Application.cpp (Phase 5, Step 3.4) already resolved
// everything down to plain PublishedTextureListEntry scalars.
void RegisterListTexturesRoute(httplib::Server& server, FrameCaptureBridge* captureBridge)
{
    server.Get("/list_textures", [captureBridge](const httplib::Request&, httplib::Response& res) {
        if (captureBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("capture bridge not available"), "application/json");
            return;
        }

        const std::vector<PublishedTextureListEntry> published = captureBridge->GetPublishedTextureList();
        std::vector<TextureListEntryView> views;
        views.reserve(published.size());
        for (const PublishedTextureListEntry& entry : published) {
            // Trivial 1:1 field copy - the ONE place this campaign
            // deliberately keeps two nearly-identical structs (see
            // PublishedTextureListEntry's own doc comment, FrameCaptureBridge.h,
            // Step 3.3, for why they are not the same type).
            views.push_back(TextureListEntryView{
                entry.name, entry.regime, entry.format, entry.width, entry.height, entry.hasDepth,
                entry.framesSinceUpdate, entry.kind, entry.depth });
        }
        res.set_content(BuildListTexturesResponseJson(views), "application/json");
    });
}

// editor-core-separation-7 campaign, PHASE4 - GET /render_graph. No query
// parameters (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8). Unlike
// RegisterListTexturesRoute() above, this DOES need the RenderGraph
// (gte::rg::)-namespaced type - FrameCaptureBridge::GetPublishedRenderGraphMetadata()
// already returns the fully-resolved gte::rg::RenderGraphMetadata this
// route's own response builder needs as-is (see NetworkRoutes.h's own doc
// comment on BuildRenderGraphMetadataResponseJson()).
void RegisterRenderGraphRoute(httplib::Server& server, FrameCaptureBridge* captureBridge)
{
    server.Get("/render_graph", [captureBridge](const httplib::Request&, httplib::Response& res) {
        if (captureBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("capture bridge not available"), "application/json");
            return;
        }
        const gte::rg::RenderGraphMetadata metadata = captureBridge->GetPublishedRenderGraphMetadata();
        res.set_content(BuildRenderGraphMetadataResponseJson(metadata), "application/json");
    });
}

// task_manager/frame-debugger-3 campaign, PHASE7
// (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - copies a
// real FrameDebuggerStateOutcome (src/Application/FrameDebuggerCommandBridge.h)
// into NetworkRoutes.h's own, completely independent
// FrameDebuggerStateResponseView, one field at a time - the same "a struct
// crossing a layer boundary is never accepted directly by NetworkRoutes.h"
// convention TextureListEntryView's own doc comment already establishes.
FrameDebuggerStateResponseView ToFrameDebuggerStateResponseView(const FrameDebuggerStateOutcome& outcome)
{
    FrameDebuggerStateResponseView view;
    view.enabled = outcome.enabled;
    view.windowOpen = outcome.windowOpen;
    view.hasCapturedFrame = outcome.hasCapturedFrame;
    view.totalEventCount = outcome.totalEventCount;
    view.selectedEventIndex = outcome.selectedEventIndex;
    view.channel = outcome.channel;
    view.levelsBlack = outcome.levelsBlack;
    view.levelsWhite = outcome.levelsWhite;
    return view;
}

// Shared tail for every /frame_debugger/* route EXCEPT /state (which needs
// its own, slightly different 200-only-on-success-read shape - see that
// route's own lambda below): maps alreadyPending/timedOut/outcome.success
// to a status code + BuildFrameDebuggerCommandResponseJson(), exactly
// mirroring /activate_tab's own alreadyPending/timedOut -> 503/504 mapping
// immediately above.
void RespondWithFrameDebuggerCommandResult(
    httplib::Response& res, const FrameDebuggerCommandBridge::SubmitResult& submit)
{
    if (submit.alreadyPending) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("another frame debugger command is already in progress"), "application/json");
        return;
    }
    if (submit.timedOut) {
        res.status = 504;
        res.set_content(BuildGenericErrorResponseJson("frame debugger command timed out"), "application/json");
        return;
    }

    const FrameDebuggerCommandResult& result = *submit.result;
    const FrameDebuggerStateResponseView stateView = ToFrameDebuggerStateResponseView(result.state);
    res.status = result.success ? 200 : 409;
    res.set_content(BuildFrameDebuggerCommandResponseJson(result.success,
        result.success ? "" : "frame debugger command could not be applied - see the reported state for why", stateView),
        "application/json");
}

// editor-core-separation-8 campaign, PHASE5
// (PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md) - shared tail for
// every /render_graph/* MUTATION route (set_pass_enabled/
// set_feature_enabled/set_feature_priority/set_blur_enabled/
// set_gbuffer_enabled) - mirrors RespondWithFrameDebuggerCommandResult()'s
// own exact alreadyPending/timedOut/outcome.success mapping immediately
// above. GET /render_graph/passes does NOT use this helper - it is
// read-only and has its own response shape (mirrors /frame_debugger/state's
// own special-cased handling).
void RespondWithRenderGraphControlCommandResult(
    httplib::Response& res, const RenderGraphControlCommandBridge::SubmitResult& submit)
{
    if (submit.alreadyPending) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("another render graph control command is already in progress"), "application/json");
        return;
    }
    if (submit.timedOut) {
        res.status = 504;
        res.set_content(BuildGenericErrorResponseJson("render graph control command timed out"), "application/json");
        return;
    }
    const RenderGraphControlCommandResult& result = *submit.result;
    res.status = result.success ? 200 : 409;
    res.set_content(BuildRenderGraphControlCommandResponseJson(result.success, result.errorMessage), "application/json");
}

// The one, hand-written route table for this campaign - see
// PHASE0_MASTER_STRATEGY.md's locked "Endpoint contract". A future endpoint
// is added here as one more server.Get(...)/Post(...) line, forwarding to
// its own NetworkRoutes.h function - never composing response text inline
// in this lambda.
void RegisterRoutes(httplib::Server& server, FrameCaptureBridge* captureBridge, EngineCommandBridge* commandBridge,
    EditorUiCommandBridge* uiCommandBridge, FrameDebuggerCommandBridge* frameDebuggerCommandBridge,
    AssetImportCommandBridge* assetImportCommandBridge, ILogQueryCapability* logQueryCapability,
    RenderGraphControlCommandBridge* renderGraphControlCommandBridge, IHotReloadDebugCapability* hotReloadDebugCapability,
    IProjectLifecycleCapability* projectLifecycleCapability)
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

    // network-impl-4 campaign, Phase 5
    // (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md) -
    // GET /get_texture (any named render-graph texture, by name) and its
    // discoverability companion, GET /list_textures.
    RegisterGetTextureRoute(server, captureBridge);
    RegisterListTexturesRoute(server, captureBridge);

    // editor-core-separation-7 campaign, PHASE4 - GET /render_graph, the
    // real, current-frame render-graph metadata endpoint this whole campaign
    // exists to ship.
    RegisterRenderGraphRoute(server, captureBridge);

    // network-impl-7 campaign
    // (PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md) - GET /list_tabs.
    // Needs NO bridge at all - the panel list comes straight from
    // EditorPanelRegistry::Instance() (editor-core-separation-3 campaign,
    // PHASE4 - see Core/EditorPanelRegistry.h) - this is the SIMPLEST route in
    // this whole file: zero runtime/thread/bridge dependency beyond that
    // process-wide registry.
    server.Get("/list_tabs", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(BuildListTabsResponseJson(), "application/json");
    });

    // network-impl-7 campaign
    // (PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md) -
    // GET /activate_tab?name=<PanelName>. See PHASE0_MASTER_STRATEGY.md's
    // own locked endpoint contract for the exact status-code mapping
    // implemented below.
    server.Get("/activate_tab", [uiCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedActivateTabQuery parsed = ParseActivateTabQuery(req.get_param_value("name"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        // IMPORTANT ordering detail: an unknown tab NAME (404) is checked
        // BEFORE the uiCommandBridge == nullptr (503) check - see this
        // phase document's own Section 3.3 "IMPORTANT ordering detail" note
        // - a request for an unrecognized name fails for the same reason
        // regardless of whether the bridge exists at all, so it must always
        // be a 404, never a 503.
        if (parsed.notFound) {
            res.status = 404;
            res.set_content(BuildUnknownTabNameResponseJson(parsed.tabName), "application/json");
            return;
        }
        if (uiCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("editor UI command bridge not available"), "application/json");
            return;
        }

        EditorUiCommandRequest request;
        request.kind = EditorUiCommandKind::ActivateTab;
        request.activateTab.tabName = parsed.tabName;

        const EditorUiCommandBridge::SubmitResult submit = uiCommandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another editor UI command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("editor UI command timed out"), "application/json");
            return;
        }

        const ActivateTabOutcome& outcome = submit.result->activateTab;
        res.status = outcome.success ? 200 : 409;
        res.set_content(BuildActivateTabResponseJson(outcome.success, outcome.tabExists, parsed.tabName), "application/json");
    });

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE6 (task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md)
    // - POST /spawn_gpu_driven_test_batch. Reuses the SAME uiCommandBridge
    // (EditorUiCommandBridge) /activate_tab already uses, mirroring its own
    // exact "parse -> bridge unavailable check -> build request -> SubmitAndWait
    // -> map alreadyPending/timedOut/outcome" shape.
    server.Post("/spawn_gpu_driven_test_batch", [uiCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedSpawnGpuDrivenTestBatchRequest parsed = ParseSpawnGpuDrivenTestBatchRequest(req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (uiCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("editor UI command bridge not available"), "application/json");
            return;
        }

        EditorUiCommandRequest request;
        request.kind = EditorUiCommandKind::SpawnGpuDrivenTestBatch;
        request.spawnGpuDrivenTestBatch.instanceCount = parsed.instanceCount;

        const EditorUiCommandBridge::SubmitResult submit = uiCommandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another editor UI command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("editor UI command timed out"), "application/json");
            return;
        }

        const SpawnGpuDrivenTestBatchOutcome& outcome = submit.result->spawnGpuDrivenTestBatch;
        // A release build (NullEditorLayer) reports editorAvailable == false
        // as 503 - "structurally unavailable in this build", not a caller
        // mistake - mirrors /save_scene's own "editorAvailable" -> 503
        // convention. A real, dedicated bool field (editor-core-separation-1
        // campaign, PHASE8) - never a substring search against
        // outcome.errorMessage's own TEXT (the previous, fragile shape this
        // replaces). Every other failure (should be unreachable -
        // ParseSpawnGpuDrivenTestBatchRequest() above already rejects
        // count < 1) would be a 400.
        if (!outcome.success) {
            res.status = outcome.editorAvailable ? 400 : 503;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        res.status = 200;
        res.set_content(
            BuildSpawnGpuDrivenTestBatchResponseJson(true, "", outcome.instanceCount), "application/json");
    });

    // task_manager/frame-debugger-3 campaign, PHASE7
    // (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) -
    // GET /frame_debugger/open, /enable, /capture, /select_event,
    // /set_channel, /set_levels, /state. Every route below
    // shares the SAME shape: parse (NetworkRoutes.h) -> bridge-unavailable
    // (503) check -> build a FrameDebuggerCommandRequest ->
    // FrameDebuggerCommandBridge::SubmitAndWait() ->
    // RespondWithFrameDebuggerCommandResult() maps alreadyPending/timedOut/
    // outcome to a status code + response body - mirroring every other
    // bridge-backed route in this file exactly.
    server.Get("/frame_debugger/open", [frameDebuggerCommandBridge](const httplib::Request&, httplib::Response& res) {
        if (frameDebuggerCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("frame debugger command bridge not available"), "application/json");
            return;
        }
        FrameDebuggerCommandRequest request;
        request.kind = FrameDebuggerCommandKind::OpenWindow;
        const FrameDebuggerCommandBridge::SubmitResult submit = frameDebuggerCommandBridge->SubmitAndWait(request);
        RespondWithFrameDebuggerCommandResult(res, submit);
    });

    server.Get("/frame_debugger/enable", [frameDebuggerCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedFrameDebuggerEnableQuery parsed = ParseFrameDebuggerEnableQuery(req.get_param_value("value"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (frameDebuggerCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("frame debugger command bridge not available"), "application/json");
            return;
        }
        FrameDebuggerCommandRequest request;
        request.kind = FrameDebuggerCommandKind::SetEnabled;
        request.setEnabled.enabled = parsed.value;
        const FrameDebuggerCommandBridge::SubmitResult submit = frameDebuggerCommandBridge->SubmitAndWait(request);
        RespondWithFrameDebuggerCommandResult(res, submit);
    });

    server.Get("/frame_debugger/capture", [frameDebuggerCommandBridge](const httplib::Request&, httplib::Response& res) {
        if (frameDebuggerCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("frame debugger command bridge not available"), "application/json");
            return;
        }
        FrameDebuggerCommandRequest request;
        request.kind = FrameDebuggerCommandKind::CaptureNow;
        const FrameDebuggerCommandBridge::SubmitResult submit = frameDebuggerCommandBridge->SubmitAndWait(request);
        RespondWithFrameDebuggerCommandResult(res, submit);
    });

    server.Get("/frame_debugger/select_event", [frameDebuggerCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedFrameDebuggerSelectEventQuery parsed = ParseFrameDebuggerSelectEventQuery(req.get_param_value("index"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (frameDebuggerCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("frame debugger command bridge not available"), "application/json");
            return;
        }
        FrameDebuggerCommandRequest request;
        request.kind = FrameDebuggerCommandKind::SelectEvent;
        request.selectEvent.index = parsed.index;
        const FrameDebuggerCommandBridge::SubmitResult submit = frameDebuggerCommandBridge->SubmitAndWait(request);
        RespondWithFrameDebuggerCommandResult(res, submit);
    });

    server.Get("/frame_debugger/set_channel", [frameDebuggerCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedFrameDebuggerSetChannelQuery parsed = ParseFrameDebuggerSetChannelQuery(req.get_param_value("value"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (frameDebuggerCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("frame debugger command bridge not available"), "application/json");
            return;
        }
        FrameDebuggerCommandRequest request;
        request.kind = FrameDebuggerCommandKind::SetChannel;
        request.setChannel.channel = parsed.channel;
        const FrameDebuggerCommandBridge::SubmitResult submit = frameDebuggerCommandBridge->SubmitAndWait(request);
        RespondWithFrameDebuggerCommandResult(res, submit);
    });

    server.Get("/frame_debugger/set_levels", [frameDebuggerCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedFrameDebuggerSetLevelsQuery parsed =
            ParseFrameDebuggerSetLevelsQuery(req.get_param_value("black"), req.get_param_value("white"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (frameDebuggerCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("frame debugger command bridge not available"), "application/json");
            return;
        }
        FrameDebuggerCommandRequest request;
        request.kind = FrameDebuggerCommandKind::SetLevels;
        request.setLevels.black = parsed.black;
        request.setLevels.white = parsed.white;
        const FrameDebuggerCommandBridge::SubmitResult submit = frameDebuggerCommandBridge->SubmitAndWait(request);
        RespondWithFrameDebuggerCommandResult(res, submit);
    });

    // GET /frame_debugger/state - a READ-ONLY status endpoint. Goes through
    // the SAME bridge (a GetState command kind) rather than a lighter-weight
    // direct read - see FrameDebuggerCommandBridge.h's own header comment
    // for exactly why (the Frame Debugger's state is genuinely mutable,
    // main-thread-owned data with no atomics of its own).
    server.Get("/frame_debugger/state", [frameDebuggerCommandBridge](const httplib::Request&, httplib::Response& res) {
        if (frameDebuggerCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("frame debugger command bridge not available"), "application/json");
            return;
        }
        FrameDebuggerCommandRequest request;
        request.kind = FrameDebuggerCommandKind::GetState;
        const FrameDebuggerCommandBridge::SubmitResult submit = frameDebuggerCommandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another frame debugger command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("frame debugger command timed out"), "application/json");
            return;
        }
        res.status = 200;
        res.set_content(BuildFrameDebuggerStateResponseJson(ToFrameDebuggerStateResponseView(submit.result->state)), "application/json");
    });

    // editor-core-separation-8 campaign, PHASE5
    // (PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md) -
    // GET /render_graph/set_pass_enabled, /passes, /set_feature_enabled,
    // /set_feature_priority, /set_blur_enabled, /set_gbuffer_enabled. Every
    // route below shares the SAME shape as every other bridge-backed route
    // in this file: parse (NetworkRoutes.h) -> bridge-unavailable (503)
    // check -> build a RenderGraphControlCommandRequest ->
    // RenderGraphControlCommandBridge::SubmitAndWait() ->
    // RespondWithRenderGraphControlCommandResult() maps alreadyPending/
    // timedOut/outcome to a status code + response body - EXCEPT
    // /render_graph/passes, which is read-only and has its own response
    // shape (mirrors /frame_debugger/state's own special-cased handling
    // immediately above).
    server.Get("/render_graph/set_pass_enabled",
        [renderGraphControlCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedRenderGraphSetPassEnabledQuery parsed =
            ParseRenderGraphSetPassEnabledQuery(req.get_param_value("name"), req.get_param_value("enabled"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (renderGraphControlCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("render graph control command bridge not available"), "application/json");
            return;
        }
        RenderGraphControlCommandRequest request;
        request.kind = RenderGraphControlCommandKind::SetBuiltInPassEnabled;
        request.setPassEnabled.name = parsed.name;
        request.setPassEnabled.enabled = parsed.enabled;
        const RenderGraphControlCommandBridge::SubmitResult submit = renderGraphControlCommandBridge->SubmitAndWait(request);
        RespondWithRenderGraphControlCommandResult(res, submit);
    });

    server.Get("/render_graph/passes",
        [renderGraphControlCommandBridge](const httplib::Request&, httplib::Response& res) {
        if (renderGraphControlCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("render graph control command bridge not available"), "application/json");
            return;
        }
        RenderGraphControlCommandRequest request;
        request.kind = RenderGraphControlCommandKind::ListPassStates;
        const RenderGraphControlCommandBridge::SubmitResult submit = renderGraphControlCommandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another render graph control command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("render graph control command timed out"), "application/json");
            return;
        }
        std::vector<RenderGraphControlPassStateResponseView> views;
        views.reserve(submit.result->passStates.size());
        for (const RenderGraphControlPassStateOutcome& outcome : submit.result->passStates) {
            RenderGraphControlPassStateResponseView view;
            view.name = outcome.name;
            view.enabled = outcome.enabled;
            view.everDeclaredThisSession = outcome.everDeclaredThisSession;
            views.push_back(std::move(view));
        }
        res.status = 200;
        res.set_content(BuildRenderGraphControlPassStatesResponseJson(views), "application/json");
    });

    server.Get("/render_graph/set_feature_enabled",
        [renderGraphControlCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedRenderGraphSetFeatureEnabledQuery parsed =
            ParseRenderGraphSetFeatureEnabledQuery(req.get_param_value("name"), req.get_param_value("enabled"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (renderGraphControlCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("render graph control command bridge not available"), "application/json");
            return;
        }
        RenderGraphControlCommandRequest request;
        request.kind = RenderGraphControlCommandKind::SetFeatureEnabled;
        request.setFeatureEnabled.name = parsed.name;
        request.setFeatureEnabled.enabled = parsed.enabled;
        const RenderGraphControlCommandBridge::SubmitResult submit = renderGraphControlCommandBridge->SubmitAndWait(request);
        RespondWithRenderGraphControlCommandResult(res, submit);
    });

    server.Get("/render_graph/set_feature_priority",
        [renderGraphControlCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedRenderGraphSetFeaturePriorityQuery parsed =
            ParseRenderGraphSetFeaturePriorityQuery(req.get_param_value("name"), req.get_param_value("priority"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (renderGraphControlCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("render graph control command bridge not available"), "application/json");
            return;
        }
        RenderGraphControlCommandRequest request;
        request.kind = RenderGraphControlCommandKind::SetFeaturePriority;
        request.setFeaturePriority.name = parsed.name;
        request.setFeaturePriority.priority = parsed.priority;
        const RenderGraphControlCommandBridge::SubmitResult submit = renderGraphControlCommandBridge->SubmitAndWait(request);
        RespondWithRenderGraphControlCommandResult(res, submit);
    });

    server.Get("/render_graph/set_blur_enabled",
        [renderGraphControlCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedRenderGraphSetBoolQuery parsed = ParseRenderGraphSetBoolQuery(req.get_param_value("enabled"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (renderGraphControlCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("render graph control command bridge not available"), "application/json");
            return;
        }
        RenderGraphControlCommandRequest request;
        request.kind = RenderGraphControlCommandKind::SetBlurEnabled;
        request.setBlurEnabled.enabled = parsed.enabled;
        const RenderGraphControlCommandBridge::SubmitResult submit = renderGraphControlCommandBridge->SubmitAndWait(request);
        RespondWithRenderGraphControlCommandResult(res, submit);
    });

    server.Get("/render_graph/set_gbuffer_enabled",
        [renderGraphControlCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedRenderGraphSetBoolQuery parsed = ParseRenderGraphSetBoolQuery(req.get_param_value("enabled"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (renderGraphControlCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("render graph control command bridge not available"), "application/json");
            return;
        }
        RenderGraphControlCommandRequest request;
        request.kind = RenderGraphControlCommandKind::SetGBufferEnabled;
        request.setGBufferEnabled.enabled = parsed.enabled;
        const RenderGraphControlCommandBridge::SubmitResult submit = renderGraphControlCommandBridge->SubmitAndWait(request);
        RespondWithRenderGraphControlCommandResult(res, submit);
    });

    // network-impl-3 campaign, Phase 5
    // (PHASE5_NETWORK_POST_ROUTES_AND_COMMAND_DISPATCH.md) - the engine's
    // first POST routes, and its first routes that MUTATE the ECS world.
    // Each handler is still a PURE function of its own request data plus
    // EngineCommandBridge::SubmitAndWait() (see AGENTS.md, "Networking") -
    // it never touches Registry/Renderer/Game/AssetDatabase directly. The
    // two routes are deliberately NOT collapsed into one shared helper (see
    // this phase document's own Step 2 - different request parser,
    // different response builder, different failure-status mapping for a
    // not-found case).
    server.Post("/instantiate_primitive", [commandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedInstantiatePrimitiveRequest parsed = ParseInstantiatePrimitiveRequest(req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (commandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
            return;
        }

        EngineCommandRequest request;
        request.kind = EngineCommandKind::InstantiatePrimitive;
        request.instantiatePrimitive.shape = parsed.shape;
        request.instantiatePrimitive.requestedName = parsed.name;
        request.instantiatePrimitive.worldPosition = Vec3{ parsed.worldX, parsed.worldY, parsed.worldZ };
        request.instantiatePrimitive.hasParent = parsed.hasParent;
        request.instantiatePrimitive.parentName = parsed.parentName;

        const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
            return;
        }

        const InstantiatePrimitiveOutcome& outcome = submit.result->instantiatePrimitive;
        res.status = outcome.success ? 200 : 400;
        res.set_content(BuildInstantiatePrimitiveResponseJson(outcome.success, outcome.errorMessage,
            outcome.entityIndex, outcome.entityGeneration, outcome.resolvedName,
            outcome.parentRequestedButNotFound, outcome.requestedParentName), "application/json");
    });

    server.Post("/delete_entity", [commandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedDeleteEntityRequest parsed = ParseDeleteEntityRequest(req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (commandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
            return;
        }

        EngineCommandRequest request;
        request.kind = EngineCommandKind::DeleteEntity;
        request.deleteEntity.name = parsed.name;

        const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
            return;
        }

        const DeleteEntityOutcome& outcome = submit.result->deleteEntity;
        // 404 (not 400) specifically for "no such entity" - a not-found
        // lookup is a distinct, well-known HTTP status from a generic bad
        // request, and is what lets a caller (an LLM/script) tell "you
        // typo'd the JSON shape" (400) apart from "that name doesn't exist
        // right now" (404) without parsing the error string. An EMPTY name
        // (the other DeleteEntityByName() failure case) is unreachable here
        // in practice, since ParseDeleteEntityRequest() above already
        // rejects an empty "name" field as a 400 -
        // Game::DeleteEntityByName()'s own empty-name guard is defense in
        // depth for its OTHER (non-network) callers, not something this
        // route can actually trigger.
        res.status = outcome.success ? 200 : 404;
        res.set_content(BuildDeleteEntityResponseJson(outcome.success, outcome.errorMessage,
            outcome.deletedEntityIndex, outcome.deletedEntityGeneration), "application/json");
    });

    // network-impl-5 campaign
    // (PHASE4_NETWORK_POST_ROUTES_WIRING.md) - two more POST routes, wired
    // the exact same five-step way as /instantiate_primitive//delete_entity
    // above: parse (Phase 1) -> bridge unavailable check -> build an
    // EngineCommandRequest (converting Phase 1's plain floats into real
    // Vec3/Quat values HERE, the one place that boundary is crossed) ->
    // SubmitAndWait() -> map alreadyPending/timedOut/outcome to a status
    // code + Phase 1's response builder.
    server.Post("/set_entity_trs", [commandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedSetEntityTrsRequest parsed = ParseSetEntityTrsRequest(req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (commandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
            return;
        }

        EngineCommandRequest request;
        request.kind = EngineCommandKind::SetEntityTrs;
        request.setEntityTrs.name = parsed.name;
        request.setEntityTrs.hasTranslation = parsed.hasTranslation;
        request.setEntityTrs.translation = Vec3{ parsed.translationX, parsed.translationY, parsed.translationZ };
        request.setEntityTrs.hasRotationEulerDegrees = parsed.hasRotationEulerDegrees;
        request.setEntityTrs.rotationEulerDegrees =
            Vec3{ parsed.rotationPitchXDegrees, parsed.rotationYawYDegrees, parsed.rotationRollZDegrees };
        request.setEntityTrs.hasScale = parsed.hasScale;
        request.setEntityTrs.scale = Vec3{ parsed.scaleX, parsed.scaleY, parsed.scaleZ };

        const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
            return;
        }

        const SetEntityTrsOutcome& outcome = submit.result->setEntityTrs;
        if (!outcome.success) {
            // See PHASE0/PHASE2's own note: TWO distinct failure reasons get
            // TWO distinct status codes - a not-found NAME is 404 (mirrors
            // /delete_entity's own convention), while a found-but-Transform-less
            // entity is 409 (the entity positively exists, this operation just
            // cannot apply to it).
            res.status = outcome.entityNotFound ? 404 : 409;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }

        const Vec3 resultEulerDegrees = outcome.resultingRotation.ToEulerDegrees();
        TransformSnapshotView snapshot;
        snapshot.positionX = outcome.resultingPosition.x;
        snapshot.positionY = outcome.resultingPosition.y;
        snapshot.positionZ = outcome.resultingPosition.z;
        snapshot.rotationEulerXDegrees = resultEulerDegrees.x;
        snapshot.rotationEulerYDegrees = resultEulerDegrees.y;
        snapshot.rotationEulerZDegrees = resultEulerDegrees.z;
        snapshot.rotationQuatX = outcome.resultingRotation.x;
        snapshot.rotationQuatY = outcome.resultingRotation.y;
        snapshot.rotationQuatZ = outcome.resultingRotation.z;
        snapshot.rotationQuatW = outcome.resultingRotation.w;
        snapshot.scaleX = outcome.resultingScale.x;
        snapshot.scaleY = outcome.resultingScale.y;
        snapshot.scaleZ = outcome.resultingScale.z;

        res.status = 200;
        res.set_content(BuildSetEntityTrsResponseJson(true, "", outcome.entityIndex, outcome.entityGeneration,
            outcome.translationChanged, outcome.rotationChanged, outcome.scaleChanged, snapshot), "application/json");
    });

    server.Post("/instantiate_light", [commandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedInstantiateLightRequest parsed = ParseInstantiateLightRequest(req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (commandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
            return;
        }

        EngineCommandRequest request;
        request.kind = EngineCommandKind::InstantiateLight;
        request.instantiateLight.lightType = parsed.lightType;
        request.instantiateLight.requestedName = parsed.name;
        request.instantiateLight.worldPosition = Vec3{ parsed.worldX, parsed.worldY, parsed.worldZ };
        request.instantiateLight.hasRotationEulerDegrees = parsed.hasRotationEulerDegrees;
        request.instantiateLight.rotationEulerDegrees =
            Vec3{ parsed.rotationPitchXDegrees, parsed.rotationYawYDegrees, parsed.rotationRollZDegrees };
        request.instantiateLight.color = Vec3{ parsed.colorR, parsed.colorG, parsed.colorB };
        request.instantiateLight.illuminanceLux = parsed.illuminanceLux;
        request.instantiateLight.active = parsed.active;
        request.instantiateLight.hasParent = parsed.hasParent;
        request.instantiateLight.parentName = parsed.parentName;

        const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
            return;
        }

        const InstantiateLightOutcome& outcome = submit.result->instantiateLight;
        res.status = outcome.success ? 200 : 400;
        // network-impl-5 campaign - deliberately reuses
        // BuildInstantiatePrimitiveResponseJson() verbatim (see NetworkRoutes.h,
        // Phase 1, Step 3.4) - InstantiateLightOutcome's own fields line up
        // 1:1 with what that builder already expects.
        res.set_content(BuildInstantiatePrimitiveResponseJson(outcome.success, outcome.errorMessage,
            outcome.entityIndex, outcome.entityGeneration, outcome.resolvedName,
            outcome.parentRequestedButNotFound, outcome.requestedParentName), "application/json");
    });

    // task_manager/stl-parser-2 campaign, PHASE2 - POST /import_asset. See
    // PHASE0_MASTER_STRATEGY.md's own locked endpoint contract for the exact
    // status-code mapping implemented below, and AGENTS.md's "Networking" for
    // why this handler still never touches AssetDatabase/ProjectPanel
    // directly - only AssetImportCommandBridge::SubmitAndWait().
    server.Post("/import_asset", [assetImportCommandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedImportAssetRequest parsed = ParseImportAssetRequest(req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (assetImportCommandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("asset import command bridge not available"), "application/json");
            return;
        }

        AssetImportCommandRequest request;
        request.kind = AssetImportCommandKind::ImportExternalFile;
        request.importExternalFile.sourceAbsolutePath = parsed.sourcePath;
        request.importExternalFile.destinationRelativeFolder = parsed.destinationFolder;

        // PHASE0's Locked Design Decision #6 - this bridge's own 120000ms
        // default (see AssetImportCommandBridge.h) is used here EXPLICITLY
        // (pass no second argument to SubmitAndWait()) - do not shorten it to
        // match the other bridges' 3000ms default.
        const AssetImportCommandBridge::SubmitResult submit = assetImportCommandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another asset import is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("asset import timed out"), "application/json");
            return;
        }

        const ImportExternalFileOutcome& outcome = submit.result->importExternalFile;
        if (!outcome.projectAvailable) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson(
                "the Editor's \"Project\" panel is not available in this build (the Editor "
                "module, or specifically its \"Project\" panel, is not compiled in)"), "application/json");
            return;
        }

        // NetworkRoutes.h's own "must never depend on src/Application/" rule
        // means BuildImportAssetResponseJson() takes an ImportedAssetResponseView
        // (this file's OWN type), never the Application-layer
        // ImportExternalFileOutcome directly - copy field-by-field first.
        ImportedAssetResponseView view;
        view.message = outcome.message;
        view.finalRelativePath = outcome.finalRelativePath;
        view.finalAbsolutePath = outcome.finalAbsolutePath;
        view.guid = outcome.guid;
        view.convertedToMeshAsset = outcome.convertedToMeshAsset;
        view.meshSourceFormat = outcome.meshSourceFormat;
        view.convertedToKtx2 = outcome.convertedToKtx2;
        view.convertedToMotionAsset = outcome.convertedToMotionAsset;
        view.meshVertexCount = outcome.meshVertexCount;
        view.meshTriangleCount = outcome.meshTriangleCount;
        res.status = outcome.success ? 200 : 400;
        res.set_content(outcome.success ? BuildImportAssetResponseJson(view) : BuildGenericErrorResponseJson(outcome.message),
            "application/json");
    });

    // task_manager/stl-parser-2 campaign, PHASE4 - POST /instantiate_asset.
    // Reuses the SAME EngineCommandBridge/commandBridge every other
    // /instantiate_* route already uses - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #9 for why this is
    // correct (an ECS/Renderer-mutating spawn, not an Editor/Project-panel
    // concern - this route has NO dependency on assetImportCommandBridge at
    // all, and works identically regardless of whether the Editor module is compiled in).
    server.Post("/instantiate_asset", [commandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedInstantiateAssetRequest parsed = ParseInstantiateAssetRequest(req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (commandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
            return;
        }

        EngineCommandRequest request;
        request.kind = EngineCommandKind::InstantiateMeshAsset;
        request.instantiateMeshAsset.absoluteGtaPath = parsed.gtaPath;

        const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
            return;
        }

        const InstantiateMeshAssetOutcome& outcome = submit.result->instantiateMeshAsset;
        if (!outcome.success) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        res.status = 200;
        res.set_content(BuildInstantiateAssetResponseJson(outcome.entityIndex, outcome.entityGeneration, outcome.resolvedName),
            "application/json");
    });

    // task_manager/scene-serialization-2 campaign, PHASE5
    // (PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md) - POST /save_scene and
    // POST /load_scene, a thin network bridge to Editor/SceneIO.h's
    // SaveScene()/LoadScene(). Reuses the SAME EngineCommandBridge/
    // commandBridge every other engine command already uses.
    server.Post("/save_scene", [commandBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedScenePathRequest parsed = ParseScenePathRequest(req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (commandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
            return;
        }

        EngineCommandRequest request;
        request.kind = EngineCommandKind::SaveScene;
        request.saveScene.path = parsed.path;

        const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
            return;
        }

        const SaveSceneOutcome& outcome = submit.result->saveScene;
        if (!outcome.editorAvailable) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        // A failed save is a 500 (an environment/I-O problem, not a
        // caller mistake) - see this route's own strategy doc note on why
        // this differs from /load_scene's own 400 below.
        res.status = outcome.success ? 200 : 500;
        res.set_content(BuildScenePathResponseJson(outcome.success, outcome.errorMessage, outcome.resolvedPath), "application/json");
    });

    server.Post("/load_scene", [commandBridge](const httplib::Request& req, httplib::Response& res) {
        // Mirrors /save_scene exactly, substituting EngineCommandKind::LoadScene,
        // request.loadScene.path, and submit.result->loadScene. A failed load
        // (missing/malformed file - see LoadScene()'s own doc comment) is a 400
        // (a caller-actionable "that scene file wasn't found or was invalid"),
        // NOT a 500 (which is reserved for save's own I/O-failure case, an
        // environment problem rather than a bad request) - this ONE status-code
        // difference between the two routes' otherwise-identical shape is
        // implemented deliberately, not copy-pasted identically.
        const ParsedScenePathRequest parsed = ParseScenePathRequest(req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (commandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
            return;
        }

        EngineCommandRequest request;
        request.kind = EngineCommandKind::LoadScene;
        request.loadScene.path = parsed.path;

        const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
            return;
        }

        const LoadSceneOutcome& outcome = submit.result->loadScene;
        if (!outcome.editorAvailable) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        res.status = outcome.success ? 200 : 400;
        res.set_content(BuildScenePathResponseJson(outcome.success, outcome.errorMessage, outcome.resolvedPath), "application/json");
    });

    // task_manager/logger-1 campaign, PHASE3
    // (PHASE3_NETWORK_ENDPOINTS_GET_LOGS_AND_CLEAR_LOGS.md) - GET /get_logs.
    // Needs NO bridge at all, exactly like GET /list_tabs above - Logger is
    // its OWN, purpose-built, thread-safe store (see AGENTS.md, "Logging"),
    // not engine state reached through the usual bridge rule (see AGENTS.md,
    // "Networking" - this route is a documented, narrow, deliberate
    // EXCEPTION to that rule, not a precedent for bypassing it elsewhere).
    // editor-core-separation-2 campaign, PHASE3 - now calls through the
    // nullable ILogQueryCapability* bridge instead of gte::Logger:: directly
    // (closes Defect C) - `nullptr` degrades to a 503, mirroring every
    // other bridge's identical "nullptr -> 503" convention already in this
    // file (e.g. /get_texture, /instantiate_primitive).
    server.Get("/get_logs", [logQueryCapability](const httplib::Request& req, httplib::Response& res) {
        const ParsedGetLogsQuery parsed = ParseGetLogsQuery(req.get_param_value("since_id"),
            req.get_param_value("min_level"), req.get_param_value("category"),
            req.get_param_value("keyword"), req.get_param_value("frame_min"),
            req.get_param_value("frame_max"), req.get_param_value("limit"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (logQueryCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("log query capability not available"), "application/json");
            return;
        }
        const std::vector<LogEntry> entries = logQueryCapability->Query(parsed.filter);
        res.set_content(
            BuildGetLogsResponseJson(entries, logQueryCapability->IsEnabled(), logQueryCapability->LatestEntryId()),
            "application/json");
    });

    // task_manager/logger-1 campaign, PHASE3 - POST /clear_logs. Same
    // "no bridge needed" shape as GET /get_logs above.
    // editor-core-separation-2 campaign, PHASE3 - same nullable-capability
    // "nullptr -> 503" conversion as GET /get_logs above.
    server.Post("/clear_logs", [logQueryCapability](const httplib::Request&, httplib::Response& res) {
        if (logQueryCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("log query capability not available"), "application/json");
            return;
        }
        const std::size_t clearedCount = logQueryCapability->EntryCount();
        logQueryCapability->Clear();
        res.set_content(BuildClearLogsResponseJson(clearedCount), "application/json");
    });
    // --- editor-core-separation-12 campaign (Project Assembly Hot Reload
    // plan, BIG-STEP 1) - see that campaign's PHASE3 doc for the full
    // design. All 5 OBSERVE routes below (status/ledger/loaded_assemblies/
    // component_types) EXCEPT scene_snapshot bypass EngineCommandBridge
    // entirely (mirrors GET /get_logs's own "no bridge needed" shape) -
    // scene_snapshot is the ONE exception, routed through commandBridge
    // instead, because it touches the live ECS Registry (see
    // Core/EditorCapabilities.h's own IHotReloadDebugCapability doc comment
    // for why).

    server.Get("/project_assembly/hot_reload/status",
        [hotReloadDebugCapability](const httplib::Request&, httplib::Response& res) {
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        res.set_content(BuildHotReloadStatusResponseJson(hotReloadDebugCapability->GetHotReloadStatus()), "application/json");
    });

    server.Get("/project_assembly/debug/ledger",
        [hotReloadDebugCapability](const httplib::Request& req, httplib::Response& res) {
        const ParsedProjectNameQuery parsed = ParseProjectNameQuery(req.get_param_value("name"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        res.set_content(
            BuildLedgerEntryResponseJson(parsed.projectName, hotReloadDebugCapability->GetLedgerEntry(parsed.projectName)),
            "application/json");
    });

    server.Get("/project_assembly/debug/loaded_assemblies",
        [hotReloadDebugCapability](const httplib::Request&, httplib::Response& res) {
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        res.set_content(
            BuildLoadedAssembliesResponseJson(hotReloadDebugCapability->GetLoadedAssemblyFileNames()), "application/json");
    });

    server.Get("/project_assembly/debug/component_types",
        [hotReloadDebugCapability](const httplib::Request&, httplib::Response& res) {
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        res.set_content(
            BuildComponentTypeNamesResponseJson(hotReloadDebugCapability->GetRegisteredComponentTypeNames()), "application/json");
    });

    // scene_snapshot is the ONE OBSERVE route routed through
    // EngineCommandBridge (see this campaign's PHASE0 doc, Correction 1) -
    // mirrors GET-via-POST-style /save_scene//load_scene's own
    // SubmitAndWait() shape exactly (lines ~1130-1165 above), even though
    // this route is itself a GET.
    server.Get("/project_assembly/debug/scene_snapshot",
        [commandBridge](const httplib::Request&, httplib::Response& res) {
        if (commandBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("engine command bridge not available"), "application/json");
            return;
        }
        EngineCommandRequest request;
        request.kind = EngineCommandKind::GetSceneSnapshot;
        const EngineCommandBridge::SubmitResult submit = commandBridge->SubmitAndWait(request);
        if (submit.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("another engine command is already in progress"), "application/json");
            return;
        }
        if (submit.timedOut) {
            res.status = 504;
            res.set_content(BuildGenericErrorResponseJson("engine command timed out"), "application/json");
            return;
        }
        const GetSceneSnapshotOutcome& outcome = submit.result->getSceneSnapshot;
        if (!outcome.editorAvailable) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        if (!outcome.success) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        res.set_content(outcome.sceneJson, "application/json");
    });

    server.Post("/project_assembly/debug/compile_only",
        [hotReloadDebugCapability](const httplib::Request& req, httplib::Response& res) {
        const ParsedProjectNameQuery parsed = ParseProjectNameQuery(req.get_param_value("name"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        const bool started = hotReloadDebugCapability->TriggerCompileOnly(parsed.projectName);
        res.set_content(
            BuildCompileOnlyTriggerResponseJson(started, started ? "" : "a build for this project is already in progress"),
            "application/json");
    });

    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE4 - POST /project_assembly/create_project. Placed
    // immediately after /project_assembly/debug/compile_only, same file,
    // same section, same style.
    server.Post("/project_assembly/create_project",
        [projectLifecycleCapability](const httplib::Request& req, httplib::Response& res) {
        // Deliberately does NOT go through ParseProjectNameQuery() -
        // CreateNewProjectAssembly() itself already validates the name far
        // more strictly (IsValidProjectAssemblyIdentifierName(), PHASE2)
        // than that older, permissive helper ever did; duplicating a
        // weaker check in front of a stronger one adds nothing.
        const std::string name = req.get_param_value("name");
        if (projectLifecycleCapability == nullptr) {
            res.status = 503;
            res.set_content(
                BuildGenericErrorResponseJson("project lifecycle capability not available"), "application/json");
            return;
        }
        const IProjectLifecycleCapability::CreateProjectOutcome outcome =
            projectLifecycleCapability->CreateNewProjectAssembly(name);
        if (!outcome.success) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
            return;
        }
        nlohmann::json body;
        body["created_source_directory"] = outcome.createdSourceDirectory;
        res.set_content(body.dump(), "application/json");
    });

    // editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 3), PHASE4 - the AGREED route contract from BIG-STEP 1's own
    // PHASE3 is UNCHANGED (method/path/query-param shape) - only this
    // handler's OWN body changed, from a permanent 501 placeholder to the
    // real thing: TriggerHotReload() now genuinely submits into
    // ProjectAssemblyHotReloadCommandBridge and BLOCKS until either the
    // whole synchronous hot-reload cycle finishes on the main thread, or
    // this HTTP request's own SubmitAndWait() call times out waiting for it
    // (see that bridge's own doc comment for its default timeout).
    server.Post("/project_assembly/hot_reload",
        [hotReloadDebugCapability](const httplib::Request& req, httplib::Response& res) {
        const ParsedProjectNameQuery parsed = ParseProjectNameQuery(req.get_param_value("name"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        // `started` here means "the cycle ran to completion" (regardless of
        // Success/RolledBack/CriticalFailure - see GetHotReloadStatus()
        // below for that), NOT "the cycle succeeded". `false` means
        // REJECTED (already in flight elsewhere, or this HTTP caller gave
        // up waiting - see SubmitAndWait()'s own doc comment for the
        // latter; the cycle itself, if it had already started, keeps
        // running on the main thread regardless).
        const bool started = hotReloadDebugCapability->TriggerHotReload(parsed.projectName);
        if (!started) {
            res.status = 503;
            res.set_content(
                BuildGenericErrorResponseJson("hot reload rejected - a build for this project may already be in progress, or this request timed out waiting for the main thread"),
                "application/json");
            return;
        }
        // Reuses the EXISTING BuildHotReloadStatusResponseJson() builder
        // (already used by GET /project_assembly/hot_reload/status since
        // BIG-STEP 1) - zero new JSON-shape code needed. IHotReloadDebugCapability
        // itself needed ZERO signature changes across this whole campaign,
        // exactly as BIG-STEP 1/2 promised downstream campaigns (LDD-HR3).
        res.set_content(BuildHotReloadStatusResponseJson(hotReloadDebugCapability->GetHotReloadStatus()), "application/json");
    });

    // editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 4), PHASE4 - LDD-HR6. The ONE narrow, hardcoded, testing-only
    // mutation route - see IHotReloadDebugCapability::
    // SetProbeHotReloadMarkerValueForTesting()'s own doc comment
    // (Core/EditorCapabilities.h) for the full "why narrow" reasoning.
    server.Post("/project_assembly/debug/set_probe_marker_value",
        [hotReloadDebugCapability](const httplib::Request& req, httplib::Response& res) {
        const ParsedSetProbeMarkerValueQuery parsed = ParseSetProbeMarkerValueQuery(req.get_param_value("value"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (hotReloadDebugCapability == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
            return;
        }
        const bool success = hotReloadDebugCapability->SetProbeHotReloadMarkerValueForTesting(parsed.value);
        nlohmann::json body;
        body["success"] = success;
        res.set_content(body.dump(), "application/json");
    });
}

} // namespace

struct NetworkServer::Impl {
    httplib::Server server;
};

NetworkServer::NetworkServer(FrameCaptureBridge* captureBridge, EngineCommandBridge* commandBridge,
    EditorUiCommandBridge* uiCommandBridge, FrameDebuggerCommandBridge* frameDebuggerCommandBridge,
    AssetImportCommandBridge* assetImportCommandBridge, ILogQueryCapability* logQueryCapability,
    RenderGraphControlCommandBridge* renderGraphControlCommandBridge, IHotReloadDebugCapability* hotReloadDebugCapability,
    IProjectLifecycleCapability* projectLifecycleCapability)
    : m_impl(std::make_unique<Impl>())
    , m_captureBridge(captureBridge)
    , m_commandBridge(commandBridge)
    , m_uiCommandBridge(uiCommandBridge)
    , m_frameDebuggerCommandBridge(frameDebuggerCommandBridge)
    , m_assetImportCommandBridge(assetImportCommandBridge)
    , m_logQueryCapability(logQueryCapability)
    , m_renderGraphControlCommandBridge(renderGraphControlCommandBridge)
    , m_hotReloadDebugCapability(hotReloadDebugCapability)
    , m_projectLifecycleCapability(projectLifecycleCapability)
{
    // Registered exactly ONCE per NetworkServer instance, here in the
    // constructor - never inside Start() - so a Start()/Stop()/Start()
    // restart cycle (or a Start() that overlaps a failed bind retry) can
    // NEVER re-register the same route handler onto the same
    // httplib::Server a second time.
    RegisterRoutes(m_impl->server, m_captureBridge, m_commandBridge, m_uiCommandBridge, m_frameDebuggerCommandBridge,
        m_assetImportCommandBridge, m_logQueryCapability, m_renderGraphControlCommandBridge, m_hotReloadDebugCapability,
        m_projectLifecycleCapability);
    // task_manager/stl-parser-2 campaign, PHASE2 - m_assetImportCommandBridge
    // is now actually consulted by RegisterRoutes() above (POST /import_asset).
    // editor-core-separation-8 campaign, PHASE5 - m_renderGraphControlCommandBridge
    // is now actually consulted by RegisterRoutes() above (the 6 new
    // GET /render_graph/* routes).
    // editor-core-separation-12 campaign - m_hotReloadDebugCapability is now
    // actually consulted by RegisterRoutes() above (the 7 new
    // GET/POST /project_assembly/* routes).
    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE4 - m_projectLifecycleCapability is now actually
    // consulted by RegisterRoutes() above (POST /project_assembly/create_project).
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
        // logger-1 campaign, Phase 2 - additive log call next to this
        // existing fprintf site. This runs on the CALLING thread (the main
        // thread, since Application calls Start() synchronously during
        // construction), not the Network background thread - see PHASE2's
        // own Step 2 for the fact-checked detail.
        GTE_LOG_ERROR("Network", "failed to bind " + std::string(kBindHost) + ":"
            + std::to_string(port) + " - network endpoint disabled this run.");
        return; // Non-fatal - see header comment.
    }

    m_boundPort.store(resolvedPort);
    m_running.store(true);

    m_thread = std::thread([this]() {
        m_impl->server.listen_after_bind();
        m_running.store(false);
    });

    std::fprintf(stdout, "NetworkServer: listening on %s:%d\n", kBindHost, resolvedPort);
    // logger-1 campaign, Phase 2 - additive log call next to this existing
    // fprintf site (same main-thread caveat as the failed-bind site above).
    GTE_LOG_INFO("Network", "listening on " + std::string(kBindHost) + ":"
        + std::to_string(resolvedPort));
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
