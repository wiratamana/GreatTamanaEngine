#include "LegacyRenderFeatureOrchestrator.h"

#include "../Core.h"
#include "../Logging.h"
#include "PluginRenderFeatureDiagnostics.h"
#include "PluginRenderPassBuilderAdapter.h"

#include "../../../plugins/gte_plugin_abi/IPluginRenderPassBuilder.h"
#include "../../../plugins/gte_plugin_abi/IRenderFeatureModule.h"

#include <optional>
#include <string>

namespace gte {

// editor-core-separation-6 campaign, PHASE2 - a VERBATIM relocation of
// Core::LoadPlugins()'s own multi-plugin warning (editor-core-separation-4,
// PHASE5) - same trigger condition (`> 1`), same exact warning text.
void LegacyRenderFeatureOrchestrator::OnPluginsLoaded(const std::vector<IPluginModule*>& modules)
{
    const int renderFeatureModuleCount = CountModulesImplementingRenderFeature(modules);
    if (renderFeatureModuleCount > 1) {
        GTE_LOG_WARNING("PluginHost",
            std::to_string(renderFeatureModuleCount) + " loaded plugins implement IRenderFeatureModule_v1 - "
            "only the LAST-registered one's render output will be visible this frame (render-graph "
            "compositing for multiple render-feature plugins is not implemented - see "
            "docs/conventions/plugin-architecture.md).");
    }
}

// editor-core-separation-6 campaign, PHASE2 - a VERBATIM relocation of the
// "PluginRenderFeatures" provider's own loop body (editor-core-separation-3,
// PHASE3), with the inline isGameView/compositedKey/pluginTarget derivation
// replaced by the new, shared Core::FindPluginRenderFeatureTarget() accessor
// (Core.cpp/Core.h, this same phase). The rest of the loop body is unchanged.
void LegacyRenderFeatureOrchestrator::ContributeRenderGraphPasses(
    const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&)
{
    const std::optional<Core::PluginRenderFeatureTargetInfo> resolved = m_core.FindPluginRenderFeatureTarget(frame);
    if (!resolved.has_value()) {
        return;
    }

    bool anyPluginFeatureRanThisView = false;
    for (IPluginModule* module : m_core.GetPluginHost().AllLoadedModules()) {
        if (auto* feature = static_cast<IRenderFeatureModule_v1*>(
                module->QueryCapability(kIRenderFeatureModule_v1_Name))) {
            // editor-core-separation-21 campaign, PHASE4 - the toggle
            // registry is now threaded through so AddFullscreenClearPass()
            // can honestly consult it (fixing PHASE3's confirmed-lie
            // findings #18/#19) - Core::GetRenderPassToggleRegistryMutable()
            // returns the SAME single registry instance every other
            // built-in/toggle-aware pass in this engine already consults.
            PluginRenderPassBuilderAdapter adapter(
                frame.builder, resolved->target, &m_core.GetRenderPassToggleRegistryMutable());
            feature->AddRenderGraphPasses(adapter);
            anyPluginFeatureRanThisView = true;
        }
    }
    if (anyPluginFeatureRanThisView) {
        frame.finalTextureOutputs.push_back(resolved->target);
    }
}

} // namespace gte
