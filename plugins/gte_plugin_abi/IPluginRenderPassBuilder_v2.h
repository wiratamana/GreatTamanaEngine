#pragma once

// editor-core-separation-6 campaign, PHASE1
// (PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md) - the ADDITIVE, v2 sibling of
// IPluginRenderPassBuilder.h's existing IPluginRenderPassBuilder (v1,
// UNTOUCHED by this campaign - see plugins/demo_render_feature/
// RenderFeaturePlugin.cpp, still the exact same file, still compiling,
// still working). A small, fixed, still-curated, still-growable palette of
// real drawing operations (RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_
// 2026-09-25.md Section 3.3) - deliberately just these 3 for this campaign
// (PHASE0_MASTER_STRATEGY.md Locked Design Decision #3 drops the proposal's
// own scene-color/depth-read methods entirely - no fixed operation below
// needs to read the scene first, they only ever draw ON TOP of whatever a
// prior plugin/stage already produced). Every method uses ONLY plain
// built-in types - no std::string/std::vector/gte_core type crosses this
// boundary, mirroring IPluginRenderPassBuilder (v1)'s own exact discipline.

namespace gte {

class IPluginRenderPassBuilder_v2 {
public:
    virtual ~IPluginRenderPassBuilder_v2() = default;

    // Solid-fill, exactly like v1's AddFullscreenClearPass, but composited
    // via THIS feature's own declared blendMode (GtePluginRenderFeatureDescriptor)
    // instead of always being a hard clear - see RenderFeatureOps.comp
    // (PHASE4), opCode 0.
    virtual void AddSolidFillPass(const char* debugName, float r, float g, float b, float a) = 0;

    // A parameterized radial vignette (screen-space, normalized center/
    // radius/softness/color) - covers "damage vignette"/"low-health pulse"/
    // "night-vision edge falloff" without any shader upload. centerX/centerY
    // and innerRadius/outerRadius are normalized 0.0-1.0 fractions of the
    // view's own width/height (innerRadius/outerRadius as a fraction of the
    // view's diagonal - see RenderFeatureOps.comp's own doc comment for the
    // EXACT formula, PHASE4). Alpha naturally falls to 0 outside
    // outerRadius, so this pass's own private target needs no separate
    // "clear to transparent" step first - see RenderFeatureOps.comp,
    // opCode 1.
    virtual void AddRadialVignettePass(const char* debugName, float centerX, float centerY,
        float innerRadius, float outerRadius, float r, float g, float b, float a) = 0;

    // A parameterized full-screen color-grade (brightness/contrast/
    // saturation/tint) - covers "underwater"/"night vision"/"photo mode
    // grade" without any shader upload. brightness/contrast/saturation are
    // multipliers around their own neutral value (1.0 = unchanged);
    // tintR/tintG/tintB is the tint color; tintStrength (0.0-1.0) is how
    // strongly the tint is mixed in - see RenderFeatureOps.comp, opCode 2.
    virtual void AddColorGradePass(const char* debugName, float brightness, float contrast,
        float saturation, float tintR, float tintG, float tintB, float tintStrength) = 0;
};

} // namespace gte
