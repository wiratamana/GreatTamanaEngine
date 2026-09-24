#pragma once

// editor-core-separation-3 campaign, PHASE3
// (PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md).

namespace gte {

// The ONLY way a plugin ever declares a render-graph pass - source design
// doc Section 3.3/5's own "small, curated, ABI-contract wrapper... never
// the raw internal type verbatim", made concrete and DELIBERATELY MINIMAL
// for this campaign's own Milestone 1 scope (PHASE0_MASTER_STRATEGY.md,
// Locked Design Decision #2/#3): a plugin NEVER receives a real
// rg::RenderGraphBuilder&, ever. Every method here uses ONLY plain built-in
// types - no std::string/std::vector/gte_core type crosses this boundary.
//
// Deliberately narrow for v1 - exactly enough to satisfy this campaign's
// own throwaway demo ("clears the screen to a solid debug color"). A future
// _v2 (a NEW interface, additive, never redefining this one's meaning -
// source design doc Section 4.3/8) is where a genuinely richer pass-
// building surface (textured draws, compute dispatches, reading another
// pass's output) would be designed, once a REAL future capability actually
// needs it - not invented speculatively here.
class IPluginRenderPassBuilder {
public:
    virtual ~IPluginRenderPassBuilder() = default;

    // Declares one graphics pass that clears the CURRENT view's Game/Scene
    // render target to the given solid RGBA color (each component 0.0-1.0)
    // - runs AFTER every real production pass this frame (see
    // PHASE0/PHASE3's own "PluginRenderFeatures" provider ordering). `debugName`
    // must be a stable, static-duration string literal owned by the CALLER
    // (the plugin) - the adapter implementing this interface never takes
    // ownership of it, never frees it, and does not copy it beyond this
    // one call's own duration (Locked Design Decision #3/#4,
    // PHASE0_MASTER_STRATEGY.md).
    virtual void AddFullscreenClearPass(const char* debugName, float r, float g, float b, float a) = 0;
};

} // namespace gte
