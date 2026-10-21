#pragma once

#include "GtaoDebugEndpoint.h"
#include "GtaoTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

#include <array>
#include <cstdint>

namespace gte {

// Per-view resources and last-frame memory. Parameter blocks are per view on purpose.
struct GtaoViewState {
    fx::PersistentTarget raw{ "Gtao.Raw" };
    std::array<fx::PersistentTarget, 2> history{
        fx::PersistentTarget{ "Gtao.History0" },
        fx::PersistentTarget{ "Gtao.History1" },
    };
    fx::GpuParams<GtaoMainParamsGpu> mainParams;
    fx::GpuParams<fx::TemporalResolveParamsGpu> resolveParams;
    fx::TemporalHistory previous;
    GtaoViewSnapshot snapshot;
};

// Screen-space ambient occlusion: PostOpaque compute passes (sample, denoise) and a PostComposite multiply.
class GtaoFeature final : public fx::Feature {
public:
    explicit GtaoFeature(fx::FeatureHost& host);

    const char* Name() const override { return kGtaoFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const GtaoSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const GtaoSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    GtaoViewSnapshot Snapshot(rg::ViewScope scope) const;

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void OcclusionPass(fx::PostOpaqueFrame& f);
    void CompositePass(fx::CompositeFrame& f);

    GtaoSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    GtaoDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<GtaoViewState> m_views;
    fx::ComputeShader m_mainShader{ "GtaoMain" };
    fx::ComputeShader m_resolveShader{ "GtaoResolve" };
    fx::FullscreenShader m_compositeShader{ "GtaoComposite" };
    fx::Channel<GtaoResult> m_result{ "Result" };
};

} // namespace gte
