#pragma once

#include "ToneMappingDebugEndpoint.h"
#include "ToneMappingTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

#include <cstdint>

namespace gte {

// Per-view last frame report.
struct ToneMappingViewState {
    ToneMappingViewSnapshot snapshot;
};

// Display transform: the last PostComposite step turns linear scene color into display values.
class ToneMappingFeature final : public fx::Feature {
public:
    explicit ToneMappingFeature(fx::FeatureHost& host);

    const char* Name() const override { return kToneMappingFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const ToneMappingSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const ToneMappingSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    ToneMappingViewSnapshot Snapshot(rg::ViewScope scope) const;

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void CompositePass(fx::CompositeFrame& f);

    ToneMappingSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    ToneMappingDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<ToneMappingViewState> m_views;
    fx::ComputeShader m_shader{ "ToneMapping" };
    fx::ViewOutput m_output{ "ToneMapped" };
};

} // namespace gte
