#pragma once
#include "SyncCommandBridge.h"

#include <string>
#include <utility>
#include <vector>

namespace gte {

// One module HTTP call (/<prefix>/<name>): the action name plus the raw query pairs, parsed on the main thread.
struct ModuleCommandRequest {
    std::string action;
    std::vector<std::pair<std::string, std::string>> params;
};

struct ModuleCommandResult {
    int httpStatus = 200;
    std::string contentType = "application/json";
    std::string body;
};

class ModuleCommandBridge : public SyncCommandBridge<ModuleCommandRequest, ModuleCommandResult> {};

} // namespace gte
