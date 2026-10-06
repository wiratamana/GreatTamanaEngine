#pragma once

#include "../../Core/Plugins/IEngineFeatureModule.h"
#include "ShadowTypes.h"
#include "ShadowMaskRenderer.h"
#include "ShadowCompositeRenderer.h"

#include "../../Renderer/Pipeline.h"
#include "../../Renderer/PipelineHandle.h"

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
    void EnsureDepthPipelineBuilt(Renderer& renderer);

    Core& m_core;
    ShadowSettings m_settings;

    // Lazily built, process-lifetime - owned by RenderSystem's own pool.
    PipelineHandle m_depthPipeline;

    // Locked on Shadow.DepthPass's first frame - see ShadowTypes.h.
    bool m_mapResolutionLocked = false;
    bool m_mapResolutionMismatchWarned = false; // Logged at most once per process.
    std::uint32_t m_mapResolutionInUse = 0;

    ShadowMaskRenderer m_maskRenderer;
    ShadowCompositeRenderer m_compositeRenderer;
};

} // namespace gte
