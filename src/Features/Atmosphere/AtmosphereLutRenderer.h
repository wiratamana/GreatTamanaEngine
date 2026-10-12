#pragma once

// The single home for every atmosphere LUT compute pass (Transmittance/
// Multi-Scattering/Sky-View/Aerial-Perspective) plus the Sky Background
// draw and Aerial Perspective Composite - see AtmospherePassSequence.h for
// the per-frame call order a caller must follow.
//
// Public, Vulkan-free: every Vk*-bearing field/call lives behind a pimpl
// boundary in src/Renderer/Internal/AtmosphereLutRendererImpl.h/.cpp -
// mirrors Core/Plugins/RenderFeatureCompositor.h's own shape. Only
// src/Renderer may know this engine's graphics backend is Vulkan.

#include "AtmosphereTypes.h"
#include "../../Core/Extent.h"
#include "../../Core/GraphicsHandles.h"
#include "../../ECS/Registry.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"
#include "../../Renderer/Renderer.h" // Renderer::CapturedRawPixels value type

#include <cstdint>
#include <memory>

namespace gte {

namespace rg {
class CommandBuffer;
class RenderPassToggleRegistry;
} // namespace rg

class RenderTexture; // CompositedOutput() returns a pointer - never needs volk.h.
class Texture2D;     // TransmittanceLutOutput() returns a pointer - same reasoning.

class AtmosphereLutRenderer {
public:
    AtmosphereLutRenderer();
    ~AtmosphereLutRenderer();
    AtmosphereLutRenderer(const AtmosphereLutRenderer&) = delete;
    AtmosphereLutRenderer& operator=(const AtmosphereLutRenderer&) = delete;
    AtmosphereLutRenderer(AtmosphereLutRenderer&&) = delete;
    AtmosphereLutRenderer& operator=(AtmosphereLutRenderer&&) = delete;

    // Declares this frame's Transmittance LUT compute pass into `builder` -
    // writes a persistent 256x256 output Texture2D registered as
    // "AtmosphereTransmittanceLut". The caller must add the returned handle
    // to this call's own outputs root set, or the write is silently culled.
    rg::TextureHandle AddTransmittanceLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

    // Declares this frame's Multi-Scattering LUT pass - reads this call's
    // own AddTransmittanceLutPass() result, writes a persistent 64x64 HDR
    // output RenderTexture ("AtmosphereMultiScatteringLut"). Must be called
    // after AddTransmittanceLutPass() in the same frame.
    rg::TextureHandle AddMultiScatteringLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, rg::TextureHandle transmittanceLutHandle,
        rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

    // Declares this frame's Sky-View LUT pass for ONE view - `outputTextureName`
    // (a string-literal/static-storage-duration pointer) lets this method
    // serve multiple views with distinct registered texture names. Must be
    // called after AddTransmittanceLutPass()/AddMultiScatteringLutPass().
    rg::TextureHandle AddSkyViewLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
        rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
        const char* outputTextureName, rg::ViewScope viewScope,
        rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

    // Declares this frame's Aerial Perspective froxel-volume pass for ONE
    // view - writes a persistent 128x128x32 HDR VolumeTexture registered
    // under `outputVolumeName`. The caller must call
    // `builder.KeepVolumeTextureOutput(result)` explicitly (a
    // VolumeTextureHandle can never be a finalOutputs root).
    rg::VolumeTextureHandle AddAerialPerspectiveVolumePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
        rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
        const char* outputVolumeName, rg::ViewScope viewScope,
        rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

    // Draws this view's sky background directly against `cmd`, inside the
    // caller's own already-open render pass - looks up the already-computed
    // Sky-View LUT for `skyViewLutName` (this frame's own AddSkyViewLutPass()
    // call for that exact name must have already run).
    void DrawSkyBackground(Renderer& renderer, rg::CommandBuffer& cmd, const Mat4& viewProjection,
        const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
        const char* skyViewLutName, float skyExposure);

    // Declares this frame's Aerial Perspective Composite pass for ONE view.
    // `sourceColorHandle`/`sourceDepthHandle` are read as color/depth
    // inputs; `sourceColorSampler`/`sourceDepthView`/`sourceDepthSampler`
    // are the caller-resolved opaque handles actually bound to the shader.
    // Writes a persistent output RenderTexture under `outputTextureName`.
    rg::TextureHandle AddAerialPerspectiveCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        rg::TextureHandle sourceColorHandle, SamplerHandle sourceColorSampler,
        rg::TextureHandle sourceDepthHandle, ImageViewHandle sourceDepthView, SamplerHandle sourceDepthSampler,
        rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
        const Mat4& invViewProjection, Vec3 cameraWorldPosition, float aerialPerspectiveStrength,
        float maxDistanceKm, float depthExponent, Extent2D extent, const char* outputTextureName,
        rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

    // Returns `outputTextureName`'s own persistent composited output, or
    // nullptr if that name has never been passed to
    // AddAerialPerspectiveCompositePass() yet this session - used to hand
    // the Editor a stable RenderTexture* for "Game"/"Scene" display and as
    // GET /get_game_view's capture source.
    RenderTexture* CompositedOutput(const char* outputTextureName) noexcept;

    // Transitions `outputTextureName`'s composited output to a real
    // ShaderRead state, ready for Dear ImGui/GET /get_game_view/GET
    // /get_texture to sample it directly. Must be called against the SAME
    // command buffer the offscreen RenderGraph::Execute() call just
    // recorded into, before that command buffer is ended/submitted.
    void FinalizeAerialPerspectiveCompositeForSampling(rg::CommandBuffer& cmd, const char* outputTextureName);

    // Exposes the persistent Texture2D behind AddTransmittanceLutPass()'s
    // own output, so a validation tool can read back its real, currently-
    // computed pixels. Returns nullptr if that pass has never run yet.
    Texture2D* TransmittanceLutOutput() noexcept;

    // The real Z-slice count for `aerialPerspectiveVolumeName` - returns 0
    // if that volume has never been generated this session.
    int AerialPerspectiveVolumeDepth(const char* aerialPerspectiveVolumeName) const noexcept;

    // Declares a tiny compute pass copying ONE Z slice of
    // `aerialPerspectiveVolumeHandle` into a real, registered 2D
    // RenderTexture under `outputTextureName` - GET /get_texture/GET
    // /list_textures-capturable with zero further networking changes.
    // Must be called after AddAerialPerspectiveVolumePass() for
    // `aerialPerspectiveVolumeName` in the same frame.
    rg::TextureHandle AddAerialPerspectiveVolumeDebugSlicePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
        std::uint32_t debugSliceIndex, const char* outputTextureName, rg::ViewScope viewScope,
        rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

    // Feeds GET /get_debug_texture / the Editor's numeric LUT inspector -
    // the raw captured bytes stay Vulkan-free (Renderer::CapturedRawPixels
    // is a plain bytes+width+height value type). The ad-hoc, IMMEDIATE-
    // dispatch counterpart of AddAerialPerspectiveVolumeDebugSlicePass()
    // above, for an Editor button click outside any per-frame RenderGraph
    // pass. Returns an empty CapturedRawPixels if `aerialPerspectiveVolumeName`
    // has never been generated this session.
    Renderer::CapturedRawPixels CaptureAerialPerspectiveVolumeSliceImmediate(Renderer& renderer,
        const char* aerialPerspectiveVolumeName, std::uint32_t sliceIndex, const char* outputTextureName);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Resolves this frame's AtmosphereFrameUniforms for ONE view, given that
// view's own eye world-space position. `registry` resolves the real,
// first-active ECS DirectionalLight entity, falling back to a hardcoded
// placeholder sun whenever the Registry has none.
AtmosphereFrameUniforms ResolveAtmosphereFrameUniforms(Registry& registry, Vec3 eyeWorldPosition);

} // namespace gte
