#pragma once

#include "../../Core/Plugins/IEngineFeatureModule.h"
#include "../../Game/SceneQuery.h"
#include "../../Renderer/RenderGraph/BuildRetryGate.h"
#include "../../Renderer/RenderGraph/Shader.h"
#include "../../Renderer/SceneServicesDescriptorSet.h"
#include "ShadowMaskRenderer.h"
#include "ShadowTypes.h"

#include <cstdint>

namespace gte {

class Core;

// One sun, one shadow map: a PreOpaque depth pass, a PostOpaque compute mask pass and a PostComposite darken pass.
class ShadowFeature final : public IEngineFeatureModule {
public:
    explicit ShadowFeature(Core& core);

    ShadowFeature(const ShadowFeature&) = delete;
    ShadowFeature& operator=(const ShadowFeature&) = delete;
    ShadowFeature(ShadowFeature&&) = delete;
    ShadowFeature& operator=(ShadowFeature&&) = delete;

    const char* ModuleName() const override { return kShadowFeatureName; }

    ShadowSettings& Settings() noexcept { return m_settings; }

private:
    void RegisterPasses();
    void EnsureDepthPipelinesBuilt(Renderer& renderer, std::uint64_t frame);

    Core& m_core;
    ShadowSettings m_settings;

    // One depth-only pipeline per supported vertex layout; instanced layouts are not shadow-cast.
    PipelineOverrideSet m_depthPipelines;
    bool m_depthPipelinesBuilt = false;
    rg::BuildRetryGate m_depthPipelineGate;

    // Locked on the depth pass's first frame (see ShadowTypes.h).
    bool m_mapResolutionLocked = false;
    bool m_mapResolutionMismatchWarned = false;
    std::uint32_t m_mapResolutionInUse = 0;

    // Scene Services slot this feature publishes its depth map into.
    std::uint32_t m_shadowMapSlot = kInvalidSceneServiceSlotIndex;

    ShadowMaskRenderer m_maskRenderer;
    rg::Shader m_compositeShader{ "ShadowComposite", rg::ShaderKind::Graphics };
};

} // namespace gte
