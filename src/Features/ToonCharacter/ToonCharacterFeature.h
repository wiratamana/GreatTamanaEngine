#pragma once

#include "ToonCharacterDebugEndpoint.h"
#include "ToonCharacterTypes.h"
#include "../../Core/FeatureKit/FeatureKit.h"
#include "../../ECS/Entity.h"
#include "../../Game/Lighting/ToonLook.h"

#include <cstdint>
#include <vector>

namespace gte {

// Per-view working set. `parts` keeps its capacity between frames.
struct ToonCharacterViewState {
    std::vector<Entity> parts;      // Mesh parts under a toon root, collected this frame.
    fx::EntitySelection selection;  // Reserved by the first draw of the frame; valid for `selectionSerial` only.
    std::uint64_t selectionSerial = 0;
    ToonCharacterViewSnapshot snapshot;
};

// Cel-shaded redraw of marked characters: surface (takes the parts over from the main draw), outline hull and
// blended hair ring. Shadow cascades still draw the parts, so toon characters cast shadows; the surface shader receives them.
class ToonCharacterFeature final : public fx::Feature {
public:
    explicit ToonCharacterFeature(fx::FeatureHost& host);

    const char* Name() const override { return kToonCharacterFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;
    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }

    const ToonCharacterSettings& Settings() const noexcept { return m_settings; }

    // Sanitizes `candidate` and stores it. Main thread only.
    void ApplySettings(const ToonCharacterSettings& candidate);

    // The last state of `scope`'s view; NotRendered when that view has not rendered for a few frames.
    ToonCharacterViewSnapshot Snapshot(rg::ViewScope scope) const;

private:
    enum class DrawOutcome : std::uint8_t { PassDisabled, NoParts, CaptureArmed, Refused, Drawn };

    void LookPass(fx::PreOpaqueFrame& f);
    void SurfacePass(fx::OpaqueFrame& f);
    void OutlinePass(fx::OpaqueFrame& f);
    void OverlayPass(fx::OpaqueFrame& f);
    DrawOutcome RecordDraw(fx::OpaqueFrame& f, rg::StaticName passName, fx::SceneShaderSet& shaders, bool claim);

    ToonCharacterSettings m_settings;
    std::uint64_t m_newestFrame = 0;
    ToonCharacterDebugEndpoint m_debugEndpoint{ *this };

    fx::PerView<ToonCharacterViewState> m_views;
    fx::Channel<ToonLook> m_look{ kToonLookChannel };

    // Shaders/<name>.vert and .frag. The hull is front-culled; the overlay blends over the hair and writes no depth.
    fx::SceneShaderSet m_surfaceShaders{ fx::SceneShaderSet::Shaded("ToonSurface",
        { .cull = CullMode::None, .depthCompare = CompareOp::Less, .depthWrite = true, .blend = BlendMode::Opaque }) };
    fx::SceneShaderSet m_outlineShaders{ fx::SceneShaderSet::Shaded("ToonOutline",
        { .cull = CullMode::Front, .depthCompare = CompareOp::LessEqual, .depthWrite = true, .blend = BlendMode::Opaque }) };
    fx::SceneShaderSet m_overlayShaders{ fx::SceneShaderSet::Shaded("ToonOverlay",
        { .cull = CullMode::Back, .depthCompare = CompareOp::LessEqual, .depthWrite = false, .blend = BlendMode::AlphaBlend }) };
};

} // namespace gte
