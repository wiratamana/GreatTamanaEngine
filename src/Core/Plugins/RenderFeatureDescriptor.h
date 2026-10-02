#pragma once

#include <cstdint>
#include <cstddef>

// better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
// RELOCATED here, verbatim, from `plugins/gte_plugin_abi/RenderFeatureDescriptor.h`
// (deleted this same phase alongside the rest of the ABI-only `plugins/` tree).
// This is a direct, gte_core-owned analog of Landmine A's own PHASE1 precedent
// (`src/Core/EditorPanelModule.h`): `GtePluginRenderFeatureDescriptor`/
// `RenderFeatureStage`/`RenderFeatureBlendMode`/`MakeRenderFeatureDescriptor()`
// are NOT ABI-only, despite having lived under the ABI folder - they are
// directly, permanently used by `Core::RegisterProjectRenderFeature()`/
// `Core::AddScreenPostProcessPass()` (`src/Core/Core.h/.cpp`) and
// `RenderFeatureCompositor::RegisterProjectFeature()`
// (`src/Core/Plugins/RenderFeatureCompositor.h/.cpp`) - both explicitly,
// permanently KEPT per `PHASE0_MASTER_STRATEGY.md`'s own Step 1 "Explicitly
// KEPT, forever" list. This relocation was NOT itemized in
// `PHASE0_MASTER_STRATEGY.md`'s own Section 2.2 "Three Landmines" audit (a
// fourth landmine that audit missed) - see `PHASE4_COMPLETION_REPORT.md` for
// the full, honest account of why this was necessary.
//
// Everything below is an exact, byte-for-byte copy of the original file's own
// content - only this top-of-file comment changed.

namespace gte {

// PHASE0_MASTER_STRATEGY.md Locked Design Decision #1 (editor-core-separation-6
// campaign): as of that campaign, ONLY RenderFeatureStage::PostComposite and
// RenderFeatureStage::PreUI were wired into the live render graph through
// THIS enum's own entry point (RenderFeatureCompositor::RegisterProjectFeature(),
// reached via Core::RegisterProjectRenderFeature()).
//
// better-render-pass-5 effort, BLOCK 3
// (task_manager/better-render-pass-5/PHASE0_MASTER_STRATEGY.md) -
// RenderFeatureStage::PreOpaque is now ALSO real and wired, but through a
// DIFFERENT, SEPARATE, PreOpaque-specific entry point:
// Core::AddPreOpaquePass() / RenderFeatureCompositor::RegisterPreOpaqueFeature()
// (src/Core/Plugins/RenderFeatureCompositor.h) - NOT RegisterProjectFeature().
// A caller that still passes RenderFeatureStage::PreOpaque to
// RegisterProjectFeature() (the OLD, blend-chain-shaped entry point built
// for PostComposite/PreUI) is STILL refused, loudly, exactly as before -
// that entry point's own storage shape (a bounded GPU-state slot pool, a
// private blend target to composite onto) simply does not apply to a
// PreOpaque pass, which has no "screen so far" to blend onto (nothing has
// been drawn yet this point in the frame) - it draws into its own,
// self-managed render view instead and Publish()es straight to the
// RenderPassBlackboard. See docs/conventions/project-assembly-system.md's
// own "PreOpaque passes" subsection for the full, authoritative contract.
//
// PostOpaque/PostTransparent are wired via their OWN, separate entry
// points - Core::AddPostOpaquePass()/Core::AddPostTransparentPass()
// (src/Core/Plugins/RenderFeatureCompositor.h, RegisterPostOpaqueFeature()/
// RegisterPostTransparentFeature()) - mirroring PreOpaque's own precedent
// exactly. RegisterProjectFeature() (the OLD, blend-chain-shaped entry
// point) still refuses all three of PreOpaque/PostOpaque/PostTransparent,
// unchanged. Numeric values are stable and must never be renumbered once
// shipped.
enum class RenderFeatureStage : std::uint32_t {
    PreOpaque       = 0,  // WIRED - but ONLY via Core::AddPreOpaquePass();
                          // RegisterProjectFeature() still refuses it (see above).
    PostOpaque      = 1,  // WIRED - but ONLY via Core::AddPostOpaquePass();
                          // RegisterProjectFeature() still refuses it (see above).
    PostTransparent = 2,  // WIRED - but ONLY via Core::AddPostTransparentPass();
                          // RegisterProjectFeature() still refuses it (see above).
    PostComposite   = 3,  // WIRED - today's existing single hook point.
    PreUI           = 4,  // WIRED - runs immediately AFTER every PostComposite
                          // entry, same hook point, same frame.
};

// PHASE0_MASTER_STRATEGY.md Locked Design Decision #10 (editor-core-separation-6
// campaign) - Replace is the legacy hard overwrite (still legal, still
// available); the other four are real, host-owned GPU blends
// (RenderFeatureBlend.comp).
enum class RenderFeatureBlendMode : std::uint32_t {
    Replace         = 0,
    AlphaOver       = 1,
    Additive        = 2,
    Multiply        = 3,
    ScreenSpaceMask = 4,
};

// A caller sets these once, typically via MakeRenderFeatureDescriptor() below.
// The HOST NEVER trusts a caller to self-order at runtime -
// RenderFeatureCompositor groups by stage, sorts by priority ascending WITHIN
// each stage, and reuses that one resolved ordering every subsequent frame
// (never re-sorted per-frame unless SetFeaturePriority() is called).
struct GtePluginRenderFeatureDescriptor {
    // Display name - shown in the Editor's "Render Graph" panel and every
    // diagnostic log line (collision warnings, unwired-stage warnings). Must
    // be a short, human-readable, null-terminated string; truncated safely if
    // longer than 63 characters (see MakeRenderFeatureDescriptor() below, the
    // bounded-copy helper every caller should use to fill this field).
    char name[64];

    RenderFeatureStage stage;

    // Lower runs first WITHIN the same stage. Caller-declared, NEVER
    // auto-assigned by the host (except AddScreenPostProcessPass()'s own
    // documented auto-priority convenience wrapper). Two entries in the SAME
    // stage with the SAME priority is a declared CONFLICT -
    // RenderFeatureCompositor logs a loud GTE_LOG_WARNING naming both entries
    // by their `name` field, then falls back to a documented, STABLE
    // tie-break (lexical comparison of `name`) purely so the engine never
    // crashes - the ambiguity is a visible, actionable fact, never a pixel
    // mystery.
    std::int32_t priority;

    RenderFeatureBlendMode blendMode;
};

// Bounded, ALWAYS-null-terminated copy into GtePluginRenderFeatureDescriptor::name.
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
