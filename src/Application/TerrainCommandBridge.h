#pragma once
#include "SyncCommandBridge.h"

#include <string>
#include <utility>
#include <vector>

namespace gte {

// One /terrain/<action> call: the action name plus the raw query pairs, parsed on the main thread.
struct TerrainCommandRequest {
    std::string action;
    std::vector<std::pair<std::string, std::string>> params;
};

struct TerrainCommandResult {
    int httpStatus = 200;
    std::string contentType = "application/json";
    std::string body;
};

class TerrainCommandBridge : public SyncCommandBridge<TerrainCommandRequest, TerrainCommandResult> {};

} // namespace gte
