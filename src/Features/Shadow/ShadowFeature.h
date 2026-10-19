#pragma once

#include "ShadowDebugEndpoint.h"
#include "ShadowMath.h"
#include "ShadowTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

#include <array>
#include <cstdint>

namespace gte {

// Per-view resources.
struct ShadowViewState {
    std::array<fx::OffscreenTarget, kMaxShadowCascades> maps{
        fx::OffscreenTarget{ "Shadow.Cascade0" },
        fx::OffscreenTarget{ "Shadow.Cascade1" },
        fx::OffscreenTarget{ "Shadow.Cascade2" },
        fx::OffscreenTarget{ "Shadow.Cascade3" },
    };
    fx::PersistentTarget mask{ "Shadow.Mask" };
    fx::PersistentTarget cascadeId{ "Shadow.CascadeId" };
    fx::GpuParams<ShadowMaskParamsGpu> params;
    fx::GpuReadback<ShadowDepthRangeGpu> depthRange{ "Shadow.DepthRange" };
    ShadowSdsmState sdsm;
    ShadowViewSnapshot snapshot;
};

// Cascaded shadow maps fitted to the camera: PreOpaque cascade depth passes, a PostOpaque depth-range
// reduction, a PostOpaque compute mask pass and a PostComposite darken pass.
class ShadowFeature final : public fx::Feature {
public:
    explicit ShadowFeature(fx::FeatureHost& host);

    const char* Name() const override { return kShadowFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const ShadowSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const ShadowSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    ShadowViewSnapshot Snapshot(rg::ViewScope scope) const;

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void CascadesPass(fx::PreOpaqueFrame& f);
    void DepthRangePass(fx::PostOpaqueFrame& f);
    void MaskPass(fx::PostOpaqueFrame& f);
    void CompositePass(fx::CompositeFrame& f);

    ShadowSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    ShadowDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<ShadowViewState> m_views;
    fx::ComputeShader m_maskShader{ "ShadowMask" };
    fx::ComputeShader m_depthReduceShader{ "ShadowDepthReduce" };
    fx::FullscreenShader m_compositeShader{ "ShadowComposite" };
    fx::SceneShaderSet m_depthShaders = fx::SceneShaderSet::DepthOnly("ShadowDepth");
    fx::Channel<ShadowMapData> m_mapData{ "Map" };
    fx::Channel<SunShadowReceiver> m_receiverData{ kSunShadowReceiverChannel };
    fx::Channel<fx::Texture> m_maskData{ "Mask" };
    fx::Channel<fx::Texture> m_cascadeIdData{ "CascadeId" };
};

} // namespace gte
