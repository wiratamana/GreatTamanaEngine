#include "NetworkModuleRoutes.h"

#include "NetworkRoutes.h"

#include "../Application/FeatureRouteRegistry.h"
#include "../Application/ModuleCommandBridge.h"

#include <httplib.h>

#include <memory>
#include <string>
#include <utility>

namespace gte::Network {

namespace {

constexpr int kTimeoutMilliseconds = 20000;

void Respond(httplib::Response& res, int status, const std::string& body, const char* contentType = "application/json")
{
    res.status = status;
    res.set_content(body, contentType);
}

} // namespace

void RegisterModuleRoutes(httplib::Server& server, ModuleCommandBridge* bridge, ModuleBrowserRefusal refuseBrowser)
{
    // One main-thread executor serves every family; /<prefix>/<name> arrives as the action "<actionPrefix><name>".
    for (ModuleRouteSpec& route : FeatureRouteRegistry::Instance().ModuleRoutes()) {
        const std::string pattern = "/" + route.urlPrefix + R"(/(\w+))";
        const auto spec = std::make_shared<const ModuleRouteSpec>(std::move(route));
        server.Get(pattern, [bridge, refuseBrowser, spec](const httplib::Request& req, httplib::Response& res) {
            ModuleCommandRequest request;
            request.action = spec->actionPrefix + req.matches[1].str();
            if (!spec->IsReadOnly(request.action) && refuseBrowser(req, res)) {
                return;
            }
            if (bridge == nullptr) {
                Respond(res, 503, BuildGenericErrorResponseJson("module command bridge not available"));
                return;
            }
            for (const auto& [key, value] : req.params) {
                request.params.emplace_back(key, value);
            }
            ModuleCommandBridge::SubmitResult submit = bridge->SubmitAndWait(std::move(request), kTimeoutMilliseconds);
            if (submit.alreadyPending) {
                Respond(res, 503, BuildGenericErrorResponseJson("another module command is running, retry"));
                return;
            }
            if (submit.timedOut || !submit.result) {
                Respond(res, 504, BuildGenericErrorResponseJson("module command timed out"));
                return;
            }
            Respond(res, submit.result->httpStatus, submit.result->body, submit.result->contentType.c_str());
        });
    }
}

} // namespace gte::Network
