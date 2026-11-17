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
using TerrainBrowserRefusal = bool (*)(const httplib::Request&, httplib::Response&);

// Registers /terrain/<action>. A null bridge answers 503. Every action except state and map refuses browser requests.
void RegisterTerrainRoutes(httplib::Server& server, ModuleCommandBridge* bridge, TerrainBrowserRefusal refuseBrowser);

} // namespace gte::Network
