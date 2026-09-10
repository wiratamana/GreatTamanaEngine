#include "RenderGraphCompiler.h"

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

// Implementation note on cycle detection (Step 3.2.3's Kahn's-algorithm
// "naturally detects a cycle" requirement): the edge-construction rule
// below (Step 3.2.1 - "the most recent pass, among those declared so far,
// that wrote it") only ever adds an edge from a STRICTLY LOWER declaration
// index to a STRICTLY HIGHER one (a pass can only depend on a writer that
// was already declared before it; there is no mechanism here for an
// earlier-declared pass to depend on a later-declared one). This makes
// the produced graph a DAG BY CONSTRUCTION, with the passes' own
// declaration order already being one valid topological order - a real
// cycle (as sketched in this phase's own strategy document's Step 3.4,
// "pass A reads what B writes AND writes what B reads") is therefore
// structurally UNREACHABLE through any graph an author can actually
// declare via RenderGraphBuilder/AddPass(). The throw below is kept
// anyway, as genuinely correct defensive code (matching the strategy
// document's explicit request for Kahn's algorithm's own natural cycle
// detection) in case a future change to the edge-construction rule above
// (e.g. a WAR/read-then-later-write hazard edge) ever makes a real cycle
// possible - see RENDERGRAPH_PHASE3_COMPLETION_REPORT.md for the full
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

    // --- Step 1: build the dependency graph (Step 3.2.1) --------------
    //
    // edgeExists[from][to] == true means "from must execute before to".
    // A plain vector-of-vectors (never a hash-keyed container) - see
    // AGENTS.md's "no hashing on the hot path" philosophy and this
    // phase's own Step 3.3 determinism requirement. Pass counts in this
    // engine are single digits today (three hardcoded passes) and are
    // not expected to grow into the thousands, so an O(passCount^2)
    // adjacency matrix is the right, simplest tool here.
    std::vector<std::vector<bool>> edgeExists(
        static_cast<std::size_t>(passCount), std::vector<bool>(static_cast<std::size_t>(passCount), false));

    auto addEdge = [&edgeExists](std::int32_t from, std::int32_t to) {
        if (from < 0 || from == to) {
            // from < 0: no prior writer recorded yet for this resource -
            // nothing to order against. from == to: a pass that reads AND
            // writes the same resource (e.g. a depth attachment) must
            // never gain a self-edge - see this phase's own "self-loop"
            // edge-case test.
            return;
        }
        edgeExists[static_cast<std::size_t>(from)][static_cast<std::size_t>(to)] = true;
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

    for (std::int32_t i = 0; i < passCount; ++i) {
        const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];

        // RAW: this pass reads whatever the most recently declared prior
        // pass wrote to this resource (or nothing, if no prior pass ever
        // wrote it - a "read of a never-written resource" is simply not
        // given an edge; it is not this compiler's job to validate that,
        // see Step 4's "no automatic resource-usage validation").
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
        }

        // WAW: this pass writes a resource some prior pass already wrote -
        // preserve that declaration order (see Step 3.2's "multiple
        // writers to the same imported resource" case), then become the
        // new last writer for anything declared after this pass.
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

    // --- Step 2: backward reachability from finalOutputs (Step 3.2.2) ---
    //
    // Every pass that writes a `finalOutputs` texture (or a
    // `finalVolumeTextureOutputs` volume texture - see below) is a root;
    // walking backwards along edgeExists from every root marks every pass
    // that (directly or transitively) contributes to a final output as
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

    while (!stack.empty()) {
        const std::int32_t node = stack.back();
        stack.pop_back();
        for (std::int32_t predecessor = 0; predecessor < passCount; ++predecessor) {
            if (edgeExists[static_cast<std::size_t>(predecessor)][static_cast<std::size_t>(node)] &&
                !kept[static_cast<std::size_t>(predecessor)]) {
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
    // zero-in-degree "ready" set are broken by lowest original
    // declaration index (Step 3.3's determinism requirement) - a
    // std::set stays ordered ascending with no hashing involved
    // whatsoever, so pulling out the smallest ready index every
    // iteration is both cheap (pass counts are tiny) and, critically,
    // fully deterministic run after run.
    std::vector<std::int32_t> inDegree(static_cast<std::size_t>(passCount), 0);
    for (std::int32_t to = 0; to < passCount; ++to) {
        if (!kept[static_cast<std::size_t>(to)]) {
            continue;
        }
        for (std::int32_t from = 0; from < passCount; ++from) {
            if (kept[static_cast<std::size_t>(from)] &&
                edgeExists[static_cast<std::size_t>(from)][static_cast<std::size_t>(to)]) {
                ++inDegree[static_cast<std::size_t>(to)];
            }
        }
    }

    std::set<std::int32_t> ready;
    for (std::int32_t i = 0; i < passCount; ++i) {
        if (kept[static_cast<std::size_t>(i)] && inDegree[static_cast<std::size_t>(i)] == 0) {
            ready.insert(i);
        }
    }

    std::vector<std::int32_t> order;
    order.reserve(static_cast<std::size_t>(passCount));

    while (!ready.empty()) {
        const std::int32_t node = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(node);

        for (std::int32_t successor = 0; successor < passCount; ++successor) {
            if (kept[static_cast<std::size_t>(successor)] &&
                edgeExists[static_cast<std::size_t>(node)][static_cast<std::size_t>(successor)]) {
                if (--inDegree[static_cast<std::size_t>(successor)] == 0) {
                    ready.insert(successor);
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
