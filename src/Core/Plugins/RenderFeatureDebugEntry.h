#pragma once

#include <cstdint>
#include <string>

// Plain, dependency-free debug snapshot of one registered render feature,
// produced by RenderFeatureCompositor::DebugSnapshot() and consumed by the
// Editor's "Render Graph" panel and GET /render_graph.
namespace gte {

struct RenderFeatureDebugEntry {
    std::string name;      // Copied from descriptor.name (bounded char[64]).
    std::string stage;     // "PreOpaque"/"PostOpaque"/"PostTransparent"/"PostComposite"/"PreUI".
    std::int32_t priority = 0;
    std::string blendMode; // "Replace"/"AlphaOver"/"Additive"/"Multiply"/"ScreenSpaceMask"/"None".

    // The combined gate: host-side enabledOverride AND the toggle registry's
    // own flag, when the name is independently known there too.
    bool enabled = true;

    // True for a Project Assembly feature, false for an engine built-in one.
    // Set explicitly by the registering code (RenderFeatureOwner) - never
    // inferred from any other field.
    bool isProjectFeature = false;
};

} // namespace gte
