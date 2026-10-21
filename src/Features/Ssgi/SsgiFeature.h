#pragma once

#include "SsgiDebugEndpoint.h"
#include "SsgiTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

#include <array>
#include <cstdint>

namespace gte {

// Per-view resources and last-frame memory. Parameter blocks are per view on purpose.
struct SsgiViewState {
    fx::PersistentTarget color{ "Ssgi.Color" };   // GI size: radiance + view depth.
    fx::PersistentTarget normal{ "Ssgi.Normal" }; // GI size: world normals.
    fx::PersistentTarget raw{ "Ssgi.Raw" };       // GI size: trace output.
    std::array<fx::PersistentTarget, 2> history{
        fx::PersistentTarget{ "Ssgi.History0" },
        fx::PersistentTarget{ "Ssgi.History1" },
    };
    fx::GpuParams<SsgiPrepareParamsGpu> prepareParams;
    fx::GpuParams<SsgiTraceParamsGpu> traceParams;
    fx::GpuParams<fx::TemporalResolveParamsGpu> resolveParams;
    fx::TemporalHistory previous;
    SsgiViewSnapshot snapshot;
};

// Screen-space diffuse bounce light: PostOpaque compute passes (prepare, trace, resolve) and a PostComposite add.
class SsgiFeature final : public fx::Feature {
public:
    explicit SsgiFeature(fx::FeatureHost& host);

    const char* Name() const override { return kSsgiFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const SsgiSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const SsgiSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    SsgiViewSnapshot Snapshot(rg::ViewScope scope) const;

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void TracePass(fx::PostOpaqueFrame& f);
    void CompositePass(fx::CompositeFrame& f);

    SsgiSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    SsgiDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<SsgiViewState> m_views;
    fx::ComputeShader m_prepareShader{ "SsgiPrepare" };
    fx::ComputeShader m_traceShader{ "SsgiTrace" };
    fx::ComputeShader m_resolveShader{ "SsgiResolve" };
    fx::FullscreenShader m_compositeShader{ "SsgiComposite" };
    fx::Channel<SsgiResult> m_result{ "Result" };
};

} // namespace gte
