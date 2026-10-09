#pragma once

#include "../../Core/Plugins/IEngineFeatureModule.h"
#include "ShadowTypes.h"
#include "ShadowMaskRenderer.h"
#include "ShadowCompositeRenderer.h"

#include "../../Game/SceneQuery.h"
#include "../../Renderer/Pipeline.h"
#include "../../Renderer/PipelineHandle.h"
#include "../../Renderer/SceneServicesDescriptorSet.h"

namespace gte {

class Core;

// One sun, one shadow map: a PreOpaque depth pass, a PostOpaque compute
// mask pass, and a PostComposite darken pass. See ShadowMath.h for the
// light-space math and ShadowTypes.h for tunable settings.
class ShadowFeature final : public IEngineFeatureModule {
public:
    explicit ShadowFeature(Core& core);

    ShadowFeature(const ShadowFeature&) = delete;
    ShadowFeature& operator=(const ShadowFeature&) = delete;
    ShadowFeature(ShadowFeature&&) = delete;
    ShadowFeature& operator=(ShadowFeature&&) = delete;

    const char* ModuleName() const override { return "Shadow"; }

    ShadowSettings& Settings() noexcept { return m_settings; }

private:
    void RegisterPasses();
    void EnsureDepthPipelinesBuilt(Renderer& renderer);

    Core& m_core;
    ShadowSettings m_settings;

    // Lazily built, process-lifetime - one entry per supported vertex layout
    // (PositionColor/PositionNormal/PositionNormalUv - PositionNormalInstanced
    // intentionally left unset, see EnsureDepthPipelinesBuilt()'s own comment).
    // Owned by RenderSystem's own Pipeline pool.
    PipelineOverrideSet m_depthPipelines;
    bool m_depthPipelinesBuilt = false;

    // Retry backoff for a persistently failing build - avoids retrying at
    // full frame rate forever when the underlying failure never clears (e.g.
    // a broken shader directory).
    std::uint32_t m_depthPipelineFailedAttempts = 0;
    std::uint32_t m_depthPipelineRetryCooldownFramesRemaining = 0;
    bool m_depthPipelineGaveUp = false; // Logged once, stops all further retries.

    // Locked on the depth pass's first frame - see ShadowTypes.h.
    bool m_mapResolutionLocked = false;
    bool m_mapResolutionMismatchWarned = false; // Logged at most once per process.
    std::uint32_t m_mapResolutionInUse = 0;

    // Scene Services slot index this feature publishes its depth map into -
    // see SceneServicesDescriptorSet.h. Registered once, in the constructor.
    std::uint32_t m_shadowMapSlot = kInvalidSceneServiceSlotIndex;

    ShadowMaskRenderer m_maskRenderer;
    ShadowCompositeRenderer m_compositeRenderer;
};

} // namespace gte
