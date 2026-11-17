#include "TerrainModule.h"

#include "TerrainCommands.h"
#include "TerrainPanel.h"
#include "TerrainUiBridge.h"
#include "../../../Core/Core.h"
#include "../../../Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../../Editor/BuiltinFeatureEditorPanelRegistry.h"
#include "../../../Editor/EditorExtensions.h"
#include "../../../Game/Game.h"
#include "../../../Game/RenderSystem.h"

namespace gte {

TerrainModule::TerrainModule(Core& core)
    : m_core(core)
    , m_world(std::make_unique<TerrainWorld>([this] { return MakeServices(); }))
{
}

TerrainModule::~TerrainModule() = default;

TerrainWorldServices TerrainModule::MakeServices()
{
    TerrainWorldServices services;
    services.game = &m_core.GetGame();
    services.renderer = &m_core.GetRenderer();
    services.sceneServicesSetLayout = m_core.GetSceneServicesDescriptorSet().LayoutHandle();
    services.compositor = m_core.GetRenderFeatureCompositor();
    services.findEndpoint = [this](std::string_view name) -> fx::IFeatureDebugEndpoint* {
        return m_modules != nullptr ? m_modules->FindEndpoint(name) : nullptr;
    };
    services.gpuDrivenScene = services.findEndpoint("GpuDrivenScene");
    return services;
}

void TerrainModule::OnAttach(const EditorModuleAttach& attach)
{
    m_modules = &attach.modules;
    m_ui = attach.extensions != nullptr ? &attach.extensions->Get<TerrainUiBridge>() : nullptr;
}

void TerrainModule::OnUiRequests()
{
    if (m_world != nullptr && m_ui != nullptr) {
        ApplyTerrainUiRequest(*m_world, *m_ui);
    }
}

void TerrainModule::OnFrameTick(const EditorFrameTick& tick)
{
    if (m_world == nullptr) {
        return;
    }
    std::optional<Mat4> viewProjection;
    if (m_world->IsOpen()) {
        TerrainTickContext context;
        if (const std::optional<fx::View> primary = m_core.PrimaryCameraView(context.aspect);
            primary.has_value() && primary->extent.width > 0 && primary->extent.height > 0) {
            context.aspect = static_cast<float>(primary->extent.width) / static_cast<float>(primary->extent.height);
        }
        EngineContext& engine = m_core.GetEngineContext();
        context.gpuFrame = engine.gpuFrame;
        context.nowSeconds = tick.nowSeconds;
        context.deltaSeconds = tick.deltaSeconds;
        context.frameIndex = engine.time.FrameCount();
        m_world->Tick(context);
        if (m_ui != nullptr && m_world->IsOpen()) {
            viewProjection = RenderSystem::ResolveActiveCameraViewProjection(m_core.GetGame().GetRegistry(), context.aspect);
        }
    }
    if (m_ui != nullptr) {
        PublishTerrainSnapshot(*m_world, *m_ui, viewProjection ? &*viewProjection : nullptr);
    }
}

std::optional<ModuleCommandResult> TerrainModule::OnCommand(const ModuleCommandRequest& request)
{
    if (m_world == nullptr) {
        return std::nullopt;
    }
    return ExecuteTerrainCommand(*m_world, request);
}

void TerrainModule::OnShutdown()
{
    m_world.reset();
}

namespace {

std::unique_ptr<IEngineFeatureModule> MakeTerrainModule(Core& core)
{
    return std::make_unique<TerrainModule>(core);
}

std::unique_ptr<EditorWindow> CreateTerrainPanel(
    IEngineFeatureModule&, EditorContext&, Renderer&, const rg::RenderGraph&)
{
    return std::make_unique<TerrainPanel>();
}

} // namespace

GTE_REGISTER_BUILTIN_FEATURE_MODULE("TerrainWorld", &MakeTerrainModule);
GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("TerrainWorld", &CreateTerrainPanel);

} // namespace gte
