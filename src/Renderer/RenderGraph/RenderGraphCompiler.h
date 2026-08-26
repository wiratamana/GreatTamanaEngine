#pragma once

// Phase 3 (RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md, part 3 of the
// wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - "The Smart
// Planner": turns Phase 2's inert CompiledGraphInput (a bag of declared
// passes/resources, in whatever order the caller happened to call
// AddPass()) into a genuinely COMPILED artifact - a linear pass EXECUTION
// ORDER that respects every declared read-after-write/write-after-write
// dependency, with every pass that provably contributes nothing to a final
// output CULLED out entirely, and with every resource's exact LIFETIME
// (the first/last pass index that touches it) computed once, up front.
//
// This is pure graph algorithm - topological sort plus reachability
// analysis - and, like Phase 1/2 before it, is entirely Vulkan-free and
// Tier-1-testable: Compile() touches no VkDevice, no Renderer, nothing
// GPU-shaped at all. See tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp.
//
// Nothing outside src/Renderer/RenderGraph/ includes this header yet, and
// nothing calls Compile() from production code yet - that is deliberate
// (this phase's own "What We Will NOT Do": Phase 6 is the first real
// consumer, tying this together with Phase 4's physical resource
// realization and Phase 5's barrier synthesis into actual Vulkan
// recording).

#include "RenderGraphBuilder.h"
#include "RenderGraphTypes.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gte::rg {

// A single resource's lifetime, expressed purely as INDICES INTO
// CompiledGraph::executionOrder (never a raw pass-declaration index) -
// this is what lets Phase 4 answer "does a pooled resource left over from
// last frame need to still be alive by the time THIS pass runs" without
// re-deriving execution order itself. `-1` means "never used" (either the
// resource was never referenced by any surviving pass at all, or every
// pass that touched it was culled) - never a valid index, since a real
// executionOrder position is always >= 0.
//
// A resource's `lastUsePassIndex` covers BOTH reads and writes that touch
// it, not just reads - a resource's last WRITE with no subsequent read is
// still kept alive through that write's own pass, since the write itself
// needs the resource to exist (see RENDERGRAPH_PHASE3_COMPILATION_STRATEGY_v1.md,
// Step 3.2.4).
struct ResourceLifetime {
    std::int32_t firstUsePassIndex = -1;
    std::int32_t lastUsePassIndex = -1;

    friend bool operator==(const ResourceLifetime&, const ResourceLifetime&) noexcept = default;
};

// The compiled artifact Compile() produces - a linear, topologically
// sorted, already-culled pass list plus a parallel-to-CompiledGraphInput's
// own texture/buffer tables of resource lifetimes. `executionOrder` is
// EMPTY for a graph with no reachable passes (e.g. an empty input, or a
// `finalOutputs` set nothing ever writes) - that is a valid, non-error
// result, not a failure.
struct CompiledGraph {
    // Topologically sorted, culled-passes-EXCLUDED execution order. Each
    // PassHandle::index is the pass's original declaration index into
    // CompiledGraphInput::passes (i.e. Phase 6's executor resolves a
    // PassHandle back to the real PassRecord via
    // `input.passes[handle.index]`) - PassHandle::generation is always 1,
    // mirroring RenderGraphBuilder::CreateTexture()/CreateBuffer()'s own
    // "every minted handle starts at generation 1" convention, since
    // nothing else mints/recycles a PassHandle's generation today.
    std::vector<PassHandle> executionOrder;

    // Parallel to CompiledGraphInput::textureDescs/bufferDescs (same
    // index) - one ResourceLifetime per declared resource, regardless of
    // whether it survived culling.
    std::vector<ResourceLifetime> textureLifetimes;
    std::vector<ResourceLifetime> bufferLifetimes;
};

// Compiles `input` against the REQUIRED root set `finalOutputs` - the
// texture handles the caller actually needs to exist by the end of this
// frame (e.g. the swapchain image the Present pass writes, and nothing
// else). A pass with no path (direct or transitive, through declared
// reads/writes) to any `finalOutputs` entry is dead code and is culled
// entirely: excluded from `executionOrder`, and none of its declared
// reads/writes extend any resource's lifetime.
//
// `input` is taken by NON-CONST reference (not `const&`, despite this
// phase's own strategy document sketching a `const&` signature) because
// Compile() is the ONE place `PassRecord::isCulled` (see
// RenderGraphTypes.h) is ever written - every pass that does not survive
// culling has `input.passes[i].isCulled` set to `true` here (and every
// pass that DOES survive is explicitly set to `false`, so calling
// Compile() again on an already-compiled input is always safe/idempotent,
// never leaves a stale `true` behind from a previous, different
// `finalOutputs` set).
//
// Given the SAME `input` (same passes, declared in the same order, same
// reads/writes) and the SAME `finalOutputs`, Compile() always produces the
// exact same `executionOrder` - see this phase's own Step 3.3. Throws
// `std::runtime_error` if the kept passes' declared dependencies form a
// cycle that cannot be topologically ordered - see this header's
// implementation file for why that specific condition can only ever be
// reached defensively, never through an ordinarily-declared graph (a
// documented finding from this session, not an oversight).
CompiledGraph Compile(CompiledGraphInput& input, std::span<const TextureHandle> finalOutputs);

} // namespace gte::rg
