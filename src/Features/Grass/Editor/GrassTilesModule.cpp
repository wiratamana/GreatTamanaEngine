#include "GrassTilesModule.h"

#include "../../Terrain/Editor/TerrainModule.h"
#include "../../Water/Editor/WaterModule.h"
#include "../../../Application/FeatureRouteRegistry.h"
#include "../../../Core/Core.h"
#include "../../../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../../Game/Game.h"
#include "../../../Game/RenderSystem.h"

namespace gte {

GrassTilesModule::GrassTilesModule(Core& core)
    : m_core(core)
    , m_provider(std::make_unique<GrassTileProvider>([this] { return MakeServices(); }))
{
}

GrassTilesModule::~GrassTilesModule() = default;

GrassTileServices GrassTilesModule::MakeServices()
{
    GrassTileServices services;
    if (m_modules == nullptr) {
        return services;
    }
    if (TerrainModule* const terrainModule = m_modules->Find<TerrainModule>(kTerrainModuleName)) {
        services.terrain = terrainModule->World();
    }
    if (WaterModule* const waterModule = m_modules->Find<WaterModule>(kWaterModuleName)) {
        services.water = waterModule->World();
    }
    services.grass = dynamic_cast<GrassFeature*>(m_modules->FindFeature(kGrassFeatureName));
    return services;
}

void GrassTilesModule::OnAttach(const EditorModuleAttach& attach)
{
    m_modules = &attach.modules;
}

void GrassTilesModule::OnFrameTick(const EditorFrameTick& tick)
{
    if (m_provider == nullptr) {
        return;
    }
    const GrassTileServices services = MakeServices();
    if (services.terrain != nullptr && services.terrain->IsOpen()) {
        const Vec3 eye = RenderSystem::ResolveActiveCameraWorldPosition(m_core.GetGame().GetRegistry());
        m_provider->Tick(tick.nowSeconds, static_cast<double>(eye.x), static_cast<double>(eye.z));
    } else {
        m_provider->Tick(tick.nowSeconds, 0.0, 0.0);
    }
}

void GrassTilesModule::OnShutdown()
{
    m_provider.reset();
}

namespace {

std::unique_ptr<IEngineFeatureModule> MakeGrassTilesModule(Core& core)
{
    return std::make_unique<GrassTilesModule>(core);
}

} // namespace

GTE_REGISTER_BUILTIN_FEATURE_MODULE("GrassTiles", &MakeGrassTilesModule);
// Short routes: the height tile source and the root height checks.
GTE_REGISTER_DEBUG_ROUTE_ALIAS("/grass/tile", "Grass", "tile");
GTE_REGISTER_DEBUG_ROUTE_ALIAS("/grass/probe", "Grass", "probe");
GTE_REGISTER_DEBUG_ROUTE_ALIAS("/grass/probe_batch", "Grass", "probe_batch");

} // namespace gte
