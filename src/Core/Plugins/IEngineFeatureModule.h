#pragma once

#include <memory>

namespace gte {

class Core;

// One instance per built-in feature module, created at host startup and destroyed at shutdown.
class IEngineFeatureModule {
public:
    virtual ~IEngineFeatureModule() = default;

    // Short stable name for logging and panel-factory lookup. Never used for behavior branching.
    virtual const char* ModuleName() const = 0;
};

// Instances are created after Core exists; static registration only registers the factory.
using EngineFeatureModuleFactory = std::unique_ptr<IEngineFeatureModule> (*)(Core&);

} // namespace gte
