#pragma once

#include "TemporalAaDebugEndpoint.h"
#include "TemporalAaTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

#include <array>
#include <cstdint>

namespace gte {

// Per-view resources and last-frame memory. The parameter block is per view on purpose.
struct TemporalAaViewState {
    std::array<fx::PersistentTarget, 2> color{ fx::PersistentTarget{ "TemporalAa.Color0" },
        fx::PersistentTarget{ "TemporalAa.Color1" } };
    std::array<fx::PersistentTarget, 2> depth{ fx::PersistentTarget{ "TemporalAa.Depth0" },
        fx::PersistentTarget{ "TemporalAa.Depth1" } };
    fx::GpuParams<TemporalAaParamsGpu> params;
    fx::TemporalHistory previous;
    bool resolvedValid = false; // Heartbeat: the resolve declared its passes in `resolvedFrame`.
    std::uint64_t resolvedFrame = 0;
    TemporalAaViewSnapshot snapshot;
};

// Temporal anti-aliasing: a sub-pixel jitter of the scene draw, resolved against the previous frame before Bloom.
class TemporalAaFeature final : public fx::Feature {
public:
    explicit TemporalAaFeature(fx::FeatureHost& host);

    const char* Name() const override { return kTemporalAaFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const TemporalAaSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const TemporalAaSettings& candidate);

    // The last state of `scope`'s view. The resolve half and the jitter half each age on their own.
    TemporalAaViewSnapshot Snapshot(rg::ViewScope scope) const;

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void JitterPass(fx::PreOpaqueFrame& f);
    void ResolvePass(fx::CompositeFrame& f);

    TemporalAaSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    TemporalAaDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<TemporalAaViewState> m_views;
    fx::ComputeShader m_resolveShader{ "TemporalAaResolve" };
};

} // namespace gte
