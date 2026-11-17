#pragma once

// Editor-tier hooks for a feature that owns live editor objects (a world, a worker, a panel mailbox)
// in addition to, or instead of, a render feature. All hooks are optional and run on the main thread.

#include "../Application/ModuleCommandBridge.h"
#include "../Core/FeatureKit/FeatureDebug.h"
#include "../Core/Plugins/IEngineFeatureModule.h"
#include "../Profiling/NameTable.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace gte {

class EditorExtensions;
class EditorFeatureHooks;

namespace fx { class Feature; }

// Finds the other built-in feature modules. Resolve on every use; never cache a result across frames.
class IEditorModuleDirectory {
public:
    virtual ~IEditorModuleDirectory() = default;

    virtual IEngineFeatureModule* FindModule(std::string_view moduleName) const = 0;

    // The built-in render feature with that name, or null.
    virtual fx::Feature* FindFeature(std::string_view featureName) const = 0;

    template <typename T>
    T* Find(std::string_view moduleName) const
    {
        return dynamic_cast<T*>(FindModule(moduleName));
    }

    // Debug endpoint of the render feature with that name, or null.
    fx::IFeatureDebugEndpoint* FindEndpoint(std::string_view featureName) const;
};

struct EditorModuleAttach {
    const IEditorModuleDirectory& modules;
    EditorExtensions* extensions = nullptr; // Null when there is no editor UI.
    EditorFeatureHooks* hooks = nullptr;    // View and menu hooks; null when there is no editor UI.
};

struct EditorFrameTick {
    double nowSeconds = 0.0;
    double deltaSeconds = 0.0;
};

class IEditorFeatureModule : public IEngineFeatureModule {
public:
    // Modules run in ascending order, ties in creation order.
    virtual int TickOrder() const noexcept { return 0; }

    // Once, after every module exists and before the first frame.
    virtual void OnAttach(const EditorModuleAttach& /*attach*/) {}

    // Every frame before the game update: apply what the UI panels asked for.
    virtual void OnUiRequests() {}

    // Every frame after the game update and before the frame is built.
    virtual void OnFrameTick(const EditorFrameTick& /*tick*/) {}

    // Actions that start with this text are offered to this module first. Empty means "all the rest".
    virtual std::string_view CommandPrefix() const noexcept { return {}; }

    // Runs one command; nullopt means "not mine".
    virtual std::optional<ModuleCommandResult> OnCommand(const ModuleCommandRequest& /*request*/) { return std::nullopt; }

    // Once before any module is destroyed, in reverse tick order. Every module and Core are still alive.
    virtual void OnShutdown() {}
};

// The ordered editor modules among the built-in feature modules. Declare it after the module list it refers to.
class EditorModuleSet final : public IEditorModuleDirectory {
public:
    EditorModuleSet() = default;
    ~EditorModuleSet() override;
    EditorModuleSet(const EditorModuleSet&) = delete;
    EditorModuleSet& operator=(const EditorModuleSet&) = delete;

    // `modules` must outlive this set. Sorts the editor modules and calls OnAttach on each.
    void Attach(const std::vector<std::unique_ptr<IEngineFeatureModule>>& modules, EditorExtensions* extensions);

    void ApplyUiRequests();
    void FrameTick(const EditorFrameTick& tick);

    // The answer of the module that owns the action, a 404 when none does, or a 500 when the module threw.
    ModuleCommandResult RunCommand(const ModuleCommandRequest& request);

    IEngineFeatureModule* FindModule(std::string_view moduleName) const override;
    fx::Feature* FindFeature(std::string_view featureName) const override;

    std::size_t EditorModuleCount() const noexcept { return m_ordered.size(); }

private:
    // Logs the first throw of each hook of a module; later throws of that hook stay silent.
    void ReportThrow(std::size_t index, unsigned hookBit, const char* hook, const char* what);

    const std::vector<std::unique_ptr<IEngineFeatureModule>>* m_all = nullptr;
    std::vector<IEditorFeatureModule*> m_ordered;
    std::vector<Profiling::NameId> m_uiRequestScopes; // Parallel to m_ordered.
    std::vector<Profiling::NameId> m_frameTickScopes;
    std::vector<unsigned> m_reportedHooks;            // Parallel to m_ordered, one bit per hook.
};

} // namespace gte
