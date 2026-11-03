#pragma once

#include "BloomDebugEndpoint.h"
#include "BloomTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"

#include <array>
#include <cstdint>

namespace gte {

// Per-view last frame report.
struct BloomViewState {
    BloomViewSnapshot snapshot;
};

// Glow: bright pixels are blurred through a mip chain and added to the picture before the display transform.
class BloomFeature final : public fx::Feature {
public:
    explicit BloomFeature(fx::FeatureHost& host);

    const char* Name() const override { return kBloomFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;

    const BloomSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const BloomSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    BloomViewSnapshot Snapshot(rg::ViewScope scope) const;

    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

private:
    void CompositePass(fx::CompositeFrame& f);
    bool ChainPassesEnabled(const fx::CompositeFrame& f, std::uint32_t levels) const;
    bool ChainShadersReady(fx::CompositeFrame& f);

    BloomSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    BloomDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<BloomViewState> m_views;
    fx::ComputeShader m_prefilterShader{ "BloomPrefilter" };
    fx::ComputeShader m_downShader{ "BloomDown" };
    fx::ComputeShader m_upShader{ "BloomUp" };

    // Down chain: mip 0 is the bright pass at half size. Up chain: level i holds mip i plus everything below it.
    std::array<fx::TransientTarget, kBloomMaxLevels> m_mips{
        fx::TransientTarget{ "Bloom.Mip0" }, fx::TransientTarget{ "Bloom.Mip1" }, fx::TransientTarget{ "Bloom.Mip2" },
        fx::TransientTarget{ "Bloom.Mip3" }, fx::TransientTarget{ "Bloom.Mip4" }, fx::TransientTarget{ "Bloom.Mip5" },
    };
    std::array<fx::TransientTarget, kBloomMaxLevels - 1> m_ups{
        fx::TransientTarget{ "Bloom.Up0" }, fx::TransientTarget{ "Bloom.Up1" }, fx::TransientTarget{ "Bloom.Up2" },
        fx::TransientTarget{ "Bloom.Up3" }, fx::TransientTarget{ "Bloom.Up4" },
    };
};

} // namespace gte
