#pragma once

// Owns the live water world of the editor: ticks it once per frame, serves the water panel state, the
// water HTTP commands (actions that start with "water.") and the body gizmos over the view images.

#include "WaterWorld.h"
#include "../../../Editor/EditorFeatureHooks.h"
#include "../../../Editor/EditorFeatureModule.h"

#include <memory>

namespace gte {

class Core;
struct TerrainUiBridge;
struct WaterUiBridge;

inline constexpr const char* kWaterModuleName = "WaterWorld";

class WaterModule final : public IEditorFeatureModule, private IEditorViewHook {
public:
    explicit WaterModule(Core& core);
    ~WaterModule() override;

    const char* ModuleName() const override { return kWaterModuleName; }
    int TickOrder() const noexcept override { return 10; }
    void OnAttach(const EditorModuleAttach& attach) override;
    void OnFrameTick(const EditorFrameTick& tick) override;
    std::string_view CommandPrefix() const noexcept override { return "water."; }
    std::optional<ModuleCommandResult> OnCommand(const ModuleCommandRequest& request) override;
    void OnShutdown() override;

    // Null after shutdown.
    water::IWaterWorld* World() noexcept { return m_world.get(); }

private:
    bool ClaimsPointer() const noexcept override;
    void OnViewOverlay(const EditorViewContext& view) override;
    WaterWorldServices MakeServices();

    const IEditorModuleDirectory* m_modules = nullptr;
    EditorFeatureHooks* m_hooks = nullptr;
    WaterUiBridge* m_ui = nullptr;
    const TerrainUiBridge* m_terrainUi = nullptr;
    std::unique_ptr<WaterWorld> m_world;
};

} // namespace gte
