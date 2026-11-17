#include "FeatureRouteRegistry.h"

#include <algorithm>
#include <utility>

namespace gte {

bool ModuleRouteSpec::IsReadOnly(const std::string& action) const
{
    if (action.size() < actionPrefix.size() || action.compare(0, actionPrefix.size(), actionPrefix) != 0) {
        return false;
    }
    const std::string name = action.substr(actionPrefix.size());
    return std::find(readOnlyActions.begin(), readOnlyActions.end(), name) != readOnlyActions.end();
}

FeatureRouteRegistry& FeatureRouteRegistry::Instance()
{
    static FeatureRouteRegistry registry;
    return registry;
}

bool FeatureRouteRegistry::AddModuleRoutes(ModuleRouteSpec spec)
{
    const std::scoped_lock lock(m_mutex);
    const bool clash = spec.urlPrefix.empty() || std::any_of(m_routes.begin(), m_routes.end(),
        [&spec](const ModuleRouteSpec& known) { return known.urlPrefix == spec.urlPrefix; });
    if (!clash) {
        m_routes.push_back(std::move(spec));
    }
    return !clash;
}

bool FeatureRouteRegistry::AddDebugAlias(DebugRouteAlias alias)
{
    const std::scoped_lock lock(m_mutex);
    const bool clash = alias.path.empty() || alias.featureName.empty() || alias.action.empty()
        || std::any_of(m_aliases.begin(), m_aliases.end(), [&alias](const DebugRouteAlias& known) { return known.path == alias.path; });
    if (!clash) {
        m_aliases.push_back(std::move(alias));
    }
    return !clash;
}

std::vector<ModuleRouteSpec> FeatureRouteRegistry::ModuleRoutes() const
{
    const std::scoped_lock lock(m_mutex);
    return m_routes;
}

std::vector<DebugRouteAlias> FeatureRouteRegistry::DebugAliases() const
{
    const std::scoped_lock lock(m_mutex);
    return m_aliases;
}

FeatureRouteAutoRegister::FeatureRouteAutoRegister(
    const char* urlPrefix, const char* actionPrefix, std::initializer_list<const char*> readOnlyActions)
{
    ModuleRouteSpec spec;
    spec.urlPrefix = urlPrefix;
    spec.actionPrefix = actionPrefix;
    for (const char* name : readOnlyActions) {
        spec.readOnlyActions.emplace_back(name);
    }
    FeatureRouteRegistry::Instance().AddModuleRoutes(std::move(spec));
}

DebugRouteAliasAutoRegister::DebugRouteAliasAutoRegister(const char* path, const char* featureName, const char* action)
{
    FeatureRouteRegistry::Instance().AddDebugAlias({ path, featureName, action });
}

} // namespace gte
