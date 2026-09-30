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

    // Parallel to CompiledGraphInput::textures/buffers/volumeTextures (same
    // index) - one ResourceLifetime per declared resource, regardless of
    // whether it survived culling. render-pass-6 campaign, PHASE5 (item
    // 2.1) - reworded from the old textureDescs/bufferDescs/
    // volumeTextureDescs field names, which no longer exist.
    std::vector<ResourceLifetime> textureLifetimes;
    std::vector<ResourceLifetime> bufferLifetimes;

    // Atmosphere Scattering campaign, Phase 2
    // (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md).
    std::vector<ResourceLifetime> volumeTextureLifetimes;
};

// render-pass-4 campaign, PHASE1
// (PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md) - RenderPassEvent
// is purely descriptive metadata (see its own doc comment,
// RenderGraphTypes.h) - Compile()'s own RAW/WAW dependency scan never
// reads it. This is a SEPARATE, PURE, side-effect-free diagnostic scan
// that cross-checks the two against each other and reports every place
// they disagree - it changes NOTHING about what Compile() actually
// returns. See Compile()'s own doc comment for how a non-empty result is
// turned into a loud, unmissable diagnostic.
enum class RenderPassEventContradictionKind : std::uint8_t {
    // `readerPassIndex` reads a resource with no writer declared anywhere
    // BEFORE it in `processingOrder` - but `writerPassIndex` (declared
    // somewhere AFTER it in `processingOrder`) writes that exact same
    // resource. This is the precise, confirmed root-cause pattern behind
    // the documented, already-occurred "AtmosphereComposite silently
    // culls RenderOpaque" bug (see PHASE0_MASTER_STRATEGY.md, Step 2) -
    // `Compile()`'s own RAW-edge scan can never link this read to that
    // write, because "the most recent pass, among those declared so far,
    // that wrote it" (RenderGraphCompiler.cpp's own header comment) never
    // includes a write that hasn't happened "so far" yet.
    OrphanReadWithLaterWriter,
    // A real RAW dependency was formed - `writerPassIndex` genuinely,
    // provably runs before `readerPassIndex` - but `writerPassIndex`'s own
    // declared RenderPassEvent is LATER (numerically greater) than
    // `readerPassIndex`'s. The real dependency is still honored correctly
    // either way (Compile() doesn't need this to be consistent to be
    // CORRECT) - but the disagreement is a strong signal that one of the
    // two passes has the wrong RenderPassEvent tag for what it actually
    // does, and is worth a human looking at before it becomes a real bug
    // the next time either pass's real dependency set changes.
    DeclaredEventOrderDisagreesWithRealDependency,
};

struct RenderPassEventContradiction {
    RenderPassEventContradictionKind kind = RenderPassEventContradictionKind::OrphanReadWithLaterWriter;
    std::int32_t readerPassIndex = -1;
    std::int32_t writerPassIndex = -1;
    ResourceKind resourceKind = ResourceKind::Texture;
    std::uint32_t resourceIndex = 0;
};

// A pure, Tier-1-testable scan - never prints, never asserts, never
// throws, and never mutates `input`. `processingOrder` must be a
// permutation of `[0, input.passes.size())`, expressing "which order will
// the compiler actually resolve a read's nearest prior writer in" - PHASE1
// (this campaign's first phase) called this with the identity permutation
// (raw declaration order, `[0, 1, 2, ...]`), matching Compile()'s
// then-current algorithm exactly. render-pass-4 campaign, PHASE2
// (PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md) - Compile() now
// instead passes its own new "effective order" (every pass stable-sorted by
// RenderPassEvent, then original declaration index) here - this function's
// signature was deliberately already shaped for that, so PHASE2 never had
// to touch this function's own logic, only what it is called with.
// "Before"/"after" in both RenderPassEventContradictionKind enumerators
// above always means "earlier/later in `processingOrder`", never
// "smaller/larger original pass index".
std::vector<RenderPassEventContradiction> DetectRenderPassEventContradictions(
    const CompiledGraphInput& input, std::span<const std::int32_t> processingOrder);

// Compiles `input` against the REQUIRED root set `finalOutputs` - the
// texture handles the caller actually needs to exist by the end of this
// frame (e.g. the swapchain image the Present pass writes, and nothing
// else). ALSO reads `input.finalVolumeTextureOutputs` (Atmosphere
// Scattering campaign, Phase 6 -
// ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md - see
// RenderGraphBuilder::KeepVolumeTextureOutput()) as a SECOND, independent
// root set for VolumeTextureHandle writes, and ALSO reads
// `input.finalBufferOutputs` (editor-core-separation-26 campaign, PHASE1 -
// BIG_STEP_2_BUFFER_ROOTS_AND_BLIT_PASSES_2026-09-29.txt, Part A - see
// RenderGraphBuilder::KeepBufferOutput()) as a THIRD, independent root set
// for BufferHandle writes. A pass with no path (direct
// or transitive, through declared reads/writes) to any `finalOutputs`/
// `finalVolumeTextureOutputs` entry is dead code and is culled entirely:
// excluded from `executionOrder`, and none of its declared reads/writes
// extend any resource's lifetime.
//
// render-pass-4 campaign, PHASE1 - Compile() cross-checks every pass's
// declared RenderPassEvent against its real resource dependencies, turning a
// disagreement into an unconditional stderr report plus a debug-build
// assert() - see this header's own RenderPassEventContradiction doc comment
// above and RenderGraphCompiler.cpp's own wiring. This changes NOTHING about
// the algorithm below; it is a pure diagnostic side-channel.
//
// render-pass-6 campaign, PHASE4 (item 2.3,
// task_manager/render-pass-6/PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md) -
// Compile() no longer calls the standalone DetectRenderPassEventContradictions()
// function above at all (that function remains fully intact, independently
// callable/tested exactly as before - it is simply no longer Compile()'s own
// diagnostic implementation). Instead, Compile() computes an equivalent
// result INLINE, reusing the exact same last-writer bookkeeping
// (lastTextureWriter/lastBufferWriter/lastVolumeTextureWriter) its own
// RAW/WAW edge-construction walk (Step 1, below) already builds - once,
// reusing that walk's own state, rather than the standalone function's
// independent O(P^2*R) backward-then-forward scan. The check is threaded
// through Step 1's per-read-usage loop (exactly where each read's nearest
// prior writer is already being computed for edge-construction purposes) and
// reported, via the exact same stderr/assert code as before, once Step 1
// finishes walking every pass - still strictly BEFORE Step 2's culling, so
// the OBSERVABLE behavior (stderr text, assert firing before culling/
// reordering ever happens) is unchanged; only WHERE in the function body this
// now happens changed, from a separate pre-pass to folded into Step 1 itself.
//
// render-pass-4 campaign, PHASE2
// (PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md) - Compile() now
// ALSO computes an "effective order" - every pass stable-sorted by
// (RenderPassEvent, original declaration index) - and walks passes in THAT
// order (instead of raw declaration order) for both its RAW/WAW edge scan
// and Kahn's-algorithm ready-set tie-break. RenderPassEvent is therefore
// real, load-bearing ordering input for the first time: it can no longer
// override a genuine data dependency in the opposite direction (a real
// dependency always wins), but it now determines execution order between
// passes with no real dependency on each other, and it now determines which
// writer a read resolves to when a naive declaration-order-only scan would
// otherwise miss it (the historical "AtmosphereComposite silently culls
// RenderOpaque" bug shape - see PHASE0_MASTER_STRATEGY.md). This does NOT
// make an incorrectly-tagged RenderPassEvent value harmless - see
// DetectRenderPassEventContradictions() above, now checked against this same
// effective order.
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
