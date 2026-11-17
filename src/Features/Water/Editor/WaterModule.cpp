#include "WaterModule.h"

#include "WaterCommands.h"
#include "WaterPanel.h"
#include "WaterUiBridge.h"
#include "../../Terrain/Editor/TerrainModule.h"
#include "../../../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"
#include "../../../Editor/EditorExtensions.h"

namespace gte {

WaterModule::WaterModule(Core& /*core*/)
    : m_world(std::make_unique<WaterWorld>([this] { return MakeServices(); }))
{
}

WaterModule::~WaterModule() = default;

WaterWorldServices WaterModule::MakeServices()
{
    WaterWorldServices services;
    if (m_modules != nullptr) {
        if (TerrainModule* const terrainModule = m_modules->Find<TerrainModule>(kTerrainModuleName)) {
            services.terrain = terrainModule->World();
        }
        services.waterFeature = m_modules->FindEndpoint("Water");
    }
    return services;
}

void WaterModule::OnAttach(const EditorModuleAttach& attach)
{
    m_modules = &attach.modules;
    m_ui = attach.extensions != nullptr ? &attach.extensions->Get<WaterUiBridge>() : nullptr;
}

void WaterModule::OnFrameTick(const EditorFrameTick& tick)
{
    if (m_world == nullptr) {
        return;
    }
    if (m_ui != nullptr) {
        m_ui->world = m_world.get();
    }
    m_world->Tick(tick.nowSeconds);
}

std::optional<ModuleCommandResult> WaterModule::OnCommand(const ModuleCommandRequest& request)
{
    if (m_world == nullptr) {
        return std::nullopt;
    }
    return ExecuteWaterCommand(*m_world, request);
}

void WaterModule::OnShutdown()
{
    if (m_ui != nullptr) {
        m_ui->world = nullptr;
    }
    m_world.reset();
}

namespace {

std::unique_ptr<IEngineFeatureModule> MakeWaterModule(Core& core)
{
    return std::make_unique<WaterModule>(core);
}

std::unique_ptr<EditorWindow> CreateWaterPanel(
    IEngineFeatureModule&, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<WaterPanel>();
}

} // namespace

GTE_REGISTER_BUILTIN_FEATURE_MODULE("WaterWorld", &MakeWaterModule);
GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("WaterWorld", &CreateWaterPanel);

} // namespace gte
