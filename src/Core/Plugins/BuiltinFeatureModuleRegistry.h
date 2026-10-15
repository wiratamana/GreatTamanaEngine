#pragma once

#include "IEngineFeatureModule.h"

#include <memory>
#include <string>
#include <vector>

namespace gte {

// Meyers singleton: the factory list is built on first use, so there is no static-init-order risk.
class BuiltinFeatureModuleRegistry {
public:
    static BuiltinFeatureModuleRegistry& Instance();

    // Called by the self-registration macro before main(); only stores the factory.
    // A duplicate debugName is refused (logged, asserted in debug): the first registration wins.
    void RegisterFactory(const char* debugName, EngineFeatureModuleFactory factory);

    // Called once by the host after Core exists: one instance per factory, in registration order.
    // A factory that throws is logged by name and skipped. The caller owns the result.
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
