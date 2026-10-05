#pragma once

#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderPipeline.h"

#include <volk.h>

#include <cassert>
#include <cstddef>
#include <functional>
#include <vector>

namespace gte {

// Everything a PostOpaque/PostTransparent callback needs to sample the
// current view's own already-rendered color/depth, for exactly the view it
// was invoked for. colorHandle/depthHandle carry the same underlying value
// in this engine today (color and depth are two sub-resources of one
// imported texture) - kept as two separate, named fields so a callback's
// own body can say which sub-resource a given pass.ReadTexture() call is
// declaring a dependency against, without needing to know that fact about
// this engine's internals. colorSampler/depthImageView/depthSampler are
// plain, read-only Vulkan objects obtained directly off the owning
// RenderTexture - the generic rg::PassContext::resolveReadTexture() path
// cannot produce a working sampler for this resource shape (every imported
// texture resolves with a null sampler, and there is no depth-view
// resolution path at all).
//
// VALID FOR EXACTLY ONE CALLBACK INVOCATION. Never store a copy of this
// value, or any of its raw Vulkan fields, past the single invocation it was
// handed in - not in a member, not in a static, not published onto the
// Blackboard. The physical resource every field points at belongs to this
// frame's render graph only.
struct ScenePassReadHandles {
    rg::TextureHandle colorHandle;
    rg::TextureHandle depthHandle;
    VkSampler colorSampler = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkSampler depthSampler = VK_NULL_HANDLE;
};

// Shared callback type for both the PostOpaque and PostTransparent stages -
// one shape, not two near-duplicates. The callback is trusted to call
// builder.AddRenderPass()/AddPass()/AddComputePass() itself, exactly like
// every other Project Assembly callback in this engine - every pass it
// declares must carry the ONE required RenderPassEvent tag for whichever
// stage registered it (AfterOpaques for PostOpaque, AfterTransparents for
// PostTransparent); nothing here auto-injects that tag or a read
// declaration on the callback's behalf. Carries blackboard directly -
// mirrors ProjectPreOpaqueCallback's own identical parameter - since a
// callback has no other way to Publish() per-view derived data for a
// later PostComposite/PreUI feature to Fetch().
using ProjectScenePassCallback = std::function<void(rg::RenderGraphBuilder& builder,
    rg::RenderPassBlackboard& blackboard, rg::RenderViewId currentView,
    const ScenePassReadHandles& currentViewHandles)>;

// Pure, side-effect-free, directly Tier-1-testable - the tag-check half of
// the runtime safety net, parameterized by the expected tag (AfterOpaques
// for a PostOpaque caller, AfterTransparents for a PostTransparent one).
// Scans every pass in `builder` whose declaration index is in the
// half-open range [before, after) and returns the (absolute,
// builder-relative) indices of every one whose RenderPassEvent is not
// EXACTLY `expected`. An empty returned vector means every pass in range
// was tagged correctly - including the case where the range itself is
// empty (nothing to mis-tag).
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

// Pure, side-effect-free, directly Tier-1-testable - the second half of the
// runtime safety net. Returns true (a VIOLATION) only when the callback
// declared at least one pass in [before, after) AND none of those passes
// declared a read (via RenderGraphBuilder::PassReadsTexture()) against
// EITHER colorHandle or depthHandle. Returns false (no violation) whenever
// the range is empty - a callback that legitimately declares zero passes
// this invocation (e.g. nothing to do for this view this frame) is a
// normal, correct no-op, never a violation; only once at least one real
// pass exists does "did it forget the matching read" become meaningful.
//
// KNOWN, ACCEPTED LIMITATION: because colorHandle and depthHandle are the
// identical underlying handle value in this engine today, this can only
// confirm "the callback declared SOME read against this one handle, in
// SOME aspect" - it cannot distinguish a correctly-aspected declaration
// from one that declares color but binds depth (or vice versa) in its own
// execute lambda. That remains the author's own responsibility; this check
// only catches a totally missing declaration.
inline bool FindScenePassCallbackMissingReadDeclaration(const rg::RenderGraphBuilder& builder, std::size_t before,
    std::size_t after, rg::TextureHandle colorHandle, rg::TextureHandle depthHandle)
{
    assert(before <= after && after <= builder.DeclaredPassCount()
        && "FindScenePassCallbackMissingReadDeclaration() - [before, after) out of range");

    if (before == after) {
        return false; // Zero declared passes this invocation - never a violation.
    }

    for (std::size_t index = before; index < after; ++index) {
        if (builder.PassReadsTexture(index, colorHandle) || builder.PassReadsTexture(index, depthHandle)) {
            return false;
        }
    }
    return true; // At least one pass was declared, and none of them read either handle.
}

// Depth-aspect-aware sibling of FindScenePassCallbackMissingReadDeclaration()
// above, built on RenderGraphBuilder::PassReadsTextureAsDepth() instead of
// the aspect-blind PassReadsTexture() - flags a violation even when a pass
// in range declared a COLOR-only read against the same handle, since that
// is not a valid declaration for resolving the DEPTH sub-resource. Returns
// false whenever the range is empty, mirroring its sibling exactly.
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
