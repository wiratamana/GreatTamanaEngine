#include "NetworkServer.h"

#include "NetworkRoutes.h"

#include "../Application/EditorUiCommandBridge.h"
#include "../Application/EngineCommandBridge.h"
#include "../Application/FrameCaptureBridge.h"
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

// The one, hand-written route table for this campaign - see
// PHASE0_MASTER_STRATEGY.md's locked "Endpoint contract". A future endpoint
// is added here as one more server.Get(...)/Post(...) line, forwarding to
// its own NetworkRoutes.h function - never composing response text inline
// in this lambda.
void RegisterRoutes(httplib::Server& server, FrameCaptureBridge* captureBridge, EngineCommandBridge* commandBridge,
    EditorUiCommandBridge* uiCommandBridge)
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

    // network-impl-7 campaign
    // (PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md) - GET /list_tabs.
    // Needs NO bridge at all - the panel catalog is fixed at compile time
    // (see EditorPanelCatalog.h) - this is the SIMPLEST route in this whole
    // file: a pure function of build configuration, zero runtime/thread/
    // bridge dependency.
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
}

} // namespace

struct NetworkServer::Impl {
    httplib::Server server;
};

NetworkServer::NetworkServer(FrameCaptureBridge* captureBridge, EngineCommandBridge* commandBridge,
    EditorUiCommandBridge* uiCommandBridge)
    : m_impl(std::make_unique<Impl>())
    , m_captureBridge(captureBridge)
    , m_commandBridge(commandBridge)
    , m_uiCommandBridge(uiCommandBridge)
{
    // Registered exactly ONCE per NetworkServer instance, here in the
    // constructor - never inside Start() - so a Start()/Stop()/Start()
    // restart cycle (or a Start() that overlaps a failed bind retry) can
    // NEVER re-register the same route handler onto the same
    // httplib::Server a second time.
    RegisterRoutes(m_impl->server, m_captureBridge, m_commandBridge, m_uiCommandBridge);
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
