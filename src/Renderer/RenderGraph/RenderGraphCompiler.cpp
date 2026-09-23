#include "RenderGraphCompiler.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <set>
#include <stdexcept>

namespace gte::rg {

namespace {

bool ContainsTextureHandle(std::span<const TextureHandle> handles, const TextureHandle& handle)
{
    for (const TextureHandle& candidate : handles) {
        if (candidate == handle) {
            return true;
        }
    }
    return false;
}

// Atmosphere Scattering campaign, Phase 6
// (ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md) - the
// VolumeTextureHandle sibling of ContainsTextureHandle() above, used by the
// root-marking scan below to fix a genuine Phase 2 gap (see
// CompiledGraphInput::finalVolumeTextureOutputs's own doc comment,
// RenderGraphBuilder.h).
bool ContainsVolumeTextureHandle(std::span<const VolumeTextureHandle> handles, const VolumeTextureHandle& handle)
{
    for (const VolumeTextureHandle& candidate : handles) {
        if (candidate == handle) {
            return true;
        }
    }
    return false;
}

} // namespace

// render-pass-4 campaign, PHASE1
// (PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md) - see this
// function's own declaration (RenderGraphCompiler.h) for the full doc
// comment. Deliberately fully self-contained/independent of Compile()'s
// own lastTextureWriter/lastBufferWriter/lastVolumeTextureWriter
// bookkeeping arrays below - this keeps it safely callable from a test
// with an arbitrary hand-built CompiledGraphInput, with zero dependency
// on Compile()'s own internals. Never prints, never asserts, never
// throws, and never mutates `input`.
std::vector<RenderPassEventContradiction> DetectRenderPassEventContradictions(
    const CompiledGraphInput& input, std::span<const std::int32_t> processingOrder)
{
    std::vector<RenderPassEventContradiction> contradictions;

    // Tiny helper: does `usage` refer to the same resource as `other`?
    auto sameResource = [](const ResourceUsage& a, const ResourceUsage& b) {
        if (a.kind != b.kind) {
            return false;
        }
        switch (a.kind) {
        case ResourceKind::Texture:
            return a.texture == b.texture;
        case ResourceKind::Buffer:
            return a.buffer == b.buffer;
        case ResourceKind::VolumeTexture:
            return a.volumeTexture == b.volumeTexture;
        }
        return false;
    };

    for (std::size_t readerPos = 0; readerPos < processingOrder.size(); ++readerPos) {
        const std::int32_t readerIndex = processingOrder[readerPos];
        const PassRecord& reader = input.passes[static_cast<std::size_t>(readerIndex)];

        for (const ResourceUsage& read : reader.reads) {
            // Find the NEAREST writer strictly before readerPos in processingOrder.
            std::int32_t nearestWriterIndex = -1;
            for (std::int32_t candidatePos = static_cast<std::int32_t>(readerPos) - 1; candidatePos >= 0;
                 --candidatePos) {
                const PassRecord& candidate = input.passes[static_cast<std::size_t>(processingOrder[static_cast<std::size_t>(candidatePos)])];
                bool writesIt = false;
                for (const ResourceUsage& write : candidate.writes) {
                    if (sameResource(write, read)) {
                        writesIt = true;
                        break;
                    }
                }
                if (writesIt) {
                    nearestWriterIndex = processingOrder[static_cast<std::size_t>(candidatePos)];
                    break;
                }
            }

            if (nearestWriterIndex == -1) {
                // No writer found before this read in processingOrder - is
                // there one ANYWHERE (i.e. strictly after, in
                // processingOrder terms)?
                for (std::int32_t otherPos = static_cast<std::int32_t>(readerPos) + 1;
                     otherPos < static_cast<std::int32_t>(processingOrder.size()); ++otherPos) {
                    const std::int32_t otherIndex = processingOrder[static_cast<std::size_t>(otherPos)];
                    const PassRecord& other = input.passes[static_cast<std::size_t>(otherIndex)];
                    for (const ResourceUsage& write : other.writes) {
                        if (sameResource(write, read)) {
                            RenderPassEventContradiction contradiction;
                            contradiction.kind = RenderPassEventContradictionKind::OrphanReadWithLaterWriter;
                            contradiction.readerPassIndex = readerIndex;
                            contradiction.writerPassIndex = otherIndex;
                            contradiction.resourceKind = read.kind;
                            contradiction.resourceIndex = read.kind == ResourceKind::Texture ? read.texture.index
                                : read.kind == ResourceKind::Buffer                          ? read.buffer.index
                                                                                              : read.volumeTexture.index;
                            contradictions.push_back(contradiction);
                            break; // One report per (reader, resource) pair is enough.
                        }
                    }
                }
            } else {
                const PassRecord& writer = input.passes[static_cast<std::size_t>(nearestWriterIndex)];
                if (writer.renderPassEvent > reader.renderPassEvent) {
                    RenderPassEventContradiction contradiction;
                    contradiction.kind = RenderPassEventContradictionKind::DeclaredEventOrderDisagreesWithRealDependency;
                    contradiction.readerPassIndex = readerIndex;
                    contradiction.writerPassIndex = nearestWriterIndex;
                    contradiction.resourceKind = read.kind;
                    contradiction.resourceIndex = read.kind == ResourceKind::Texture ? read.texture.index
                        : read.kind == ResourceKind::Buffer                          ? read.buffer.index
                                                                                       : read.volumeTexture.index;
                    contradictions.push_back(contradiction);
                }
            }
        }
    }

    return contradictions;
}

// Implementation note on cycle detection (Step 3.2.3's Kahn's-algorithm
// "naturally detects a cycle" requirement): the edge-construction rule
// below (Step 3.2.1 - "the most recently WALKED pass, among those walked so
// far in EFFECTIVE order, that wrote it" - render-pass-4 campaign, PHASE2,
// see effectiveOrder's own doc comment inside Compile() below for what
// "effective order" means and why it replaced raw declaration order) only
// ever adds an edge from a strictly earlier EFFECTIVE POSITION to a
// strictly later one (a pass can only depend on a writer already WALKED, in
// effective order, before it; there is no mechanism here for a
// earlier-walked pass to depend on a later-walked one). This makes the
// produced graph a DAG BY CONSTRUCTION, with the passes' own EFFECTIVE
// (RenderPassEvent-then-declaration-order) order already being one valid
// topological order - a real cycle (as sketched in this phase's own
// strategy document's Step 3.4, "pass A reads what B writes AND writes what
// B reads") is therefore structurally UNREACHABLE through any graph an
// author can actually declare via RenderGraphBuilder/AddPass(). The throw
// below is kept anyway, as genuinely correct defensive code (matching the
// strategy document's explicit request for Kahn's algorithm's own natural
// cycle detection) in case a future change to the edge-construction rule
// above (e.g. a WAR/read-then-later-write hazard edge) ever makes a real
// cycle possible - see RENDERGRAPH_PHASE3_COMPLETION_REPORT.md for the full
// write-up of this finding.
CompiledGraph Compile(CompiledGraphInput& input, std::span<const TextureHandle> finalOutputs)
{
    const std::int32_t passCount = static_cast<std::int32_t>(input.passes.size());

    CompiledGraph result;
    result.textureLifetimes.assign(input.textureDescs.size(), ResourceLifetime{});
    result.bufferLifetimes.assign(input.bufferDescs.size(), ResourceLifetime{});
    // Atmosphere Scattering campaign, Phase 2
    // (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md).
    result.volumeTextureLifetimes.assign(input.volumeTextureDescs.size(), ResourceLifetime{});

    if (passCount == 0) {
        return result;
    }
    // render-pass-4 campaign, PHASE2
    // (PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md) - `effectiveOrder`
    // is a permutation of [0, passCount) - every pass's ORIGINAL declaration
    // index, reordered by a STABLE sort on (RenderPassEvent, original index).
    // This is the first time RenderPassEvent becomes real, load-bearing
    // ordering input: the RAW/WAW edge scan below, and Kahn's-algorithm's own
    // ready-set tie-break, both walk passes in THIS order from now on,
    // instead of raw declaration order. A stable sort guarantees two passes
    // sharing the same RenderPassEvent tier keep their EXACT prior relative
    // order - byte-identical behavior for every pass that was already
    // correctly ordered relative to its own tier-mates (see this phase's own
    // completion report for the full audit confirming this holds for every
    // pass shipping today).
    std::vector<std::int32_t> effectiveOrder(static_cast<std::size_t>(passCount));
    for (std::int32_t i = 0; i < passCount; ++i) {
        effectiveOrder[static_cast<std::size_t>(i)] = i;
    }
    std::stable_sort(effectiveOrder.begin(), effectiveOrder.end(), [&input](std::int32_t a, std::int32_t b) {
        return input.passes[static_cast<std::size_t>(a)].renderPassEvent
            < input.passes[static_cast<std::size_t>(b)].renderPassEvent;
    });

    // effectivePosition[originalPassIndex] = that pass's position in
    // effectiveOrder - the inverse permutation, used below wherever the
    // algorithm needs to compare/order by EFFECTIVE position rather than
    // walk effectiveOrder directly.
    std::vector<std::int32_t> effectivePosition(static_cast<std::size_t>(passCount), -1);
    for (std::size_t pos = 0; pos < effectiveOrder.size(); ++pos) {
        effectivePosition[static_cast<std::size_t>(effectiveOrder[pos])] = static_cast<std::int32_t>(pos);
    }

    // render-pass-6 campaign, PHASE4 (item 2.3,
    // task_manager/render-pass-6/PHASE4_COMPILER_ADJACENCY_LIST_REWRITE.md) -
    // firstXWriter[resource] is the ORIGINAL pass index (matching
    // lastXWriter's own storage convention below) of the FIRST pass, in
    // effective order, that writes this resource, or -1 if never written by
    // anyone. This is the ONE piece Step 1's own incremental forward walk
    // (immediately below) cannot answer by itself - it cannot know about a
    // pass it hasn't reached yet - everything else the fast-path
    // contradiction check needs (the "nearest writer so far") is already
    // sitting in lastTextureWriter/lastBufferWriter/lastVolumeTextureWriter
    // by the time Step 1 processes each read. A small, separate, O(P+W)
    // preliminary walk (W = total write usages across all passes), run once,
    // before Step 1's own loop.
    std::vector<std::int32_t> firstTextureWriter(input.textureDescs.size(), -1);
    std::vector<std::int32_t> firstBufferWriter(input.bufferDescs.size(), -1);
    std::vector<std::int32_t> firstVolumeTextureWriter(input.volumeTextureDescs.size(), -1);
    for (std::int32_t pos = 0; pos < passCount; ++pos) {
        const std::int32_t i = effectiveOrder[static_cast<std::size_t>(pos)];
        for (const ResourceUsage& usage : input.passes[static_cast<std::size_t>(i)].writes) {
            switch (usage.kind) {
            case ResourceKind::Texture:
                if (usage.texture.index < firstTextureWriter.size() && firstTextureWriter[usage.texture.index] == -1) {
                    firstTextureWriter[usage.texture.index] = i;
                }
                break;
            case ResourceKind::Buffer:
                if (usage.buffer.index < firstBufferWriter.size() && firstBufferWriter[usage.buffer.index] == -1) {
                    firstBufferWriter[usage.buffer.index] = i;
                }
                break;
            case ResourceKind::VolumeTexture:
                if (usage.volumeTexture.index < firstVolumeTextureWriter.size()
                    && firstVolumeTextureWriter[usage.volumeTexture.index] == -1) {
                    firstVolumeTextureWriter[usage.volumeTexture.index] = i;
                }
                break;
            }
        }
    }

    // --- Step 1: build the dependency graph (Step 3.2.1) --------------
    //
    // render-pass-6 campaign, PHASE4 (item 2.3) - REPLACES the O(P^2)
    // adjacency MATRIX (std::vector<std::vector<bool>>) with adjacency
    // LISTS - successors[from] is every pass that must run strictly after
    // `from`; predecessors[to] is every pass that must run strictly before
    // `to`. Both are built during the SAME single O(P+E) edge-construction
    // walk below that already existed - this changes WHERE addEdge() writes
    // an edge, not HOW MANY TIMES it is called. Real pass counts per frame
    // are plausibly already in the 20-40 range (not "single digits", the
    // stale premise this comment used to state before this phase corrected
    // it) - see task_manager/render-pass-6/PHASE0_MASTER_STRATEGY.md's own
    // Step 2 evidence. A plain std::vector<std::vector<std::int32_t>> (never
    // a hash-keyed container) - matches this engine's own "no hashing on the
    // hot path" philosophy (AGENTS.md); push_back()'s own insertion order is
    // irrelevant to determinism, since Step 3's own tie-break
    // (readyByEffectivePosition, below) is what actually decides final
    // order, never adjacency-list iteration order.
    //
    // Duplicate-edge note: the reads-loop below calls addEdge(writer, i)
    // once per READ USAGE (not once per pass), so the SAME (from, to) pair
    // can legitimately be requested more than once (e.g. a pass with 2+
    // reads resolving to the same earlier writer; the WAW writes-loop below
    // has the identical per-usage shape for 2+ writes sharing the same
    // prior writer). De-duplicated here (see addEdge() below) so the
    // produced lists are a 1:1 mirror of the original matrix's own
    // naturally-idempotent edge set - this is NOT required for correctness
    // given this phase's own predecessors.size()-based in-degree
    // computation (a duplicate would self-cancel: inDegree counted twice,
    // decremented twice back-to-back within the same pop of `from`), but it
    // keeps the lists' size bounded by true edge count and makes them
    // trivially comparable to the old matrix by inspection - see
    // PHASE4_COMPLETION_REPORT.md for the full reasoning.
    std::vector<std::vector<std::int32_t>> successors(static_cast<std::size_t>(passCount));
    std::vector<std::vector<std::int32_t>> predecessors(static_cast<std::size_t>(passCount));

    auto addEdge = [&successors, &predecessors](std::int32_t from, std::int32_t to) {
        if (from < 0 || from == to) {
            // from < 0: no prior writer recorded yet for this resource -
            // nothing to order against. from == to: a pass that reads AND
            // writes the same resource (e.g. a depth attachment) must
            // never gain a self-edge - see this phase's own "self-loop"
            // edge-case test.
            return;
        }
        std::vector<std::int32_t>& succ = successors[static_cast<std::size_t>(from)];
        if (std::find(succ.begin(), succ.end(), to) != succ.end()) {
            return; // Already recorded - matches the original matrix's own natural idempotence.
        }
        succ.push_back(to);
        predecessors[static_cast<std::size_t>(to)].push_back(from);
    };

    std::vector<std::int32_t> lastTextureWriter(input.textureDescs.size(), -1);
    std::vector<std::int32_t> lastBufferWriter(input.bufferDescs.size(), -1);
    // Atmosphere Scattering campaign, Phase 2 - see this file's own
    // pre-implementation precheck (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md,
    // Step 2/3.2): every `usage.kind == ResourceKind::Texture ? ... : ...`
    // two-way branch below was audited and converted to a real,
    // exhaustive, `default:`-less three-way `switch (usage.kind)` BEFORE
    // ResourceKind::VolumeTexture was ever added to the enum, so a future
    // fourth resource kind gets this same compile-time safety net too.
    std::vector<std::int32_t> lastVolumeTextureWriter(input.volumeTextureDescs.size(), -1);

    // render-pass-6 campaign, PHASE4 - fastContradictions is Compile()'s own
    // inline replacement for the standalone DetectRenderPassEventContradictions()
    // call that used to run before this loop even started (see this file's
    // header comment / RenderGraphCompiler.h's own doc comment on Compile()
    // for the full reasoning) - appended to, below, exactly where each
    // read's nearest writer is already being computed for edge-construction
    // purposes, then reported via the same stderr/assert code the old
    // pre-pass used, once this whole loop finishes.
    std::vector<RenderPassEventContradiction> fastContradictions;

    // render-pass-4 campaign, PHASE2 - walk passes in EFFECTIVE order
    // (RenderPassEvent-then-declaration-order), not raw declaration order -
    // `i` is still the pass's ORIGINAL index (used to index successors/
    // predecessors/lastTextureWriter/etc., all still keyed by original
    // index), only the ORDER `i` takes each of its values changes.
    for (std::int32_t effectiveOrderPos = 0; effectiveOrderPos < passCount; ++effectiveOrderPos) {
        const std::int32_t i = effectiveOrder[static_cast<std::size_t>(effectiveOrderPos)];
        const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];

        // RAW: this pass reads whatever the most recently WALKED (in
        // effective order) prior pass wrote to this resource (or nothing,
        // if no prior pass ever wrote it - a "read of a never-written
        // resource" is simply not given an edge; it is not this compiler's
        // job to validate that, see Step 4's "no automatic resource-usage
        // validation").
        for (const ResourceUsage& usage : pass.reads) {
            std::int32_t writer = -1;
            switch (usage.kind) {
            case ResourceKind::Texture:
                if (usage.texture.index < lastTextureWriter.size()) {
                    writer = lastTextureWriter[usage.texture.index];
                }
                break;
            case ResourceKind::Buffer:
                if (usage.buffer.index < lastBufferWriter.size()) {
                    writer = lastBufferWriter[usage.buffer.index];
                }
                break;
            case ResourceKind::VolumeTexture:
                if (usage.volumeTexture.index < lastVolumeTextureWriter.size()) {
                    writer = lastVolumeTextureWriter[usage.volumeTexture.index];
                }
                break;
            }
            addEdge(writer, i);

            // render-pass-6 campaign, PHASE4 - the fast-path contradiction
            // check, folded inline here (see RenderGraphCompiler.h's own
            // updated doc comment on Compile() for why this MUST happen
            // here, reusing `writer`'s exact just-computed value, rather
            // than as a separate pass placed before or after this loop).
            if (writer == -1) {
                std::int32_t laterWriter = -1;
                switch (usage.kind) {
                case ResourceKind::Texture:
                    laterWriter = usage.texture.index < firstTextureWriter.size() ? firstTextureWriter[usage.texture.index] : -1;
                    break;
                case ResourceKind::Buffer:
                    laterWriter = usage.buffer.index < firstBufferWriter.size() ? firstBufferWriter[usage.buffer.index] : -1;
                    break;
                case ResourceKind::VolumeTexture:
                    laterWriter = usage.volumeTexture.index < firstVolumeTextureWriter.size()
                        ? firstVolumeTextureWriter[usage.volumeTexture.index]
                        : -1;
                    break;
                }
                // Self-exclusion guard: a pass that reads AND writes the
                // SAME resource itself (e.g. the "self-loop" depth-
                // attachment shape) can legitimately BE that resource's own
                // firstXWriter entry - must never report a pass as
                // contradicting itself (the standalone function's own
                // forward scan structurally excludes the reader's own
                // position; this reproduces that explicitly).
                if (laterWriter != -1 && laterWriter != i) {
                    RenderPassEventContradiction contradiction;
                    contradiction.kind = RenderPassEventContradictionKind::OrphanReadWithLaterWriter;
                    contradiction.readerPassIndex = i;
                    contradiction.writerPassIndex = laterWriter;
                    contradiction.resourceKind = usage.kind;
                    contradiction.resourceIndex = usage.kind == ResourceKind::Texture ? usage.texture.index
                        : usage.kind == ResourceKind::Buffer                          ? usage.buffer.index
                                                                                        : usage.volumeTexture.index;
                    fastContradictions.push_back(contradiction);
                }
            } else if (input.passes[static_cast<std::size_t>(writer)].renderPassEvent > pass.renderPassEvent) {
                RenderPassEventContradiction contradiction;
                contradiction.kind = RenderPassEventContradictionKind::DeclaredEventOrderDisagreesWithRealDependency;
                contradiction.readerPassIndex = i;
                contradiction.writerPassIndex = writer;
                contradiction.resourceKind = usage.kind;
                contradiction.resourceIndex = usage.kind == ResourceKind::Texture ? usage.texture.index
                    : usage.kind == ResourceKind::Buffer                          ? usage.buffer.index
                                                                                    : usage.volumeTexture.index;
                fastContradictions.push_back(contradiction);
            }
        }

        // WAW: this pass writes a resource some prior pass already wrote -
        // preserve that effective order (see Step 3.2's "multiple writers
        // to the same imported resource" case), then become the new last
        // writer for anything walked after this pass in effective order.
        for (const ResourceUsage& usage : pass.writes) {
            switch (usage.kind) {
            case ResourceKind::Texture:
                if (usage.texture.index < lastTextureWriter.size()) {
                    addEdge(lastTextureWriter[usage.texture.index], i);
                    lastTextureWriter[usage.texture.index] = i;
                }
                break;
            case ResourceKind::Buffer:
                if (usage.buffer.index < lastBufferWriter.size()) {
                    addEdge(lastBufferWriter[usage.buffer.index], i);
                    lastBufferWriter[usage.buffer.index] = i;
                }
                break;
            case ResourceKind::VolumeTexture:
                if (usage.volumeTexture.index < lastVolumeTextureWriter.size()) {
                    addEdge(lastVolumeTextureWriter[usage.volumeTexture.index], i);
                    lastVolumeTextureWriter[usage.volumeTexture.index] = i;
                }
                break;
            }
        }
    }

    // render-pass-6 campaign, PHASE4 - report fastContradictions via the
    // EXACT SAME stderr/assert code the old standalone-function pre-pass
    // used to run BEFORE this loop even started - still strictly before
    // Step 2's culling below, so observable behavior (stderr text, assert
    // firing before culling/reordering ever happens) is unchanged; only
    // WHERE in the function body this now happens changed.
    for (const RenderPassEventContradiction& contradiction : fastContradictions) {
        const PassRecord& reader = input.passes[static_cast<std::size_t>(contradiction.readerPassIndex)];
        const PassRecord& writer = input.passes[static_cast<std::size_t>(contradiction.writerPassIndex)];
        const char* kindText = contradiction.kind == RenderPassEventContradictionKind::OrphanReadWithLaterWriter
            ? "a read resolved to NO prior writer, but a LATER-declared pass writes the same resource - this "
              "read may be silently dropped from the dependency graph and its writer may be silently culled"
            : "a real dependency edge was formed, but the writer's own declared RenderPassEvent is LATER than "
              "the reader's - the two passes' RenderPassEvent tags disagree with their real dependency";
        std::fprintf(stderr,
            "RenderGraphCompiler: RenderPassEvent contradiction detected - reader pass \"%s\" (RenderPassEvent=%s), "
            "writer pass \"%s\" (RenderPassEvent=%s): %s.\n",
            reader.name != nullptr ? reader.name : "<unnamed>", ToString(reader.renderPassEvent),
            writer.name != nullptr ? writer.name : "<unnamed>", ToString(writer.renderPassEvent), kindText);
    }
    assert(fastContradictions.empty()
        && "RenderGraphCompiler: one or more passes' declared RenderPassEvent contradicts a real (or "
           "possibly-missing) resource dependency - see the stderr output immediately above this assert for "
           "exactly which passes and why. See task_manager/render-pass-4/PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md.");

    // --- Step 2: backward reachability from finalOutputs (Step 3.2.2) ---
    //
    // Every pass that writes a `finalOutputs` texture (or a
    // `finalVolumeTextureOutputs` volume texture - see below) is a root;
    // walking backwards along `predecessors` from every root marks every
    // pass that (directly or transitively) contributes to a final output as
    // "kept". Everything never reached is dead code - PassRecord::isCulled
    // gets written `true` for exactly those passes, and none of their
    // declared reads/writes are allowed to extend any resource's lifetime
    // (Step 4 below only ever scans `executionOrder`, which excludes
    // them entirely).
    //
    // UPDATED (Atmosphere Scattering campaign, Phase 6 -
    // ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md): this scan
    // used to be "ALREADY correctly three-way-safe as written" per Phase
    // 2's own precheck - true as far as it went (a Buffer usage could
    // never accidentally be treated as a root), but that same
    // "usage.kind == ResourceKind::Texture is the only branch that marks a
    // pass kept" property was ALSO a genuine correctness GAP, not just a
    // safety guarantee: it meant a VolumeTextureHandle could NEVER be a
    // root either, so a pass whose only write was a VolumeTextureHandle
    // (e.g. this campaign's own Phase 6 aerial-perspective froxel volume)
    // was ALWAYS silently culled, no matter what it declared - discovered
    // directly by this phase's own real workload (see
    // ATMOSPHERE_PHASE6_COMPLETION_REPORT.md). Fixed here by ALSO checking
    // `input.finalVolumeTextureOutputs` (populated via
    // RenderGraphBuilder::KeepVolumeTextureOutput() - see
    // RenderGraphBuilder.h) for a VolumeTexture write - a BufferHandle
    // still can never be a root (matching the existing, deliberate rule
    // that it never could be one either).
    std::vector<bool> kept(static_cast<std::size_t>(passCount), false);
    std::vector<std::int32_t> stack;

    for (std::int32_t i = 0; i < passCount; ++i) {
        const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];
        for (const ResourceUsage& usage : pass.writes) {
            bool isRoot = false;
            switch (usage.kind) {
            case ResourceKind::Texture:
                isRoot = ContainsTextureHandle(finalOutputs, usage.texture);
                break;
            case ResourceKind::Buffer:
                isRoot = false;
                break;
            case ResourceKind::VolumeTexture:
                isRoot = ContainsVolumeTextureHandle(input.finalVolumeTextureOutputs, usage.volumeTexture);
                break;
            }
            if (isRoot) {
                if (!kept[static_cast<std::size_t>(i)]) {
                    kept[static_cast<std::size_t>(i)] = true;
                    stack.push_back(i);
                }
                break;
            }
        }
    }

    // render-pass-6 campaign, PHASE4 (item 2.3) - REPLACES the O(P^2) matrix
    // column scan (`for predecessor in [0, passCount) check
    // edgeExists[predecessor][node]`) with a direct walk of
    // `predecessors[node]` - visits each edge at most once overall (standard
    // DFS/BFS reachability), O(P + E) total instead of O(P^2).
    while (!stack.empty()) {
        const std::int32_t node = stack.back();
        stack.pop_back();
        for (const std::int32_t predecessor : predecessors[static_cast<std::size_t>(node)]) {
            if (!kept[static_cast<std::size_t>(predecessor)]) {
                kept[static_cast<std::size_t>(predecessor)] = true;
                stack.push_back(predecessor);
            }
        }
    }

    for (std::int32_t i = 0; i < passCount; ++i) {
        input.passes[static_cast<std::size_t>(i)].isCulled = !kept[static_cast<std::size_t>(i)];
    }

    // --- Step 3: topological sort of the kept passes (Step 3.2.3) -------
    //
    // Kahn's algorithm, restricted to the kept subgraph. Ties in the
    // zero-in-degree "ready" set are broken by lowest EFFECTIVE POSITION
    // (RenderPassEvent-then-declaration-index - render-pass-4 campaign,
    // PHASE2, REPLACES the old "lowest original declaration index" rule) -
    // a std::set stays ordered ascending with no hashing involved
    // whatsoever, so pulling out the smallest ready EFFECTIVE POSITION
    // every iteration is both cheap and, critically, fully deterministic
    // run after run. render-pass-6 campaign, PHASE4 (item 2.3) - in-degree
    // is now computed directly from `predecessors[to].size()` (restricted
    // to KEPT predecessors) instead of a full O(P^2) matrix double loop,
    // and the main loop's successor scan below now walks
    // `successors[node]` directly instead of a full matrix row scan - both
    // O(P + E) instead of O(P^2). This determinism-critical tie-break rule
    // itself is COMPLETELY UNCHANGED by this rewrite - it does not depend
    // on the adjacency storage shape at all, only on which nodes become
    // ready and when, which is identical either way (see
    // PHASE4_COMPLETION_REPORT.md for the full reasoning on why duplicate
    // edges, de-duplicated or not, cannot change this).
    std::vector<std::int32_t> inDegree(static_cast<std::size_t>(passCount), 0);
    for (std::int32_t to = 0; to < passCount; ++to) {
        if (!kept[static_cast<std::size_t>(to)]) {
            continue;
        }
        for (const std::int32_t from : predecessors[static_cast<std::size_t>(to)]) {
            if (kept[static_cast<std::size_t>(from)]) {
                ++inDegree[static_cast<std::size_t>(to)];
            }
        }
    }

    // render-pass-4 campaign, PHASE2 - `readyByEffectivePosition` stores
    // EFFECTIVE POSITIONS (indices into effectiveOrder), NOT raw pass
    // indices - mapped back to the real pass index (`effectiveOrder[...]`)
    // only when actually popped/pushed to `order` below.
    std::set<std::int32_t> readyByEffectivePosition;
    for (std::int32_t i = 0; i < passCount; ++i) {
        if (kept[static_cast<std::size_t>(i)] && inDegree[static_cast<std::size_t>(i)] == 0) {
            readyByEffectivePosition.insert(effectivePosition[static_cast<std::size_t>(i)]);
        }
    }

    std::vector<std::int32_t> order;
    order.reserve(static_cast<std::size_t>(passCount));

    while (!readyByEffectivePosition.empty()) {
        const std::int32_t nodePosition = *readyByEffectivePosition.begin();
        readyByEffectivePosition.erase(readyByEffectivePosition.begin());
        const std::int32_t node = effectiveOrder[static_cast<std::size_t>(nodePosition)];
        order.push_back(node);

        for (const std::int32_t successor : successors[static_cast<std::size_t>(node)]) {
            if (kept[static_cast<std::size_t>(successor)]) {
                if (--inDegree[static_cast<std::size_t>(successor)] == 0) {
                    readyByEffectivePosition.insert(effectivePosition[static_cast<std::size_t>(successor)]);
                }
            }
        }
    }

    std::size_t keptCount = 0;
    for (const bool isKept : kept) {
        if (isKept) {
            ++keptCount;
        }
    }

    if (order.size() != keptCount) {
        // See this file's own header comment above for why this is
        // structurally unreachable through any graph declared via
        // RenderGraphBuilder today - kept here as genuinely correct
        // defensive code, per this phase's own strategy document.
        throw std::runtime_error(
            "RenderGraphCompiler::Compile: cycle detected among the graph's declared passes - "
            "a pass's reads/writes form a dependency cycle that cannot be topologically ordered");
    }

    result.executionOrder.reserve(order.size());
    for (const std::int32_t passIndex : order) {
        result.executionOrder.push_back(PassHandle{ static_cast<std::uint32_t>(passIndex), 1 });
    }

    // --- Step 4: resource lifetimes (Step 3.2.4) -------------------------
    //
    // Falls out of `order` almost for free: for each kept resource, scan
    // `order` once (in EXECUTION order, not declaration order), recording
    // the first/last executionOrder POSITION at which any surviving pass
    // reads or writes it. Both reads and writes count for
    // `lastUsePassIndex` - a resource's last WRITE with no subsequent
    // read is still kept alive through that write's own pass (see this
    // header's own comment on ResourceLifetime).
    for (std::size_t pos = 0; pos < order.size(); ++pos) {
        const PassRecord& pass = input.passes[static_cast<std::size_t>(order[pos])];

        auto touch = [&](const ResourceUsage& usage) {
            std::vector<ResourceLifetime>* lifetimes = nullptr;
            std::uint32_t index = 0;
            switch (usage.kind) {
            case ResourceKind::Texture:
                lifetimes = &result.textureLifetimes;
                index = usage.texture.index;
                break;
            case ResourceKind::Buffer:
                lifetimes = &result.bufferLifetimes;
                index = usage.buffer.index;
                break;
            case ResourceKind::VolumeTexture:
                lifetimes = &result.volumeTextureLifetimes;
                index = usage.volumeTexture.index;
                break;
            }
            if (lifetimes == nullptr || index >= lifetimes->size()) {
                return;
            }
            ResourceLifetime& lifetime = (*lifetimes)[index];
            if (lifetime.firstUsePassIndex == -1) {
                lifetime.firstUsePassIndex = static_cast<std::int32_t>(pos);
            }
            lifetime.lastUsePassIndex = static_cast<std::int32_t>(pos);
        };

        for (const ResourceUsage& usage : pass.reads) {
            touch(usage);
        }
        for (const ResourceUsage& usage : pass.writes) {
            touch(usage);
        }
    }

    return result;
}

} // namespace gte::rg
