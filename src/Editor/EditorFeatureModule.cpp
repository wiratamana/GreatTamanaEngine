#include "EditorFeatureModule.h"

#include "EditorExtensions.h"
#include "EditorFeatureHooks.h"
#include "../Core/FeatureKit/FeatureRegistration.h"
#include "../Core/Logging.h"
#include "../Profiling/ScopeTimer.h"

#include <algorithm>
#include <exception>
#include <string>
#include <string_view>

namespace gte {

namespace {

constexpr unsigned kHookUiRequests = 1u;
constexpr unsigned kHookFrameTick = 2u;
constexpr unsigned kHookCommand = 4u;

std::string JsonEscape(std::string_view text)
{
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20u) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

ModuleCommandResult FailedCommandResult(std::string_view moduleName, std::string_view what)
{
    ModuleCommandResult failed;
    failed.httpStatus = 500;
    failed.body = "{\"success\":false,\"error\":\"module '" + JsonEscape(moduleName) + "' threw: " + JsonEscape(what) + "\"}";
    return failed;
}

} // namespace

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
    m_uiRequestScopes.clear();
    m_frameTickScopes.clear();
    m_reportedHooks.assign(m_ordered.size(), 0u);
    for (const IEditorFeatureModule* module : m_ordered) {
        const std::string name = std::string("EditorModule.") + std::string(module->ModuleName());
        m_uiRequestScopes.push_back(Profiling::InternName((name + ".UiRequests").c_str()));
        m_frameTickScopes.push_back(Profiling::InternName((name + ".FrameTick").c_str()));
    }
    const EditorModuleAttach attach{ *this, extensions, extensions != nullptr ? &extensions->Get<EditorFeatureHooks>() : nullptr };
    for (IEditorFeatureModule* module : m_ordered) {
        module->OnAttach(attach);
    }
}

void EditorModuleSet::ReportThrow(std::size_t index, unsigned hookBit, const char* hook, const char* what)
{
    if ((m_reportedHooks[index] & hookBit) != 0u) {
        return;
    }
    m_reportedHooks[index] |= hookBit;
    GTE_LOG_ERROR("EditorModuleSet", std::string(hook) + " of '" + m_ordered[index]->ModuleName() + "' threw: " + what
        + " (reported once, the frame continues)");
}

void EditorModuleSet::ApplyUiRequests()
{
    for (std::size_t i = 0; i < m_ordered.size(); ++i) {
        GTE_PROFILE_SCOPE_ID(m_uiRequestScopes[i]);
        try {
            m_ordered[i]->OnUiRequests();
        } catch (const std::exception& e) {
            ReportThrow(i, kHookUiRequests, "OnUiRequests", e.what());
        } catch (...) {
            ReportThrow(i, kHookUiRequests, "OnUiRequests", "unknown exception");
        }
    }
}

void EditorModuleSet::FrameTick(const EditorFrameTick& tick)
{
    for (std::size_t i = 0; i < m_ordered.size(); ++i) {
        GTE_PROFILE_SCOPE_ID(m_frameTickScopes[i]);
        try {
            m_ordered[i]->OnFrameTick(tick);
        } catch (const std::exception& e) {
            ReportThrow(i, kHookFrameTick, "OnFrameTick", e.what());
        } catch (...) {
            ReportThrow(i, kHookFrameTick, "OnFrameTick", "unknown exception");
        }
    }
}

ModuleCommandResult EditorModuleSet::RunCommand(const ModuleCommandRequest& request)
{
    // Modules with a matching prefix first, then the ones without a prefix.
    for (const bool prefixed : { true, false }) {
        for (std::size_t i = 0; i < m_ordered.size(); ++i) {
            IEditorFeatureModule* const module = m_ordered[i];
            const std::string_view prefix = module->CommandPrefix();
            if (prefixed ? (prefix.empty() || request.action.compare(0, prefix.size(), prefix) != 0) : !prefix.empty()) {
                continue;
            }
            try {
                if (std::optional<ModuleCommandResult> result = module->OnCommand(request)) {
                    return std::move(*result);
                }
            } catch (const std::exception& e) {
                ReportThrow(i, kHookCommand, "OnCommand", e.what());
                return FailedCommandResult(module->ModuleName(), e.what());
            } catch (...) {
                ReportThrow(i, kHookCommand, "OnCommand", "unknown exception");
                return FailedCommandResult(module->ModuleName(), "unknown exception");
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
