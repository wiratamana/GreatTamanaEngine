#pragma once

namespace httplib {
class Server;
struct Request;
struct Response;
} // namespace httplib

namespace gte {
class PhysicsDebugCommandBridge;
} // namespace gte

namespace gte::Network {

// Refuses a browser request with 403 and returns true.
using PhysicsDebugBrowserRefusal = bool (*)(const httplib::Request&, httplib::Response&);

// Registers the /physics_debug/* routes. A null bridge answers 503. Routes that change state refuse browser requests.
void RegisterPhysicsDebugRoutes(httplib::Server& server, PhysicsDebugCommandBridge* bridge, PhysicsDebugBrowserRefusal refuseBrowser);

} // namespace gte::Network
