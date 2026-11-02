#include "NetworkPhysicsDebugRoutes.h"

#include "NetworkRoutes.h"

#include "../Application/PhysicsDebugCommandBridge.h"
#include "../Editor/PhysicsDebugImage.h"
#include "../Encoding/Base64.h"
#include "../Encoding/PngEncoder.h"

#include <httplib.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gte::Network {

namespace {

constexpr int kQuickTimeoutMilliseconds = 3000;
constexpr int kRunTimeoutMilliseconds = 60000;

void Respond(httplib::Response& res, int status, const std::string& body)
{
    res.status = status;
    res.set_content(body, "application/json");
}

PhysicsDebugQueryParams QueryOf(const httplib::Request& req)
{
    return PhysicsDebugQueryParams(req.params.begin(), req.params.end());
}

// Submits one command. On any failure it answers the request itself and returns nullopt.
std::optional<PhysicsDebugCommandResult> Submit(
    PhysicsDebugCommandBridge* bridge, PhysicsDebugCommandRequest request, int timeoutMilliseconds, httplib::Response& res)
{
    if (bridge == nullptr) {
        Respond(res, 503, BuildGenericErrorResponseJson(kPhysicsDebugNoBridgeMessage));
        return std::nullopt;
    }
    PhysicsDebugCommandBridge::SubmitResult submit = bridge->SubmitAndWait(std::move(request), timeoutMilliseconds);
    if (submit.alreadyPending) {
        Respond(res, 503, BuildGenericErrorResponseJson(kPhysicsDebugBusyMessage));
        return std::nullopt;
    }
    if (submit.timedOut || !submit.result) {
        Respond(res, 504, BuildGenericErrorResponseJson(kPhysicsDebugTimeoutMessage));
        return std::nullopt;
    }
    PhysicsDebugCommandResult result = std::move(*submit.result);
    if (!result.success) {
        std::string message = result.errorMessage;
        if (!result.errorKey.empty() && message.find(result.errorKey) == std::string::npos) {
            message += " (key: " + result.errorKey + ")";
        }
        Respond(res, result.httpStatus >= 400 ? result.httpStatus : 409, BuildGenericErrorResponseJson(message));
        return std::nullopt;
    }
    return result;
}

// A request that only names an entity.
PhysicsDebugCommandRequest RequestFor(PhysicsDebugCommandKind kind, std::string entity)
{
    PhysicsDebugCommandRequest request;
    request.kind = kind;
    request.entityName = std::move(entity);
    return request;
}

void ServeImage(const httplib::Request& req, httplib::Response& res, const PhysicsDebugSnapshot& snapshot)
{
    if (!snapshot.valid) {
        Respond(res, 409, BuildGenericErrorResponseJson("no pose yet, wait one frame"));
        return;
    }
    const PhysicsDebugImage image = RenderPhysicsDebugSnapshot(snapshot);
    const std::vector<std::uint8_t> png = image.rgba.empty()
        ? std::vector<std::uint8_t>{}
        : Encoding::EncodeRgba8ToPng(image.rgba.data(), static_cast<int>(image.width), static_cast<int>(image.height));
    if (png.empty()) {
        Respond(res, 500, BuildGenericErrorResponseJson("the image could not be encoded"));
        return;
    }
    const CaptureResponseFormat format =
        ResolveCaptureResponseFormat(req.get_param_value("format"), req.get_header_value("Accept"));
    if (format == CaptureResponseFormat::RawPng) {
        res.status = 200;
        res.set_content(reinterpret_cast<const char*>(png.data()), png.size(), "image/png");
        return;
    }
    PhysicsDebugImageViewInfo view;
    view.name = snapshot.view;
    view.right = image.basis.right;
    view.up = image.basis.up;
    view.forward = image.basis.forward;
    view.boundsMin = image.boundsMin;
    view.boundsMax = image.boundsMax;
    view.pixelsPerUnit = image.pixelsPerUnit;
    Respond(res, 200,
        BuildPhysicsDebugImageJsonBody(static_cast<int>(image.width), static_cast<int>(image.height),
            Encoding::EncodeBase64(png), view, snapshot.highlightedBones));
}

} // namespace

void RegisterPhysicsDebugRoutes(httplib::Server& server, PhysicsDebugCommandBridge* bridge, PhysicsDebugBrowserRefusal refuseBrowser)
{
    using Kind = PhysicsDebugCommandKind;

    server.Get("/physics_debug/list", [bridge](const httplib::Request& req, httplib::Response& res) {
        if (!req.params.empty()) {
            Respond(res, 400, BuildGenericErrorResponseJson("unknown query parameter: " + req.params.begin()->first));
            return;
        }
        if (const auto result = Submit(bridge, RequestFor(Kind::List, {}), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugListResponseJson(result->list));
        }
    });

    server.Get("/physics_debug/state", [bridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedPhysicsDebugEntityQuery parsed = ParsePhysicsDebugEntityQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        if (const auto result = Submit(bridge, RequestFor(Kind::State, parsed.entity), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugStateResponseJson(result->state));
        }
    });

    server.Get("/physics_debug/set", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser(req, res)) {
            return;
        }
        ParsedPhysicsDebugSetQuery parsed = ParsePhysicsDebugSetQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        PhysicsDebugCommandRequest request = RequestFor(Kind::Set, std::move(parsed.entity));
        request.pairs = std::move(parsed.pairs);
        if (const auto result = Submit(bridge, std::move(request), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugSettingsResponseJson(result->settings));
        }
    });

    server.Get("/physics_debug/preset", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser(req, res)) {
            return;
        }
        const ParsedPhysicsDebugPresetQuery parsed = ParsePhysicsDebugPresetQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        PhysicsDebugCommandRequest request = RequestFor(Kind::Preset, {});
        request.presetName = parsed.name;
        if (const auto result = Submit(bridge, std::move(request), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugSettingsResponseJson(result->settings));
        }
    });

    server.Post("/physics_debug/play", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser(req, res)) {
            return;
        }
        const ParsedPhysicsDebugPlayRequest parsed = ParsePhysicsDebugPlayRequest(req.body);
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        PhysicsDebugCommandRequest request = RequestFor(Kind::Play, parsed.entity);
        request.animationGtaPath = parsed.animationGtaPath;
        request.hasFrame = parsed.hasFrame;
        request.frame = parsed.frame;
        request.hasSpeed = parsed.hasSpeed;
        request.speed = parsed.speed;
        request.hasLoop = parsed.hasLoop;
        request.loop = parsed.loop;
        if (const auto result = Submit(bridge, std::move(request), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugPlayResponseJson(parsed.entity, result->animator));
        }
    });

    server.Get("/physics_debug/seek", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser(req, res)) {
            return;
        }
        const ParsedPhysicsDebugSeekQuery parsed = ParsePhysicsDebugSeekQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        PhysicsDebugCommandRequest request = RequestFor(Kind::Seek, parsed.entity);
        request.frame = parsed.frame;
        request.hasFrame = true;
        request.resetPhysics = parsed.resetPhysics;
        if (const auto result = Submit(bridge, std::move(request), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugPlayResponseJson(parsed.entity, result->animator));
        }
    });

    server.Get("/physics_debug/reset", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser(req, res)) {
            return;
        }
        const ParsedPhysicsDebugEntityQuery parsed = ParsePhysicsDebugEntityQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        if (const auto result = Submit(bridge, RequestFor(Kind::Reset, parsed.entity), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugSuccessResponseJson());
        }
    });

    server.Post("/physics_debug/run", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser(req, res)) {
            return;
        }
        ParsedPhysicsDebugRunRequest parsed = ParsePhysicsDebugRunRequest(req.body);
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        const std::string entity = parsed.run.entityName;
        PhysicsDebugCommandRequest request = RequestFor(Kind::Run, entity);
        request.run = std::move(parsed.run);
        if (const auto result = Submit(bridge, std::move(request), kRunTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugRunResponseJson(entity, result->run));
        }
    });

    server.Get("/physics_debug/capture", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser(req, res)) {
            return;
        }
        const ParsedPhysicsDebugCaptureQuery parsed = ParsePhysicsDebugCaptureQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        PhysicsDebugCommandRequest request = RequestFor(Kind::Capture, parsed.entity);
        request.captureEnabled = parsed.enabled;
        request.windowFrames = parsed.windowFrames;
        if (const auto result = Submit(bridge, std::move(request), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugCaptureResponseJson(result->capture));
        }
    });

    server.Get("/physics_debug/metrics", [bridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedPhysicsDebugMetricsQuery parsed = ParsePhysicsDebugMetricsQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        PhysicsDebugCommandRequest request = RequestFor(Kind::Metrics, parsed.entity);
        request.windowFrames = parsed.windowFrames;
        request.groupFilter = parsed.group;
        if (const auto result = Submit(bridge, std::move(request), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugMetricsResponseJson(parsed.entity, result->metrics));
        }
    });

    server.Get("/physics_debug/joints", [bridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedPhysicsDebugJointsQuery parsed = ParsePhysicsDebugJointsQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        PhysicsDebugCommandRequest request = RequestFor(Kind::Joints, parsed.entity);
        request.chainFilter = parsed.chain;
        request.groupFilter = parsed.group;
        request.first = parsed.first;
        request.count = parsed.count;
        if (const auto result = Submit(bridge, std::move(request), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugJointsResponseJson(result->joints));
        }
    });

    server.Get("/physics_debug/colliders", [bridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedPhysicsDebugEntityQuery parsed = ParsePhysicsDebugEntityQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        if (const auto result = Submit(bridge, RequestFor(Kind::Colliders, parsed.entity), kQuickTimeoutMilliseconds, res)) {
            Respond(res, 200, BuildPhysicsDebugCollidersResponseJson(result->colliders));
        }
    });

    // The main thread only hands back the drawing data; the picture is made and encoded here, on the pool thread.
    server.Get("/physics_debug/image", [bridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedPhysicsDebugImageQuery parsed = ParsePhysicsDebugImageQuery(QueryOf(req));
        if (!parsed.valid) {
            Respond(res, 400, BuildGenericErrorResponseJson(parsed.errorMessage));
            return;
        }
        PhysicsDebugCommandRequest request = RequestFor(Kind::Image, parsed.request.entityName);
        request.image = parsed.request;
        if (const auto result = Submit(bridge, std::move(request), kQuickTimeoutMilliseconds, res)) {
            ServeImage(req, res, result->snapshot);
        }
    });
}

} // namespace gte::Network
