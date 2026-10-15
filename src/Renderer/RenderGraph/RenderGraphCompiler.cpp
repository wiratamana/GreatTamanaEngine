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

bool IsPersistentTexture(std::span<const PersistentTextureUse> uses, const TextureHandle& handle)
{
    for (const PersistentTextureUse& use : uses) {
        if (use.handle == handle) {
            return true;
        }
    }
    return false;
}

bool ContainsBufferHandle(std::span<const BufferHandle> handles, const BufferHandle& handle)
{
    for (const BufferHandle& candidate : handles) {
        if (candidate == handle) {
            return true;
        }
    }
    return false;
}

} // namespace

// Finds reads whose writer is declared in a later RenderPassEvent, or only after the read.
// Self-contained: never prints, asserts, throws, or mutates `input`.
std::vector<RenderPassEventContradiction> DetectRenderPassEventContradictions(
    const CompiledGraphInput& input, std::span<const std::int32_t> processingOrder)
{
    std::vector<RenderPassEventContradiction> contradictions;

    auto sameResource = [](const ResourceUsage& a, const ResourceUsage& b) {
        if (a.kind != b.kind) {
            return false;
        }
        return DispatchByKind(a,
            [&](TextureHandle) { return a.texture == b.texture; },
            [&](BufferHandle) { return a.buffer == b.buffer; });
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
                // No earlier writer: look for a later one.
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
                            contradiction.resourceIndex = DispatchByKind(read,
                                [](TextureHandle h) { return h.index; },
                                [](BufferHandle h) { return h.index; });
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
                    contradiction.resourceIndex = DispatchByKind(read,
                        [](TextureHandle h) { return h.index; },
                        [](BufferHandle h) { return h.index; });
                    contradictions.push_back(contradiction);
                }
            }
        }
    }

    return contradictions;
}

// Builds the dependency graph, culls passes that feed no output, and orders the rest.
// Edges only point forward, so the cycle throw below is a defensive check.
CompiledGraph Compile(CompiledGraphInput& input, std::span<const TextureHandle> finalOutputs)
{
    const std::int32_t passCount = static_cast<std::int32_t>(input.passes.size());

    CompiledGraph result;
    result.textureLifetimes.assign(input.textures.size(), ResourceLifetime{});
    result.bufferLifetimes.assign(input.buffers.size(), ResourceLifetime{});

    if (passCount == 0) {
        return result;
    }

    // Passes in original-index order, stable-sorted by RenderPassEvent. Edge building and the
    // topological tie-break both use this order.
    std::vector<std::int32_t> effectiveOrder(static_cast<std::size_t>(passCount));
    for (std::int32_t i = 0; i < passCount; ++i) {
        effectiveOrder[static_cast<std::size_t>(i)] = i;
    }
    std::stable_sort(effectiveOrder.begin(), effectiveOrder.end(), [&input](std::int32_t a, std::int32_t b) {
        return input.passes[static_cast<std::size_t>(a)].renderPassEvent
            < input.passes[static_cast<std::size_t>(b)].renderPassEvent;
    });

    // Inverse permutation: effectivePosition[originalIndex] = position in effectiveOrder.
    std::vector<std::int32_t> effectivePosition(static_cast<std::size_t>(passCount), -1);
    for (std::size_t pos = 0; pos < effectiveOrder.size(); ++pos) {
        effectivePosition[static_cast<std::size_t>(effectiveOrder[pos])] = static_cast<std::int32_t>(pos);
    }

    // firstXWriter[resource] = original index of the first pass (effective order) writing it, or -1.
    // Lets a read with no earlier writer detect a later one without a second walk.
    std::vector<std::int32_t> firstTextureWriter(input.textures.size(), -1);
    std::vector<std::int32_t> firstBufferWriter(input.buffers.size(), -1);
    // Resolves the firstXWriter slot for a usage; nullptr when the handle is out of range.
    auto firstWriterSlotFor = [&](const ResourceUsage& usage) -> std::int32_t* {
        return DispatchByKind(usage,
            [&](TextureHandle h) -> std::int32_t* {
                return h.index < firstTextureWriter.size() ? &firstTextureWriter[h.index] : nullptr;
            },
            [&](BufferHandle h) -> std::int32_t* {
                return h.index < firstBufferWriter.size() ? &firstBufferWriter[h.index] : nullptr;
            });
    };
    for (std::int32_t pos = 0; pos < passCount; ++pos) {
        const std::int32_t i = effectiveOrder[static_cast<std::size_t>(pos)];
        for (const ResourceUsage& usage : input.passes[static_cast<std::size_t>(i)].writes) {
            std::int32_t* slot = firstWriterSlotFor(usage);
            if (slot != nullptr && *slot == -1) {
                *slot = i;
            }
        }
    }

    // Dependency graph as adjacency lists: successors[from] runs after `from`, predecessors[to] before `to`.
    // addEdge() de-duplicates, since one pass may read or write the same writer's resource several times.
    std::vector<std::vector<std::int32_t>> successors(static_cast<std::size_t>(passCount));
    std::vector<std::vector<std::int32_t>> predecessors(static_cast<std::size_t>(passCount));

    auto addEdge = [&successors, &predecessors](std::int32_t from, std::int32_t to) {
        if (from < 0 || from == to) {
            // No prior writer, or a pass reading and writing the same resource: no edge.
            return;
        }
        std::vector<std::int32_t>& succ = successors[static_cast<std::size_t>(from)];
        if (std::find(succ.begin(), succ.end(), to) != succ.end()) {
            return; // Edge already recorded.
        }
        succ.push_back(to);
        predecessors[static_cast<std::size_t>(to)].push_back(from);
    };

    std::vector<std::int32_t> lastTextureWriter(input.textures.size(), -1);
    std::vector<std::int32_t> lastBufferWriter(input.buffers.size(), -1);

    // Resolves the lastXWriter slot for a usage; nullptr when the handle is out of range.
    auto lastWriterSlotFor = [&](const ResourceUsage& usage) -> std::int32_t* {
        return DispatchByKind(usage,
            [&](TextureHandle h) -> std::int32_t* {
                return h.index < lastTextureWriter.size() ? &lastTextureWriter[h.index] : nullptr;
            },
            [&](BufferHandle h) -> std::int32_t* {
                return h.index < lastBufferWriter.size() ? &lastBufferWriter[h.index] : nullptr;
            });
    };

    // Collected while walking reads; reported once the walk finishes.
    std::vector<RenderPassEventContradiction> fastContradictions;

    // Walk in effective order; `i` is still the original pass index.
    for (std::int32_t effectiveOrderPos = 0; effectiveOrderPos < passCount; ++effectiveOrderPos) {
        const std::int32_t i = effectiveOrder[static_cast<std::size_t>(effectiveOrderPos)];
        const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];

        // RAW: a read depends on the most recently walked writer. A read of a never-written
        // resource gets no edge.
        for (const ResourceUsage& usage : pass.reads) {
            const std::int32_t* writerSlot = lastWriterSlotFor(usage);
            const std::int32_t writer = writerSlot != nullptr ? *writerSlot : -1;
            addEdge(writer, i);

            // Contradiction check: reuses the nearest writer just computed for the edge.
            if (writer == -1) {
                const std::int32_t* laterWriterSlot = firstWriterSlotFor(usage);
                const std::int32_t laterWriter = laterWriterSlot != nullptr ? *laterWriterSlot : -1;
                // A pass that reads and writes the same resource may be its own first writer: never report that.
                if (laterWriter != -1 && laterWriter != i) {
                    RenderPassEventContradiction contradiction;
                    contradiction.kind = RenderPassEventContradictionKind::OrphanReadWithLaterWriter;
                    contradiction.readerPassIndex = i;
                    contradiction.writerPassIndex = laterWriter;
                    contradiction.resourceKind = usage.kind;
                    contradiction.resourceIndex = DispatchByKind(usage,
                        [](TextureHandle h) { return h.index; },
                        [](BufferHandle h) { return h.index; });
                    fastContradictions.push_back(contradiction);
                }
            } else if (input.passes[static_cast<std::size_t>(writer)].renderPassEvent > pass.renderPassEvent) {
                RenderPassEventContradiction contradiction;
                contradiction.kind = RenderPassEventContradictionKind::DeclaredEventOrderDisagreesWithRealDependency;
                contradiction.readerPassIndex = i;
                contradiction.writerPassIndex = writer;
                contradiction.resourceKind = usage.kind;
                contradiction.resourceIndex = DispatchByKind(usage,
                    [](TextureHandle h) { return h.index; },
                    [](BufferHandle h) { return h.index; });
                fastContradictions.push_back(contradiction);
            }
        }

        // WAW: keep effective order between writers of one resource, then become its last writer.
        // An out-of-range slot gets no edge and no write.
        for (const ResourceUsage& usage : pass.writes) {
            std::int32_t* slot = lastWriterSlotFor(usage);
            if (slot != nullptr) {
                addEdge(*slot, i);
                *slot = i;
            }
        }
    }

    // Report contradictions before culling so the assert fires on the original graph.
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
        && "RenderGraphCompiler: a pass's declared RenderPassEvent contradicts a real (or possibly-missing) "
           "resource dependency - see the stderr output above for which passes and why.");

    // Backward reachability from the root writes (final outputs, kept outputs, persistent textures).
    // Passes never reached are culled and extend no resource lifetime.
    std::vector<bool> kept(static_cast<std::size_t>(passCount), false);
    std::vector<std::int32_t> stack;

    for (std::int32_t i = 0; i < passCount; ++i) {
        const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];
        for (const ResourceUsage& usage : pass.writes) {
            // A write is a root when its resource was explicitly kept (any texture dimension, or a buffer)
            // or is a persistent-cache texture; a buffer is culled unless kept or read by something kept.
            const bool isRoot = DispatchByKind(usage,
                [&](TextureHandle h) {
                    return ContainsTextureHandle(finalOutputs, h)
                        || ContainsTextureHandle(input.finalTextureOutputs, h)
                        || IsPersistentTexture(input.persistentCacheTextures, h);
                },
                [&](BufferHandle h) { return ContainsBufferHandle(input.finalBufferOutputs, h); });
            if (isRoot) {
                if (!kept[static_cast<std::size_t>(i)]) {
                    kept[static_cast<std::size_t>(i)] = true;
                    stack.push_back(i);
                }
                break;
            }
        }
    }

    // Walk predecessors once per node: O(P + E).
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

    // Kahn's algorithm on the kept subgraph. Ties go to the lowest effective position,
    // so the order is deterministic.
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

    // Holds effective positions (indices into effectiveOrder), not pass indices.
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
        // Unreachable for graphs built via RenderGraphBuilder; kept as a defensive check.
        throw std::runtime_error(
            "RenderGraphCompiler::Compile: cycle detected among the graph's declared passes - "
            "a pass's reads/writes form a dependency cycle that cannot be topologically ordered");
    }

    result.executionOrder.reserve(order.size());
    for (const std::int32_t passIndex : order) {
        result.executionOrder.push_back(PassHandle{ static_cast<std::uint32_t>(passIndex), 1 });
    }

    // Resource lifetimes: scan `order` once; both reads and writes extend a resource's last use.
    for (std::size_t pos = 0; pos < order.size(); ++pos) {
        const PassRecord& pass = input.passes[static_cast<std::size_t>(order[pos])];

        // Resolves which lifetime table and index a usage touches, in one pointer.
        auto touch = [&](const ResourceUsage& usage) {
            ResourceLifetime* lifetime = DispatchByKind(usage,
                [&](TextureHandle h) -> ResourceLifetime* {
                    return h.index < result.textureLifetimes.size() ? &result.textureLifetimes[h.index] : nullptr;
                },
                [&](BufferHandle h) -> ResourceLifetime* {
                    return h.index < result.bufferLifetimes.size() ? &result.bufferLifetimes[h.index] : nullptr;
                });
            if (lifetime == nullptr) {
                return;
            }
            if (lifetime->firstUsePassIndex == -1) {
                lifetime->firstUsePassIndex = static_cast<std::int32_t>(pos);
            }
            lifetime->lastUsePassIndex = static_cast<std::int32_t>(pos);
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
