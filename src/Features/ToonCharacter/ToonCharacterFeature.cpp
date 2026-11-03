#include "ToonCharacterFeature.h"

#include "ToonCharacterMath.h"
#include "ToonPartCollector.h"
#include "../../ECS/Registry.h"

namespace gte {

namespace {

constexpr std::int32_t kSurfacePriority = 100;
constexpr std::int32_t kOutlinePriority = 110;
constexpr std::int32_t kOverlayPriority = 120;

constexpr rg::StaticName kSurfaceDraw{ "ToonCharacter.Surface.Draw" };
constexpr rg::StaticName kOutlineDraw{ "ToonCharacter.Outline.Draw" };
constexpr rg::StaticName kOverlayDraw{ "ToonCharacter.Overlay.Draw" };

} // namespace

ToonCharacterFeature::ToonCharacterFeature(fx::FeatureHost&) { }

void ToonCharacterFeature::ApplySettings(const ToonCharacterSettings& candidate)
{
    m_settings = SanitizeToonCharacterSettings(candidate);
}

ToonCharacterViewSnapshot ToonCharacterFeature::Snapshot(rg::ViewScope scope) const
{
    ToonCharacterViewSnapshot snapshot = m_views.For(scope).snapshot;
    if (fx::SnapshotIsStale(snapshot.frameIndex, m_newestFrame)) {
        snapshot = ToonCharacterViewSnapshot{};
        snapshot.frameIndex = m_views.For(scope).snapshot.frameIndex;
    }
    return snapshot;
}

void ToonCharacterFeature::Setup(fx::FeatureSetup& s)
{
    // Not a render pass: Always() keeps a pass toggle from silencing the look record and the frame clock.
    // Runs before SceneLighting.Uniforms (20), which copies the record into the scene uniforms.
    s.PreOpaque("ToonCharacter.Look", kToonLookPriority, [this](fx::PreOpaqueFrame& f) { LookPass(f); },
        fx::Gate::Always());

    s.Opaque("ToonCharacter.Surface", kSurfacePriority, [this](fx::OpaqueFrame& f) { SurfacePass(f); },
        fx::Gate{ "ToonCharacter.Surface.Draw" });
    s.Opaque("ToonCharacter.Outline", kOutlinePriority, [this](fx::OpaqueFrame& f) { OutlinePass(f); },
        fx::Gate{ "ToonCharacter.Outline.Draw" });
    s.Opaque("ToonCharacter.Overlay", kOverlayPriority, [this](fx::OpaqueFrame& f) { OverlayPass(f); },
        fx::Gate{ "ToonCharacter.Overlay.Draw" });
}

void ToonCharacterFeature::LookPass(fx::PreOpaqueFrame& f)
{
    m_newestFrame = f.FrameIndex();
    ToonCharacterViewState& view = m_views.For(f.Scope());
    view.parts.clear();
    view.selection = fx::EntitySelection{};
    view.snapshot = ToonCharacterViewSnapshot{};
    view.snapshot.frameIndex = f.FrameIndex();

    if (!m_settings.enabled) {
        view.snapshot.status = ToonCharacterFrameStatus::FeatureDisabled;
        return;
    }
    CollectToonParts(f.Scene(), view.parts);
    view.snapshot.candidatePartCount = static_cast<std::uint32_t>(view.parts.size());
    if (view.parts.empty()) {
        view.snapshot.status = ToonCharacterFrameStatus::NoToonParts;
        return;
    }
    m_look.Publish(f, PackToonLook(m_settings, f.View().extent));
}

ToonCharacterFeature::DrawOutcome ToonCharacterFeature::RecordDraw(
    fx::OpaqueFrame& f, rg::StaticName passName, fx::SceneShaderSet& shaders, bool claim)
{
    if (!f.PassEnabled(passName)) {
        return DrawOutcome::PassDisabled;
    }
    ToonCharacterViewState& view = m_views.For(f.Scope());
    if (view.parts.empty()) {
        return DrawOutcome::NoParts;
    }
    // A frame capture keeps the main draw complete: the surface cannot claim, so no toon pass draws.
    if (f.ViewCaptureArmed()) {
        return DrawOutcome::CaptureArmed;
    }
    // One selection per view and frame feeds every toon draw.
    if (view.selectionSerial != f.Serial()) {
        view.selection = f.SelectEntities(view.parts);
        view.selectionSerial = f.Serial();
    }
    if (!view.selection.IsValid()) {
        return DrawOutcome::Refused;
    }

    fx::SceneDrawChain chain = f.SceneDraw(passName);
    chain.Shaders(shaders).Camera(f.View().DrawViewProjection()).Target(f.SceneTarget()).SceneServices().Only(view.selection);
    if (claim) {
        chain.Claim();
    }
    if (!chain.Record()) {
        return DrawOutcome::Refused;
    }
    if (claim) {
        view.snapshot.claimedEntityCount = chain.ClaimedCount();
    }
    return DrawOutcome::Drawn;
}

void ToonCharacterFeature::SurfacePass(fx::OpaqueFrame& f)
{
    ToonCharacterViewSnapshot& snapshot = m_views.For(f.Scope()).snapshot;
    if (!m_settings.enabled) {
        snapshot.status = ToonCharacterFrameStatus::FeatureDisabled;
        return;
    }
    ToonCharacterFrameStatus drawStatus = ToonCharacterFrameStatus::DrawRefused;
    switch (RecordDraw(f, kSurfaceDraw, m_surfaceShaders, true)) {
    case DrawOutcome::Drawn: drawStatus = ToonCharacterFrameStatus::Ok; break;
    case DrawOutcome::PassDisabled: drawStatus = ToonCharacterFrameStatus::PassDisabled; break;
    case DrawOutcome::NoParts: drawStatus = ToonCharacterFrameStatus::NoToonParts; break;
    case DrawOutcome::CaptureArmed: drawStatus = ToonCharacterFrameStatus::CaptureArmed; break;
    case DrawOutcome::Refused: break;
    }
    snapshot.status = ResolveToonSurfaceStatus(snapshot.status, drawStatus, snapshot.claimedEntityCount);
}

void ToonCharacterFeature::OutlinePass(fx::OpaqueFrame& f)
{
    // The hull shares the surface's entities and depth; it needs no claim.
    RecordDraw(f, kOutlineDraw, m_outlineShaders, false);
}

void ToonCharacterFeature::OverlayPass(fx::OpaqueFrame& f)
{
    // The overlay shader keeps only hair-ring parts and discards every other class.
    RecordDraw(f, kOverlayDraw, m_overlayShaders, false);
}

GTE_REGISTER_RENDER_FEATURE("ToonCharacter", ToonCharacterFeature);

} // namespace gte
