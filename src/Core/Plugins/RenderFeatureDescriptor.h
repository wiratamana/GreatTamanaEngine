#pragma once

#include <cstdint>
#include <cstddef>

// Render-feature descriptor types shared by Core and RenderFeatureCompositor.

namespace gte {

// PreOpaque/PostOpaque/PostTransparent are each wired through their own
// dedicated entry point (Core::AddPreOpaquePass()/AddPostOpaquePass()/
// AddPostTransparentPass()). PostComposite/PreUI go through
// Core::RegisterProjectRenderFeature()/RegisterBuiltInRenderFeature(). Values
// are stable and must never be renumbered once shipped.
enum class RenderFeatureStage : std::uint32_t {
    PreOpaque       = 0,
    PostOpaque      = 1,
    PostTransparent = 2,
    PostComposite   = 3,
    PreUI           = 4, // Runs immediately after every PostComposite entry, same frame.
};

// Replace is a hard overwrite; the rest are real GPU blends (RenderFeatureBlend.comp).
enum class RenderFeatureBlendMode : std::uint32_t {
    Replace         = 0,
    AlphaOver       = 1,
    Additive        = 2,
    Multiply        = 3,
    ScreenSpaceMask = 4,
};

// Who owns a registered render feature - used to tell a project-authored
// feature apart from an engine built-in one in debug tooling (GET
// /render_graph, the "Render Graph" panel). Never inferred from any other
// field - always set explicitly at registration time.
enum class RenderFeatureOwner : std::uint8_t { Engine, Project };

// A caller sets these once, typically via MakeRenderFeatureDescriptor() below.
// RenderFeatureCompositor groups by stage, sorts by priority ascending within
// each stage, and never re-sorts a stage unless SetFeaturePriority() is called.
struct GtePluginRenderFeatureDescriptor {
    // Shown in the "Render Graph" panel and in diagnostic logs. Null-terminated,
    // truncated safely at 63 characters by MakeRenderFeatureDescriptor() below.
    char name[64];

    RenderFeatureStage stage;

    // Lower runs first within the same stage. Two entries in the same stage
    // with the same priority log a warning and fall back to a lexical
    // name tie-break - never a crash.
    std::int32_t priority;

    RenderFeatureBlendMode blendMode;
};

// Bounded, always-null-terminated copy into GtePluginRenderFeatureDescriptor::name.
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
