#pragma once

// Hook pair that passes per-pass debug metadata (category, draw kind, owner, barrier labels) to the
// Editor's Frame Debugger. IPassDebugMetadataSink is written as passes are declared; the provider is
// queried when a snapshot is built. Only the Editor implements them; unset pointers cost one null check.

#include "RenderGraphTypes.h"

#include <cstddef>
#include <string>
#include <string_view>


namespace gte::rg {

// Pass category/draw-kind/owning-feature payload passed through the sink
// instead of being stored permanently on the hot PassRecord struct.
struct PassDebugMetadata {
    RenderPassCategory category = RenderPassCategory::General;
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;
    // Empty only if never declared through AddRenderPass()/AddBlitPass().
    std::string owningFeatureName;
};

// Write-only interface for recording per-pass debug metadata as passes are
// declared and executed. Core never implements this - only the Editor does.
class IPassDebugMetadataSink {
public:
    virtual ~IPassDebugMetadataSink() = default;

    // Called once per declared pass, in declaration order starting at 0
    // each frame (declarationIndexThisFrame has no gaps).
    virtual void OnPassDeclared(std::size_t declarationIndexThisFrame, RenderPassCategory category,
        RenderPassDrawKind drawKind, std::string_view owningFeatureName) = 0;

    // Called once per fresh graph declaration, before any pass is declared,
    // to reset whatever per-frame state the real implementation keeps.
    virtual void BeginFrame() = 0;

    // Fires once per write that actually required a barrier, at execute time.
    // declarationIndex matches OnPassDeclared()'s pass index; `transitionLabel` is the display text, e.g. "Undefined -> RT".
    virtual void OnResourceBarrierApplied(
        std::size_t declarationIndex, const std::string& resourceName, std::string transitionLabel) = 0;
};

// Read-only counterpart to IPassDebugMetadataSink. Core queries this when
// building a RenderGraphSnapshot; only the Editor ever implements it.
class IPassDebugMetadataProvider {
public:
    virtual ~IPassDebugMetadataProvider() = default;

    // Returns true and fills outMetadata if declarationIndex has a live
    // entry. Returns false (outMetadata left untouched) otherwise - never
    // throws, treat false exactly like "no sink installed".
    virtual bool QueryPassDebugMetadata(std::size_t declarationIndex, PassDebugMetadata& outMetadata) const = 0;

    // Returns true and fills outLabel if a barrier transition label was
    // recorded for this (declarationIndex, resourceName) pair this frame.
    virtual bool QueryBarrierTransitionLabel(
        std::size_t declarationIndex, const std::string& resourceName, std::string& outLabel) const = 0;
};

} // namespace gte::rg
