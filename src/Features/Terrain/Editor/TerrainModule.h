#pragma once

// Owns the live terrain world of the editor and drives it once per frame; also serves the terrain panel mailbox
// and the terrain HTTP commands.

#include "TerrainWorld.h"
#include "../../../Editor/EditorFeatureModule.h"

#include <memory>

namespace gte {

class Core;
struct TerrainUiBridge;

inline constexpr const char* kTerrainModuleName = "TerrainWorld";

class TerrainModule final : public IEditorFeatureModule {
public:
    explicit TerrainModule(Core& core);
    ~TerrainModule() override;

    const char* ModuleName() const override { return kTerrainModuleName; }
    int TickOrder() const noexcept override { return 0; }
    void OnAttach(const EditorModuleAttach& attach) override;
    void OnUiRequests() override;
    void OnFrameTick(const EditorFrameTick& tick) override;
    std::optional<ModuleCommandResult> OnCommand(const ModuleCommandRequest& request) override;
    void OnShutdown() override;

    // Null after shutdown.
    terrain::ITerrainWorld* World() noexcept { return m_world.get(); }

private:
    TerrainWorldServices MakeServices();

    Core& m_core;
    const IEditorModuleDirectory* m_modules = nullptr;
    TerrainUiBridge* m_ui = nullptr;
    std::unique_ptr<TerrainWorld> m_world;
};

} // namespace gte
