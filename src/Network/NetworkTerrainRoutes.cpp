#include "NetworkTerrainRoutes.h"

#include "NetworkRoutes.h"

#include "../Application/TerrainCommandBridge.h"

#include <httplib.h>

#include <string>
#include <utility>

namespace gte::Network {

namespace {

constexpr int kTimeoutMilliseconds = 20000;

bool IsReadOnlyAction(const std::string& action)
{
    return action == "state" || action == "map" || action == "settings";
}

void Respond(httplib::Response& res, int status, const std::string& body, const char* contentType = "application/json")
{
    res.status = status;
    res.set_content(body, contentType);
}

} // namespace

void RegisterTerrainRoutes(httplib::Server& server, TerrainCommandBridge* bridge, TerrainBrowserRefusal refuseBrowser)
{
    server.Get(R"(/terrain/(\w+))", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        TerrainCommandRequest request;
        request.action = req.matches[1].str();
        if (!IsReadOnlyAction(request.action) && refuseBrowser(req, res)) {
            return;
        }
        if (bridge == nullptr) {
            Respond(res, 503, BuildGenericErrorResponseJson("terrain bridge not available"));
            return;
        }
        for (const auto& [key, value] : req.params) {
            request.params.emplace_back(key, value);
        }
        TerrainCommandBridge::SubmitResult submit = bridge->SubmitAndWait(std::move(request), kTimeoutMilliseconds);
        if (submit.alreadyPending) {
            Respond(res, 503, BuildGenericErrorResponseJson("another terrain command is running, retry"));
            return;
        }
        if (submit.timedOut || !submit.result) {
            Respond(res, 504, BuildGenericErrorResponseJson("terrain command timed out"));
            return;
        }
        Respond(res, submit.result->httpStatus, submit.result->body, submit.result->contentType.c_str());
    });
}

} // namespace gte::Network
