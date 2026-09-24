#include "PluginRenderFeatureDiagnostics.h"

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/IRenderFeatureModule.h"

namespace gte {

int CountModulesImplementingRenderFeature(const std::vector<IPluginModule*>& modules)
{
    int count = 0;
    for (IPluginModule* module : modules) {
        if (module != nullptr && module->QueryCapability(kIRenderFeatureModule_v1_Name) != nullptr) {
            ++count;
        }
    }
    return count;
}

} // namespace gte
