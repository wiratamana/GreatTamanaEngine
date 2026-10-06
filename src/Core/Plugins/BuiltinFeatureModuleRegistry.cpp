#include "BuiltinFeatureModuleRegistry.h"

#include "../Logging.h"

#include <cassert>
#include <exception>

namespace gte {

BuiltinFeatureModuleRegistry& BuiltinFeatureModuleRegistry::Instance()
{
    static BuiltinFeatureModuleRegistry s_instance;
    return s_instance;
}

void BuiltinFeatureModuleRegistry::RegisterFactory(const char* debugName, EngineFeatureModuleFactory factory)
{
    for (const FactoryEntry& entry : m_factories) {
        if (entry.debugName == debugName) {
            GTE_LOG_ERROR("BuiltinFeatureModuleRegistry",
                std::string("RegisterFactory: duplicate name '") + debugName + "' - refused.");
            assert(false && "BuiltinFeatureModuleRegistry: duplicate name - see GTE_LOG_ERROR above.");
            return;
        }
    }
    m_factories.push_back({ debugName, factory });
}

std::vector<std::unique_ptr<IEngineFeatureModule>> BuiltinFeatureModuleRegistry::CreateAll(Core& core) const
{
    std::vector<std::unique_ptr<IEngineFeatureModule>> modules;
    modules.reserve(m_factories.size());
    for (const FactoryEntry& entry : m_factories) {
        try {
            modules.push_back(entry.factory(core));
        } catch (const std::exception& ex) {
            GTE_LOG_ERROR_BLOCKING("BuiltinFeatureModuleRegistry",
                std::string("CreateAll: factory '") + entry.debugName + "' threw during construction - skipped: "
                    + ex.what());
        } catch (...) {
            GTE_LOG_ERROR_BLOCKING("BuiltinFeatureModuleRegistry",
                std::string("CreateAll: factory '") + entry.debugName
                    + "' threw a non-std::exception during construction - skipped.");
        }
    }
    return modules;
}

} // namespace gte
