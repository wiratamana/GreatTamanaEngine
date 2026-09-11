#pragma once

// Atmosphere Scattering + Aerial Perspective campaign, Phase 7
// (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
// - the PERMANENT, always-running per-frame atmosphere pass sequence,
// relocated out of Application.cpp's own temporary
// `// TODO(ATMOSPHERE_PHASE7): relocate...` validation call site (Phases
// 3-6's own completion reports). Mirrors src/Application/RenderPasses.h's
// existing role/shape EXACTLY: thin, Application-layer free functions
// Application::Run() calls directly, in a fixed order, from inside its own
// offscreen-regime `build` lambda - deliberately NOT a single giant
// orchestration function, so each granular step's own ordering/dependency
// requirements stay visible at the call site (RenderPasses.h's own
// AddGameViewPass()/AddSceneViewPass()/AddPresentPass()/AddGpuSkinningPasses()
// are the direct precedent for this shape).
//
// Living under src/Application/ (not src/Renderer/Atmosphere/) for the
// exact same Clean Architecture reason RenderPasses.h already gives:
// Renderer/AtmosphereLutRenderer itself must never know which ECS Registry/
// active Camera/EditorCamera is "the Game View" vs. "the Scene View" - that
// engine-specific, Editor-aware knowledge belongs here, at the Application
// composition-root layer.

#include "../ECS/Registry.h"
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"
#include "../Renderer/Atmosphere/AtmosphereLutRenderer.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <functional>

namespace gte {

class Renderer;
class RenderTexture;

namespace rg {
class RenderGraphBuilder;
} // namespace rg

// Resolves the eye world-space position for the FIRST active ECS Camera
// entity (mirrors RenderSystem::ResolveActiveCameraViewProjection()'s own
// "first active Camera, in ComponentStorage<Camera> order" resolution
// exactly, but returns just the world position) - falls back to
// Vec3::Zero() when the Registry has no active Camera at all. Relocated
// verbatim out of Application.cpp's own Phase 5 temporary helper (see
// ATMOSPHERE_PHASE5_COMPLETION_REPORT.md's own "open question" about this
// function's eventual home) - this IS that eventual, permanent home.
Vec3 ResolveActiveCameraWorldPosition(Registry& registry) noexcept;

// Declares the SHARED (once per FRAME, never once per view) Transmittance
// LUT (Phase 3) + Multi-Scattering LUT (Phase 4) compute passes into
// `builder` - the Transmittance/Multi-Scattering LUTs are view-INDEPENDENT,
// unlike the Sky-View LUT/Aerial Perspective volume below (per this
// phase's own Step 3.1). The CALLER must add BOTH returned handles to this
// Execute() call's own outputs root set, or their writes are silently
// culled the next time RenderGraphCompiler::Compile() runs (same contract
// as AtmosphereLutRenderer's own AddTransmittanceLutPass()/
// AddMultiScatteringLutPass()).
struct AtmosphereSharedLutHandles {
    rg::TextureHandle transmittanceLutHandle;
    rg::TextureHandle multiScatteringLutHandle;
};
AtmosphereSharedLutHandles AddAtmosphereSharedLutPasses(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, const AtmosphereParametersGpu& atmosphereParameters);

// Resolves ONE view's own AtmosphereFrameUniforms (via
// AtmosphereLutRenderer.h's ResolveAtmosphereFrameUniforms(), still Phase
// 5's hardcoded sun-direction placeholder - `registry` is accepted purely
// so a future Phase 8 can look up a real DirectionalLight from it, per
// that function's own stable-signature contract) and declares that view's
// own Sky-View LUT (Phase 5) + Aerial Perspective Volume (Phase 6)
// passes - called ONCE PER VIEW (Game View and, now that this phase wires
// it up for real, Scene View too). `viewProjection` is this view's own
// CURRENT combined view * projection matrix - baked into the returned
// frameUniforms.invViewProjection for the Aerial Perspective volume's own
// froxel-ray reconstruction, and reused again later (unchanged) by
// MakeRecordSkyBackgroundCallback()/AddAtmosphereCompositePass() for this
// exact same view. `atmosphereSettings` (atmosphere-scattering-2 campaign
// Phase 1) supplies the 4 Aerial Perspective froxel-volume ray-march
// tunables (max distance/depth exponent/samples-per-slice/scattering
// exaggeration) set onto `result.frameUniforms` from INSIDE this function,
// right after ResolveAtmosphereFrameUniforms() returns and BEFORE
// AddSkyViewLutPass()/AddAerialPerspectiveVolumePass() are called (so both
// of those two calls' own GPU-side reads of frameUniforms already see the
// live Editor-tunable values) - the Sky-View LUT pass itself ignores these
// 4 fields entirely; only the Aerial Perspective Volume pass consumes them.
//
// The CALLER must add the returned Sky-View LUT TextureHandle to this
// call's own outputs root set, AND call
// `builder.KeepVolumeTextureOutput(result.aerialPerspectiveVolumeHandle)`
// explicitly (a VolumeTextureHandle can never be a `finalOutputs` root -
// see RenderGraphBuilder::KeepVolumeTextureOutput()'s own doc comment).
struct AtmosphereViewLutHandles {
    rg::TextureHandle skyViewLutHandle;
    rg::VolumeTextureHandle aerialPerspectiveVolumeHandle;
    AtmosphereFrameUniforms frameUniforms; // invViewProjection already set to viewProjection.Inverse().
};
AtmosphereViewLutHandles AddAtmosphereViewLutPasses(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, Registry& registry,
    const AtmosphereParametersGpu& atmosphereParameters, const AtmosphereSettings& atmosphereSettings,
    const AtmosphereSharedLutHandles& sharedLuts, Vec3 eyeWorldPosition, const Mat4& viewProjection,
    const char* skyViewLutName, const char* aerialPerspectiveVolumeName);

// Builds a ready-to-pass-into-AddGameViewPass()/AddSceneViewPass()'s own
// `recordSkyBackground` parameter (RenderPasses.h) - captures everything
// it needs by VALUE, so the returned std::function stays valid for the
// rest of this frame's recording regardless of what happens to the
// arguments afterward. `skyViewLutName` must be the SAME name just passed
// to AddAtmosphereViewLutPasses() above for this view, this same frame.
// `skyExposure` (Phase 8 -
// ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) is the Editor's
// "Atmosphere" panel-tunable value (AtmosphereSettings::skyExposure).
std::function<void(VkCommandBuffer)> MakeRecordSkyBackgroundCallback(AtmosphereLutRenderer& atmosphereLutRenderer,
    Renderer& renderer, const Mat4& viewProjection, const AtmosphereParametersGpu& atmosphereParameters,
    const AtmosphereFrameUniforms& frameUniforms, const char* skyViewLutName, float skyExposure);

// Declares the Aerial Perspective Composite pass (Phase 7, Step 3.3) for
// ONE view - MUST be called AFTER that view's own GameView/SceneView pass
// has already been declared in the SAME builder call (this pass needs to
// read that pass's own just-written color+depth this same frame).
// `viewRenderTexture` is the view's own persistent RenderTexture
// (m_gameView/m_sceneView, ImGuiEditorLayer.cpp) - MUST have been
// constructed with allowDepthSampledAccess=true (see
// DepthBuffer.h/RenderTexture.h) - its Sampler()/DepthSampler() are
// resolved here directly, never via PassContext::resolveTexture() (which
// has no depth-resolution path at all - see
// AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()'s own doc
// comment). `sourceColorHandle` is the SAME TextureHandle
// AddGameViewPass()/AddSceneViewPass() was given for this view, this same
// frame. `aerialPerspectiveStrength` (Phase 8) is the Editor's
// "Atmosphere" panel-tunable overall multiplier for the effect.
// `maxDistanceKm`/`depthExponent` (atmosphere-scattering-2 campaign Phase 1)
// are forwarded straight through, unchanged, to
// AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()'s own new
// parameters of the same name - see that function's own doc comment. The
// CALLER must add the returned TextureHandle to this call's own outputs
// root set.
rg::TextureHandle AddAtmosphereCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, RenderTexture& viewRenderTexture, rg::TextureHandle sourceColorHandle,
    rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
    const AtmosphereFrameUniforms& frameUniforms, Vec3 eyeWorldPosition, float aerialPerspectiveStrength,
    float maxDistanceKm, float depthExponent, VkExtent2D extent, const char* outputTextureName);

} // namespace gte
