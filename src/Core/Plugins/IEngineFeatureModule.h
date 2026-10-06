#pragma once

#include <memory>

namespace gte {

class Core;

// One instance per built-in feature module, constructed once at EditorHost
// (or a future PlayerHost) startup and destroyed once at shutdown.
class IEngineFeatureModule {
public:
    virtual ~IEngineFeatureModule() = default;

    // Short, stable, human-readable name - used for logging and as the
    // lookup key BuiltinFeatureEditorPanelRegistry matches an optional panel
    // factory against. Never used for any other behavior branch in gte_core.
    virtual const char* ModuleName() const = 0;
};

// Construction happens AFTER Core exists - static registration only ever
// registers the factory, never the instance.
using EngineFeatureModuleFactory = std::unique_ptr<IEngineFeatureModule> (*)(Core&);

} // namespace gte
