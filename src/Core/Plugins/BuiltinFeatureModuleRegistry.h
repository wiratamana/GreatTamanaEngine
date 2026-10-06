#pragma once

#include "IEngineFeatureModule.h"

#include <memory>
#include <string>
#include <vector>

namespace gte {

// Meyers-singleton, mirroring EditorPanelRegistry::Instance() - the vector
// is only constructed on first real use, so there is no static-init-order
// risk.
class BuiltinFeatureModuleRegistry {
public:
    static BuiltinFeatureModuleRegistry& Instance();

    // Called by each feature's self-registration macro, at static-init
    // time, before main(). Only appends to a factory list - never touches
    // Core, never constructs anything. A duplicate debugName is refused:
    // logged unconditionally, asserted in debug builds; a release build
    // keeps the first registration and drops the second.
    void RegisterFactory(const char* debugName, EngineFeatureModuleFactory factory);

    // Called exactly once, by EditorHost (or a future PlayerHost), after
    // Core exists. Constructs one instance per factory, in registration
    // order. Caller owns the result; this registry keeps only the factory
    // list. Each factory call is wrapped in try/catch - a misbehaving
    // module's constructor throwing is logged by name and skipped, it does
    // not take every other already-registered feature down with it.
    std::vector<std::unique_ptr<IEngineFeatureModule>> CreateAll(Core& core) const;

private:
    BuiltinFeatureModuleRegistry() = default;
    struct FactoryEntry { std::string debugName; EngineFeatureModuleFactory factory; };
    std::vector<FactoryEntry> m_factories;
};

struct EngineFeatureModuleAutoRegister {
    EngineFeatureModuleAutoRegister(const char* debugName, EngineFeatureModuleFactory factory)
    {
        BuiltinFeatureModuleRegistry::Instance().RegisterFactory(debugName, factory);
    }
};

} // namespace gte

// Two-level token-paste - `##` suppresses macro expansion of its own
// operands, so pasting __LINE__ directly would literally spell
// "..._AutoRegister___LINE__" every time. Mirrors GTE_PROFILE_SCOPE_CONCAT
// (src/Profiling/ScopeTimer.h).
#define GTE_BUILTIN_FEATURE_MODULE_CONCAT_INNER(a, b) a##b
#define GTE_BUILTIN_FEATURE_MODULE_CONCAT(a, b) GTE_BUILTIN_FEATURE_MODULE_CONCAT_INNER(a, b)

#define GTE_REGISTER_BUILTIN_FEATURE_MODULE(DebugNameStringLiteral, FactoryFunctionPointer) \
    static ::gte::EngineFeatureModuleAutoRegister \
        GTE_BUILTIN_FEATURE_MODULE_CONCAT(g_gteBuiltinFeatureModuleAutoRegister_, __LINE__)( \
            DebugNameStringLiteral, FactoryFunctionPointer)
