#pragma once

// The PERMANENT, always-running per-frame atmosphere pass sequence - thin,
// Application-layer free functions called directly, in a fixed order, from
// the offscreen-regime build lambda. Kept separate from AtmosphereLutRenderer
// itself: AtmosphereLutRenderer must never know which ECS Registry/active
// Camera is "the Game View" vs. "the Scene View" - that Editor-aware
// knowledge lives here instead.
//
// Pure pass-through plumbing, Vulkan-free - every opaque type here is
// forwarded straight into AtmosphereLutRenderer's own matching method.

#include "../../Core/Extent.h"
#include "../../Core/GraphicsHandles.h"
#include "../../ECS/Registry.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"
#include "AtmosphereLutRenderer.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

#include <functional>

namespace gte {

class Renderer;

namespace rg {
class CommandBuffer;
class RenderGraphBuilder;
class RenderPassToggleRegistry;
} // namespace rg

// Declares the SHARED (once per FRAME, never once per view) Transmittance +
// Multi-Scattering LUT compute passes into `builder` - view-INDEPENDENT,
// unlike the Sky-View LUT/Aerial Perspective volume below. The caller must
// add BOTH returned handles to this Execute() call's own outputs root set,
// or their writes are silently culled.
struct AtmosphereSharedLutHandles {
    rg::TextureHandle transmittanceLutHandle;
    rg::TextureHandle multiScatteringLutHandle;
};
AtmosphereSharedLutHandles AddAtmosphereSharedLutPasses(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, const AtmosphereParametersGpu& atmosphereParameters,
    rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

// Resolves ONE view's own AtmosphereFrameUniforms and declares that view's
// own Sky-View LUT + Aerial Perspective Volume passes - called ONCE PER
// VIEW (Game View and Scene View). `viewProjection` is this view's own
// current combined view * projection matrix - baked into the returned
// frameUniforms.invViewProjection, and reused again later (unchanged) by
// MakeRecordSkyBackgroundCallback()/AddAtmosphereCompositePass() for this
// exact same view. `atmosphereSettings` supplies the Aerial Perspective
// froxel-volume ray-march tunables, set onto `result.frameUniforms` before
// AddSkyViewLutPass()/AddAerialPerspectiveVolumePass() are called.
//
// The caller must add the returned Sky-View LUT TextureHandle to this
// call's own outputs root set, AND call
// `builder.KeepVolumeTextureOutput(result.aerialPerspectiveVolumeHandle)`
// explicitly (a VolumeTextureHandle can never be a finalOutputs root).
struct AtmosphereViewLutHandles {
    rg::TextureHandle skyViewLutHandle;
    rg::VolumeTextureHandle aerialPerspectiveVolumeHandle;
    AtmosphereFrameUniforms frameUniforms; // invViewProjection already set to viewProjection.Inverse().
};
AtmosphereViewLutHandles AddAtmosphereViewLutPasses(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, Registry& registry,
    const AtmosphereParametersGpu& atmosphereParameters, const AtmosphereSettings& atmosphereSettings,
    const AtmosphereSharedLutHandles& sharedLuts, Vec3 eyeWorldPosition, const Mat4& viewProjection,
    const char* skyViewLutName, const char* aerialPerspectiveVolumeName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

// Builds a ready-to-pass-into-AddGameViewPass()/AddSceneViewPass()'s own
// `recordSkyBackground` parameter - captures everything it needs by VALUE,
// so the returned std::function stays valid for the rest of this frame's
// recording regardless of what happens to the arguments afterward.
// `skyViewLutName` must be the SAME name just passed to
// AddAtmosphereViewLutPasses() above for this view, this same frame.
std::function<void(rg::CommandBuffer&)> MakeRecordSkyBackgroundCallback(AtmosphereLutRenderer& atmosphereLutRenderer,
    Renderer& renderer, const Mat4& viewProjection, const AtmosphereParametersGpu& atmosphereParameters,
    const AtmosphereFrameUniforms& frameUniforms, const char* skyViewLutName, float skyExposure);

// Declares the Aerial Perspective Composite pass for ONE view - must be
// called AFTER that view's own GameView/SceneView pass has already been
// declared in the SAME builder call. `sourceColorHandle` is read for color;
// `sourceDepthHandle` is read for depth only - a separate handle, since the
// two may come from physically different imported resources.
// `sourceColorSampler`/`sourceDepthView`/`sourceDepthSampler` are the
// caller-resolved opaque handles actually bound to the shader. The caller
// must add the returned TextureHandle to this call's own outputs root set.
rg::TextureHandle AddAtmosphereCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, rg::TextureHandle sourceColorHandle,
    SamplerHandle sourceColorSampler, rg::TextureHandle sourceDepthHandle, ImageViewHandle sourceDepthView,
    SamplerHandle sourceDepthSampler, rg::VolumeTextureHandle aerialPerspectiveVolumeHandle,
    const char* aerialPerspectiveVolumeName, const AtmosphereFrameUniforms& frameUniforms,
    Vec3 eyeWorldPosition, float aerialPerspectiveStrength, float maxDistanceKm, float depthExponent,
    Extent2D extent, const char* outputTextureName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

} // namespace gte
