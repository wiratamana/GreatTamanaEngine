#pragma once

// Renderer-internal validation helpers for the plugin render-feature
// callback contracts (Core/Plugins/ProjectRenderFeatureCallback.h/
// ProjectScenePassCallback.h) - these need the full rg::RenderGraphBuilder
// definition, so they live here instead of in those Vulkan-free headers.
// Used only by Core.cpp/RenderFeatureCompositor.cpp to confirm a callback's
// own declared passes carry the RenderPassEvent tag their stage requires.

#include "RenderGraphBuilder.h"
#include "RenderGraphTypes.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace gte {

// Every pass in [before, after) must be tagged RenderPassEvent::AfterEverything
// (the PostComposite/PreUI stage's one required tag). Returns the
// (builder-relative) indices of every violator - empty means all correct.
inline std::vector<std::size_t> FindPassesNotTaggedAfterEverything(
    const rg::RenderGraphBuilder& builder, std::size_t before, std::size_t after)
{
    assert(before <= after && after <= builder.DeclaredPassCount()
        && "FindPassesNotTaggedAfterEverything() - [before, after) out of range");

    std::vector<std::size_t> violations;
    for (std::size_t index = before; index < after; ++index) {
        if (builder.PassEventAt(index) != rg::RenderPassEvent::AfterEverything) {
            violations.push_back(index);
        }
    }
    return violations;
}

// Every pass in [before, after) must be tagged exactly `expected`
// (AfterOpaques for PostOpaque, AfterTransparents for PostTransparent).
// Returns the indices of every violator - empty means all correct.
inline std::vector<std::size_t> FindPassesNotTaggedScenePass(
    const rg::RenderGraphBuilder& builder, std::size_t before, std::size_t after, rg::RenderPassEvent expected)
{
    assert(before <= after && after <= builder.DeclaredPassCount()
        && "FindPassesNotTaggedScenePass() - [before, after) out of range");

    std::vector<std::size_t> violations;
    for (std::size_t index = before; index < after; ++index) {
        if (builder.PassEventAt(index) != expected) {
            violations.push_back(index);
        }
    }
    return violations;
}

// True only when at least one pass was declared in [before, after) and NONE
// of them read colorHandle or depthHandle - a likely forgotten dependency
// declaration. False for an empty range (nothing declared is never a
// violation).
inline bool FindScenePassCallbackMissingReadDeclaration(const rg::RenderGraphBuilder& builder, std::size_t before,
    std::size_t after, rg::TextureHandle colorHandle, rg::TextureHandle depthHandle)
{
    assert(before <= after && after <= builder.DeclaredPassCount()
        && "FindScenePassCallbackMissingReadDeclaration() - [before, after) out of range");

    if (before == after) {
        return false;
    }

    for (std::size_t index = before; index < after; ++index) {
        if (builder.PassReadsTexture(index, colorHandle) || builder.PassReadsTexture(index, depthHandle)) {
            return false;
        }
    }
    return true;
}

// Depth-aspect-aware sibling of FindScenePassCallbackMissingReadDeclaration()
// above - flags a violation even if a pass only declared a color-only read
// against the same handle, which does not resolve the depth sub-resource.
inline bool FindMissingDeclaredDepthReadForResolve(
    const rg::RenderGraphBuilder& builder, std::size_t before, std::size_t after, rg::TextureHandle depthHandle)
{
    assert(before <= after && after <= builder.DeclaredPassCount()
        && "FindMissingDeclaredDepthReadForResolve() - [before, after) out of range");

    if (before == after) {
        return false;
    }

    for (std::size_t index = before; index < after; ++index) {
        if (builder.PassReadsTextureAsDepth(index, depthHandle)) {
            return false;
        }
    }
    return true;
}

} // namespace gte
