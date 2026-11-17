#pragma once

namespace httplib {
class Server;
struct Request;
struct Response;
} // namespace httplib

namespace gte {
class ModuleCommandBridge;
} // namespace gte

namespace gte::Network {

// Refuses a browser request with 403 and returns true.
using ModuleBrowserRefusal = bool (*)(const httplib::Request&, httplib::Response&);

// Registers GET /<prefix>/<action> for every route a feature module put in the FeatureRouteRegistry.
// A null bridge answers 503. Every action that is not listed read-only refuses browser requests.
void RegisterModuleRoutes(httplib::Server& server, ModuleCommandBridge* bridge, ModuleBrowserRefusal refuseBrowser);

} // namespace gte::Network
