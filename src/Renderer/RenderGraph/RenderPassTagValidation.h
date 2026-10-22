#pragma once

// Validation helpers for the plugin render-feature callback contracts. They need the full RenderGraphBuilder,
// so they live here and not in the Vulkan-free callback headers. Used by Core.cpp and RenderFeatureCompositor.cpp.

#include "RenderGraphBuilder.h"
#include "RenderGraphTypes.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace gte {

// Every pass in [before, after) must be tagged RenderPassEvent::AfterEverything.
// Returns the (builder-relative) indices of every violator.
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

// Every pass in [before, after) must be tagged exactly `expected` (Opaques, AfterOpaques or AfterTransparents).
// Returns the indices of every violator.
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

// True when passes were declared in [before, after) and none read colorHandle or depthHandle (a likely forgotten
// dependency). A depth-attachment write on depthHandle counts: a depth test reads it, and a fake read needs another layout.
inline bool FindScenePassCallbackMissingReadDeclaration(const rg::RenderGraphBuilder& builder, std::size_t before,
    std::size_t after, rg::TextureHandle colorHandle, rg::TextureHandle depthHandle)
{
    assert(before <= after && after <= builder.DeclaredPassCount()
        && "FindScenePassCallbackMissingReadDeclaration() - [before, after) out of range");

    if (before == after) {
        return false;
    }

    for (std::size_t index = before; index < after; ++index) {
        if (builder.PassReadsTexture(index, colorHandle) || builder.PassReadsTexture(index, depthHandle)
            || builder.PassWritesTextureAsDepthReadWrite(index, depthHandle)) {
            return false;
        }
    }
    return true;
}

// Depth-aware variant: a color-only read of the same handle does not count as reading its depth sub-resource.
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
