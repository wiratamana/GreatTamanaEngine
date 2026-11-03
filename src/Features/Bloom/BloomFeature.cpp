#include "BloomFeature.h"

#include "BloomMath.h"
#include "../../Core/PixelFormat.h"

#include <array>

namespace gte {

namespace {

constexpr std::int32_t kFrameStampPriority = 46;
constexpr std::int32_t kCompositePriority = -100; // After VolumetricLight, before ToneMapping.

// Pass names are literals. Down[k] writes mip k + 1; Up[k] writes up level k.
constexpr std::array<rg::StaticName, kBloomMaxLevels - 1> kDownNames{
    "Bloom.Down0", "Bloom.Down1", "Bloom.Down2", "Bloom.Down3", "Bloom.Down4",
};
constexpr std::array<rg::StaticName, kBloomMaxLevels - 1> kUpNames{
    "Bloom.Up0", "Bloom.Up1", "Bloom.Up2", "Bloom.Up3", "Bloom.Up4",
};

} // namespace

BloomFeature::BloomFeature(fx::FeatureHost&) { }

void BloomFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a toggle from freezing the frame clock the snapshots age against.
    s.PreOpaque("Bloom.FrameStamp", kFrameStampPriority,
        [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); }, fx::Gate::Always());

    s.PostComposite("Bloom", kCompositePriority, fx::Blend::Additive,
        [this](fx::CompositeFrame& f) { CompositePass(f); },
        fx::Gate{ "Bloom.Prefilter", "Bloom.Down0", "Bloom.Down1", "Bloom.Down2", "Bloom.Down3", "Bloom.Down4",
            "Bloom.Up0", "Bloom.Up1", "Bloom.Up2", "Bloom.Up3", "Bloom.Up4", "Bloom.CopyToPrivateTarget" });
}

void BloomFeature::ApplySettings(const BloomSettings& candidate)
{
    m_settings = SanitizeBloomSettings(candidate);
}

BloomViewSnapshot BloomFeature::Snapshot(rg::ViewScope scope) const
{
    BloomViewSnapshot snapshot = m_views.For(scope).snapshot;
    if (fx::SnapshotIsStale(snapshot.frameIndex, m_newestFrame)) {
        snapshot.status = BloomFrameStatus::NotRendered;
    }
    return snapshot;
}

bool BloomFeature::ChainPassesEnabled(const fx::CompositeFrame& f, std::uint32_t levels) const
{
    if (!f.PassEnabled("Bloom.Prefilter") || !f.PassEnabled("Bloom.CopyToPrivateTarget")) {
        return false;
    }
    for (std::uint32_t i = 0; i + 1 < levels; ++i) {
        if (!f.PassEnabled(kDownNames[i]) || !f.PassEnabled(kUpNames[i])) {
            return false;
        }
    }
    return true;
}

bool BloomFeature::ChainShadersReady(fx::CompositeFrame& f)
{
    const bool prefilter = f.ShaderReady(m_prefilterShader);
    const bool down = f.ShaderReady(m_downShader);
    const bool up = f.ShaderReady(m_upShader);
    return prefilter && down && up;
}

void BloomFeature::CompositePass(fx::CompositeFrame& f)
{
    BloomViewState& view = m_views.For(f.Scope());
    const auto finish = [&](BloomFrameStatus status, std::uint32_t levels = 0) {
        view.snapshot = BloomViewSnapshot{ status, f.FrameIndex(), levels };
    };

    // Every early return declares nothing, so the compositor skips this entry.
    const BloomSettings& cfg = m_settings;
    if (!cfg.enabled || !(cfg.intensity > 0.0f)) {
        finish(BloomFrameStatus::Disabled);
        return;
    }
    const Extent2D full = f.Extent();
    const std::uint32_t levels = BloomActiveLevels(full, cfg.levels);
    if (levels == 0) {
        finish(BloomFrameStatus::ExtentTooSmall);
        return;
    }
    if (!ChainPassesEnabled(f, levels)) {
        finish(BloomFrameStatus::PassDisabled);
        return;
    }
    if (!ChainShadersReady(f)) {
        finish(BloomFrameStatus::ShadersNotReady);
        return;
    }

    std::array<fx::Texture, kBloomMaxLevels> mips{};
    std::array<fx::Texture, kBloomMaxLevels - 1> ups{};
    for (std::uint32_t i = 0; i < levels; ++i) {
        mips[i] = m_mips[i].Acquire(f, BloomMipExtent(full, i), PixelFormat::RGBA16_Float);
        if (!mips[i].IsValid()) {
            finish(BloomFrameStatus::TargetFailed);
            return;
        }
    }
    for (std::uint32_t i = 0; i + 1 < levels; ++i) {
        ups[i] = m_ups[i].Acquire(f, BloomMipExtent(full, i), PixelFormat::RGBA16_Float);
        if (!ups[i].IsValid()) {
            finish(BloomFrameStatus::TargetFailed);
            return;
        }
    }

    const auto refused = [&](const char* what) {
        f.ReportOnce(what);
        finish(BloomFrameStatus::ShadersNotReady);
    };

    if (!f.Compute(m_prefilterShader, "Bloom.Prefilter")
            .Push(MakeBloomPrefilterPush(cfg))
            .Bind("sourceColor", f.SceneColor())
            .Bind("destinationImage", mips[0])
            .Dispatch(BloomMipExtent(full, 0))) {
        refused("Bloom.Prefilter was refused");
        return;
    }
    for (std::uint32_t i = 1; i < levels; ++i) {
        if (!f.Compute(m_downShader, kDownNames[i - 1])
                .Bind("sourceColor", mips[i - 1])
                .Bind("destinationImage", mips[i])
                .Dispatch(BloomMipExtent(full, i))) {
            refused("A Bloom down pass was refused");
            return;
        }
    }
    for (std::uint32_t i = levels - 1; i-- > 0;) {
        const fx::Texture lower = (i + 2 == levels) ? mips[levels - 1] : ups[i + 1];
        if (!f.Compute(m_upShader, kUpNames[i])
                .Push(MakeBloomUpPush(cfg, i == 0))
                .Bind("sourceCurrent", mips[i])
                .Bind("sourceLower", lower)
                .Bind("destinationImage", ups[i])
                .Dispatch(BloomMipExtent(full, i))) {
            refused("A Bloom up pass was refused");
            return;
        }
    }
    if (!f.Blit("Bloom.CopyToPrivateTarget", ups[0], f.PrivateTarget(), fx::BlitFilter::Linear)) {
        refused("Bloom.CopyToPrivateTarget was refused");
        return;
    }
    finish(BloomFrameStatus::Ok, levels);
}

GTE_REGISTER_RENDER_FEATURE("Bloom", BloomFeature);

} // namespace gte
