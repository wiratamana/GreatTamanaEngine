#pragma once

// Owns the Grass height-tile provider: builds the tile from the live terrain and water and hands it to the
// Grass render feature. Ticks last, after the terrain and the water.

#include "GrassTileProvider.h"
#include "../../../Editor/EditorFeatureModule.h"

#include <memory>

namespace gte {

class Core;

inline constexpr const char* kGrassTilesModuleName = "GrassTiles";

class GrassTilesModule final : public IEditorFeatureModule {
public:
    explicit GrassTilesModule(Core& core);
    ~GrassTilesModule() override;

    const char* ModuleName() const override { return kGrassTilesModuleName; }
    int TickOrder() const noexcept override { return 20; }
    void OnAttach(const EditorModuleAttach& attach) override;
    void OnFrameTick(const EditorFrameTick& tick) override;
    void OnShutdown() override;

private:
    GrassTileServices MakeServices();

    Core& m_core;
    const IEditorModuleDirectory* m_modules = nullptr;
    std::unique_ptr<GrassTileProvider> m_provider;
};

} // namespace gte
