#pragma once

// Editor module of the Big World demo: File > Open Big World / Close Big World, the /bigworld HTTP routes,
// and the per-frame tick of the Game-layer orchestrator. It is the only place that knows all three worlds.

#include "BigWorldAdapters.h"
#include "../../../Editor/EditorFeatureHooks.h"
#include "../../../Editor/EditorFeatureModule.h"
#include "../../../Game/BigWorld/BigWorldDemo.h"

#include <memory>
#include <optional>

namespace gte {

inline constexpr const char* kBigWorldModuleName = "BigWorld";

class BigWorldModule final : public IEditorFeatureModule {
public:
    BigWorldModule() = default;
    ~BigWorldModule() override;

    const char* ModuleName() const override { return kBigWorldModuleName; }
    int TickOrder() const noexcept override { return 30; } // After terrain, water and the grass tile source.
    std::string_view CommandPrefix() const noexcept override { return "bigworld."; }
    void OnAttach(const EditorModuleAttach& attach) override;
    void OnFrameTick(const EditorFrameTick& tick) override;
    std::optional<ModuleCommandResult> OnCommand(const ModuleCommandRequest& request) override;
    void OnShutdown() override;

    // Null before OnAttach and after shutdown.
    bigworld::BigWorldDemo* Demo() noexcept { return m_demo.get(); }

private:
    EditorActionResult OpenFromMenu();
    EditorActionResult CloseFromMenu();
    ModuleCommandResult OpenCommand(const ModuleCommandRequest& request);
    ModuleCommandResult StateCommand();
    void LogPhaseChange();

    std::optional<TerrainPortAdapter> m_terrain;
    std::optional<WaterPortAdapter> m_water;
    std::optional<FeaturePortAdapter> m_features;
    std::unique_ptr<bigworld::BigWorldDemo> m_demo;
    double m_now = 0.0;
    bigworld::Phase m_loggedPhase = bigworld::Phase::Idle;
};

} // namespace gte
