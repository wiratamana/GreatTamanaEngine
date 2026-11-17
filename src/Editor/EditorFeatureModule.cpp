#include "EditorFeatureModule.h"

#include "../Core/FeatureKit/FeatureRegistration.h"
#include "../Core/Logging.h"

#include <algorithm>
#include <exception>
#include <string>

namespace gte {

fx::IFeatureDebugEndpoint* IEditorModuleDirectory::FindEndpoint(std::string_view featureName) const
{
    fx::Feature* const feature = FindFeature(featureName);
    return feature != nullptr ? feature->DebugEndpoint() : nullptr;
}

EditorModuleSet::~EditorModuleSet()
{
    for (auto it = m_ordered.rbegin(); it != m_ordered.rend(); ++it) {
        try {
            (*it)->OnShutdown();
        } catch (const std::exception& e) {
            GTE_LOG_ERROR("EditorModuleSet", std::string("OnShutdown of '") + (*it)->ModuleName() + "' threw: " + e.what());
        } catch (...) {
            GTE_LOG_ERROR("EditorModuleSet", std::string("OnShutdown of '") + (*it)->ModuleName() + "' threw.");
        }
    }
}

void EditorModuleSet::Attach(const std::vector<std::unique_ptr<IEngineFeatureModule>>& modules, EditorExtensions* extensions)
{
    m_all = &modules;
    m_ordered.clear();
    for (const std::unique_ptr<IEngineFeatureModule>& module : modules) {
        if (auto* const editorModule = dynamic_cast<IEditorFeatureModule*>(module.get())) {
            m_ordered.push_back(editorModule);
        }
    }
    std::stable_sort(m_ordered.begin(), m_ordered.end(),
        [](const IEditorFeatureModule* a, const IEditorFeatureModule* b) { return a->TickOrder() < b->TickOrder(); });
    const EditorModuleAttach attach{ *this, extensions };
    for (IEditorFeatureModule* module : m_ordered) {
        module->OnAttach(attach);
    }
}

void EditorModuleSet::ApplyUiRequests()
{
    for (IEditorFeatureModule* module : m_ordered) {
        module->OnUiRequests();
    }
}

void EditorModuleSet::FrameTick(const EditorFrameTick& tick)
{
    for (IEditorFeatureModule* module : m_ordered) {
        module->OnFrameTick(tick);
    }
}

ModuleCommandResult EditorModuleSet::RunCommand(const ModuleCommandRequest& request)
{
    // Modules with a matching prefix first, then the ones without a prefix.
    for (const bool prefixed : { true, false }) {
        for (IEditorFeatureModule* module : m_ordered) {
            const std::string_view prefix = module->CommandPrefix();
            if (prefixed ? (prefix.empty() || request.action.compare(0, prefix.size(), prefix) != 0) : !prefix.empty()) {
                continue;
            }
            if (std::optional<ModuleCommandResult> result = module->OnCommand(request)) {
                return std::move(*result);
            }
        }
    }
    ModuleCommandResult notFound;
    notFound.httpStatus = 404;
    notFound.body = "{\"success\":false,\"error\":\"no module handles the action '" + request.action + "'\"}";
    return notFound;
}

IEngineFeatureModule* EditorModuleSet::FindModule(std::string_view moduleName) const
{
    if (m_all == nullptr) {
        return nullptr;
    }
    for (const std::unique_ptr<IEngineFeatureModule>& module : *m_all) {
        if (module != nullptr && moduleName == module->ModuleName()) {
            return module.get();
        }
    }
    return nullptr;
}

fx::Feature* EditorModuleSet::FindFeature(std::string_view featureName) const
{
    if (m_all == nullptr) {
        return nullptr;
    }
    for (const std::unique_ptr<IEngineFeatureModule>& module : *m_all) {
        fx::Feature* const feature = module ? fx::FindFeature(*module) : nullptr;
        if (feature != nullptr && featureName == feature->Name()) {
            return feature;
        }
    }
    return nullptr;
}

} // namespace gte
