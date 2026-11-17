#pragma once

// HTTP routes a feature module adds by itself, so the network layer knows no feature names.
// Modules register at static-init time; the server reads the lists once, when it is built.

#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

namespace gte {

// GET /<urlPrefix>/<name> becomes the module command "<actionPrefix><name>".
struct ModuleRouteSpec {
    std::string urlPrefix;
    std::string actionPrefix;
    std::vector<std::string> readOnlyActions; // Names without the action prefix; the rest refuse browser requests.

    // `action` is the full command name, including the action prefix.
    bool IsReadOnly(const std::string& action) const;
};

// GET <path> answers like /feature_debug/invoke?name=<featureName>&action=<action>.
struct DebugRouteAlias {
    std::string path;
    std::string featureName;
    std::string action;
};

class FeatureRouteRegistry {
public:
    static FeatureRouteRegistry& Instance();

    // A duplicate url prefix or alias path is refused: the first registration wins.
    bool AddModuleRoutes(ModuleRouteSpec spec);
    bool AddDebugAlias(DebugRouteAlias alias);

    std::vector<ModuleRouteSpec> ModuleRoutes() const;
    std::vector<DebugRouteAlias> DebugAliases() const;

private:
    mutable std::mutex m_mutex;
    std::vector<ModuleRouteSpec> m_routes;
    std::vector<DebugRouteAlias> m_aliases;
};

struct FeatureRouteAutoRegister {
    FeatureRouteAutoRegister(const char* urlPrefix, const char* actionPrefix, std::initializer_list<const char*> readOnlyActions);
};

struct DebugRouteAliasAutoRegister {
    DebugRouteAliasAutoRegister(const char* path, const char* featureName, const char* action);
};

} // namespace gte

#define GTE_ROUTE_CONCAT_INNER(a, b) a##b
#define GTE_ROUTE_CONCAT(a, b) GTE_ROUTE_CONCAT_INNER(a, b)

// Registers GET /<UrlPrefix>/<action>. The rest of the arguments are the read-only action names.
#define GTE_REGISTER_MODULE_ROUTES(UrlPrefix, ActionPrefix, ...) \
    static ::gte::FeatureRouteAutoRegister GTE_ROUTE_CONCAT(g_gteModuleRoutes_, __LINE__)( \
        UrlPrefix, ActionPrefix, { __VA_ARGS__ })

// Registers a short GET route that forwards to a feature debug action.
#define GTE_REGISTER_DEBUG_ROUTE_ALIAS(Path, FeatureName, Action) \
    static ::gte::DebugRouteAliasAutoRegister GTE_ROUTE_CONCAT(g_gteDebugRouteAlias_, __LINE__)(Path, FeatureName, Action)
