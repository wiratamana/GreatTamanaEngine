#pragma once

#include <cstdint>
#include <cstddef>

// editor-core-separation-6 campaign, PHASE1
// (PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md) - the fixed-size POD
// descriptor a plugin implementing IRenderFeatureModule_v2 returns once,
// at load time, so the HOST (never the plugin itself) decides real,
// deterministic ordering and blending - see PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #1/#2/#9 and RENDER_FEATURE_COMPOSITING_FINDINGS_
// AND_PROPOSAL_2026-09-25.md Section 3.2. Mirrors GtePluginModuleInfo's own
// "fixed-size, trivially-copyable POD, safe to read via GetProcAddress()+
// call" discipline - never std::string/std::vector crossing this boundary
// (plugins/gte_plugin_abi/PublicSurface.md's own rule).

namespace gte {

// PHASE0_MASTER_STRATEGY.md Locked Design Decision #1: as of this campaign,
// ONLY RenderFeatureStage::PostComposite and RenderFeatureStage::PreUI are
// actually wired into the live render graph (RenderFeatureCompositor,
// PHASE4/PHASE5). PreOpaque/PostOpaque/PostTransparent are declared here for
// ABI future-proofing ONLY - a plugin that declares one of them today is
// refused, loudly (GTE_LOG_WARNING naming the plugin and the unwired
// stage), at RenderFeatureCompositor::OnPluginsLoaded() time, and is simply
// never invoked. Numeric values are stable and must never be renumbered
// once shipped (a plugin .dll built against an older layout of this enum
// would otherwise silently misinterpret its own declared stage).
enum class RenderFeatureStage : std::uint32_t {
    PreOpaque       = 0,  // NOT WIRED this campaign - declared, refused if used.
    PostOpaque      = 1,  // NOT WIRED this campaign - declared, refused if used.
    PostTransparent = 2,  // NOT WIRED this campaign - declared, refused if used.
    PostComposite   = 3,  // WIRED - today's existing single hook point.
    PreUI           = 4,  // WIRED - runs immediately AFTER every PostComposite
                          // entry, same hook point, same frame (see
                          // PHASE0_MASTER_STRATEGY.md Locked Design Decision #2
                          // for exactly why this is NOT a separate
                          // RenderPassEvent tier in this engine today).
};

// PHASE0_MASTER_STRATEGY.md Locked Design Decision #10 - Replace is the
// legacy _v1-equivalent hard overwrite (still legal, still available to a
// _v2 plugin that wants it); the other four are real, host-owned GPU blends
// (RenderFeatureBlend.comp, PHASE5).
enum class RenderFeatureBlendMode : std::uint32_t {
    Replace         = 0,
    AlphaOver       = 1,
    Additive        = 2,
    Multiply        = 3,
    ScreenSpaceMask = 4,
};

// A plugin author sets these once, typically returned from a single
// GetRenderFeatureDescriptor() override (IRenderFeatureModule_v2, below).
// The HOST NEVER trusts a plugin to self-order at runtime - RenderFeatureCompositor
// collects every loaded plugin's descriptor exactly ONCE, at
// OnPluginsLoaded() time (right after PluginHost::LoadPlugins() returns),
// groups by stage, sorts by priority ascending WITHIN each stage, and
// reuses that one resolved ordering every subsequent frame (never re-sorted
// per-frame - descriptors do not change while a plugin stays loaded).
struct GtePluginRenderFeatureDescriptor {
    // Display name - shown in the Editor's "Render Graph" panel (PHASE7)
    // and every diagnostic log line (collision warnings, unwired-stage
    // warnings). Must be a short, human-readable, null-terminated string;
    // truncated safely if longer than 63 characters (see
    // MakeRenderFeatureDescriptor() helper, Step 3.2, for the bounded-copy
    // helper every _v2 plugin author should use to fill this field).
    char name[64];

    RenderFeatureStage stage;

    // Lower runs first WITHIN the same stage. Author-declared, NEVER
    // auto-assigned by the host. Two plugins in the SAME stage with the
    // SAME priority is a declared CONFLICT - RenderFeatureCompositor logs
    // a loud GTE_LOG_WARNING naming both plugins by their `name` field,
    // then falls back to a documented, STABLE tie-break (lexical
    // comparison of `name`) purely so the engine never crashes - the
    // ambiguity is a visible, actionable fact at load time, never a pixel
    // mystery (PHASE0_MASTER_STRATEGY.md Locked Design Decision, mirrors
    // the Proposal's own Section 3.4 step 3).
    std::int32_t priority;

    RenderFeatureBlendMode blendMode;
};

// Bounded, ALWAYS-null-terminated copy into GtePluginRenderFeatureDescriptor::name
// - mirrors SingleCapabilityPluginModule.h's own MakeModuleInfo() helper and
// reasoning exactly (plugins/gte_plugin_abi/SingleCapabilityPluginModule.h).
inline GtePluginRenderFeatureDescriptor MakeRenderFeatureDescriptor(const char* name,
    RenderFeatureStage stage, std::int32_t priority, RenderFeatureBlendMode blendMode) noexcept
{
    GtePluginRenderFeatureDescriptor descriptor{};
    std::size_t i = 0;
    for (; name[i] != '\0' && i + 1 < sizeof(descriptor.name); ++i) {
        descriptor.name[i] = name[i];
    }
    descriptor.name[i] = '\0';
    descriptor.stage = stage;
    descriptor.priority = priority;
    descriptor.blendMode = blendMode;
    return descriptor;
}

} // namespace gte
