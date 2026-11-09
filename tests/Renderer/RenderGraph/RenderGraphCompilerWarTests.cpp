// Tests for write-after-read ordering in Compile(): a reader finishes before the next writer,
// culling is untouched and the result is deterministic. All GPU-free.

#include "Renderer/RenderGraph/RenderGraphCompiler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace gte::rg {
namespace {

void NoOpExecute(PassContext&) { }

bool ExecutionOrderEquals(const std::vector<PassHandle>& order, const std::vector<std::uint32_t>& expectedIndices)
{
    if (order.size() != expectedIndices.size()) {
        return false;
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i].index != expectedIndices[i]) {
            return false;
        }
    }
    return true;
}

std::vector<std::int32_t> ToIndices(const std::vector<PassHandle>& order)
{
    std::vector<std::int32_t> indices;
    indices.reserve(order.size());
    for (const PassHandle& handle : order) {
        indices.push_back(static_cast<std::int32_t>(handle.index));
    }
    return indices;
}

// --- Hand-built graphs --------------------------------------------------------

// Fills CompiledGraphInput directly: only reads, writes and the event matter to Compile().
struct TestGraph {
    CompiledGraphInput input;

    TextureHandle Tex()
    {
        input.textures.emplace_back();
        return TextureHandle{ static_cast<std::uint32_t>(input.textures.size() - 1), 1 };
    }

    BufferHandle Buf()
    {
        input.buffers.emplace_back();
        return BufferHandle{ static_cast<std::uint32_t>(input.buffers.size() - 1), 1 };
    }

    // Returns the declaration index of the new pass.
    std::int32_t Pass(RenderPassEvent event, std::initializer_list<ResourceUsage> reads,
        std::initializer_list<ResourceUsage> writes)
    {
        PassRecord pass;
        pass.name = "P";
        pass.renderPassEvent = event;
        pass.reads.assign(reads.begin(), reads.end());
        pass.writes.assign(writes.begin(), writes.end());
        input.passes.push_back(std::move(pass));
        return static_cast<std::int32_t>(input.passes.size() - 1);
    }

    std::int32_t Pass(std::initializer_list<ResourceUsage> reads, std::initializer_list<ResourceUsage> writes)
    {
        return Pass(RenderPassEvent::Opaques, reads, writes);
    }

    void KeepBuffer(BufferHandle buffer) { input.finalBufferOutputs.push_back(buffer); }
    void KeepTexture(TextureHandle texture) { input.finalTextureOutputs.push_back(texture); }
};

ResourceUsage Rd(BufferHandle b) { return ResourceUsage::ForBuffer(b, ResourceAccess::ComputeShaderRead); }
ResourceUsage Wr(BufferHandle b) { return ResourceUsage::ForBuffer(b, ResourceAccess::ComputeShaderWrite); }
ResourceUsage Rd(TextureHandle t) { return ResourceUsage::ForTexture(t, ResourceAccess::ShaderRead); }
ResourceUsage Wr(TextureHandle t) { return ResourceUsage::ForTexture(t, ResourceAccess::ColorAttachmentWrite); }

// --- Hazard oracle ------------------------------------------------------------

enum class HazardKind : std::uint8_t {
    ReadAfterWrite,
    WriteAfterWrite,
    WriteAfterRead,
    ReaderLostItsWriter,
};

struct Hazard {
    HazardKind kind = HazardKind::ReadAfterWrite;
    bool isBuffer = false;
    std::uint32_t resource = 0;
    std::int32_t first = -1;
    std::int32_t second = -1;
};

// Declaration indices stable-sorted by event: the program order the compiler defines.
std::vector<std::int32_t> EffectiveOrderOf(const CompiledGraphInput& input)
{
    std::vector<std::int32_t> order(input.passes.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<std::int32_t>(i);
    }
    std::stable_sort(order.begin(), order.end(), [&input](std::int32_t a, std::int32_t b) {
        return input.passes[static_cast<std::size_t>(a)].renderPassEvent
            < input.passes[static_cast<std::size_t>(b)].renderPassEvent;
    });
    return order;
}

// Checks `executed` (declaration indices; passes missing from it are culled) against program order:
// every conflicting pair of executed passes must keep its program order.
std::vector<Hazard> FindHazards(const CompiledGraphInput& input, const std::vector<std::int32_t>& executed)
{
    std::vector<std::int32_t> position(input.passes.size(), -1);
    for (std::size_t pos = 0; pos < executed.size(); ++pos) {
        position[static_cast<std::size_t>(executed[pos])] = static_cast<std::int32_t>(pos);
    }

    struct Track {
        std::int32_t lastWriter = -1;
        std::vector<std::int32_t> readers;
    };
    std::vector<Track> textures(input.textures.size());
    std::vector<Track> buffers(input.buffers.size());
    std::vector<Hazard> hazards;

    const auto check = [&](HazardKind kind, bool isBuffer, std::uint32_t resource, std::int32_t first,
                           std::int32_t second) {
        const std::int32_t firstPos = position[static_cast<std::size_t>(first)];
        const std::int32_t secondPos = position[static_cast<std::size_t>(second)];
        const bool lostWriter = kind == HazardKind::ReadAfterWrite && secondPos >= 0 && firstPos < 0;
        const bool misordered = firstPos >= 0 && secondPos >= 0 && firstPos > secondPos;
        if (lostWriter || misordered) {
            hazards.push_back(Hazard{ lostWriter ? HazardKind::ReaderLostItsWriter : kind, isBuffer, resource, first,
                second });
        }
    };

    const auto trackOf = [&](const ResourceUsage& usage, bool& isBuffer, std::uint32_t& index) -> Track* {
        isBuffer = usage.kind == ResourceKind::Buffer;
        index = isBuffer ? usage.buffer.index : usage.texture.index;
        std::vector<Track>& list = isBuffer ? buffers : textures;
        return index < list.size() ? &list[index] : nullptr;
    };

    for (const std::int32_t i : EffectiveOrderOf(input)) {
        const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];
        for (const ResourceUsage& usage : pass.reads) {
            bool isBuffer = false;
            std::uint32_t index = 0;
            Track* track = trackOf(usage, isBuffer, index);
            if (track == nullptr) {
                continue;
            }
            if (track->lastWriter >= 0 && track->lastWriter != i) {
                check(HazardKind::ReadAfterWrite, isBuffer, index, track->lastWriter, i);
            }
            if (track->readers.empty() || track->readers.back() != i) {
                track->readers.push_back(i);
            }
        }
        for (const ResourceUsage& usage : pass.writes) {
            bool isBuffer = false;
            std::uint32_t index = 0;
            Track* track = trackOf(usage, isBuffer, index);
            if (track == nullptr) {
                continue;
            }
            if (track->lastWriter >= 0 && track->lastWriter != i) {
                check(HazardKind::WriteAfterWrite, isBuffer, index, track->lastWriter, i);
            }
            for (const std::int32_t reader : track->readers) {
                if (reader != i) {
                    check(HazardKind::WriteAfterRead, isBuffer, index, reader, i);
                }
            }
            track->readers.clear();
            track->lastWriter = i;
        }
    }
    return hazards;
}

bool HasHazard(const std::vector<Hazard>& hazards, HazardKind kind, std::int32_t first, std::int32_t second)
{
    return std::any_of(hazards.begin(), hazards.end(), [&](const Hazard& h) {
        return h.kind == kind && h.first == first && h.second == second;
    });
}

// Compiles `graph` and returns the executed declaration indices.
std::vector<std::int32_t> CompileAndGetOrder(TestGraph& graph, std::span<const TextureHandle> finalOutputs = {})
{
    return ToIndices(Compile(graph.input, finalOutputs).executionOrder);
}

// --- Reference model of the dependency-only compiler (RAW and WAW, no ordering edges) ---

std::vector<bool> ReferenceKept(const CompiledGraphInput& input, std::span<const TextureHandle> finalOutputs)
{
    const std::size_t passCount = input.passes.size();
    std::vector<std::vector<std::int32_t>> predecessors(passCount);
    std::vector<std::int32_t> lastTextureWriter(input.textures.size(), -1);
    std::vector<std::int32_t> lastBufferWriter(input.buffers.size(), -1);

    const auto slotOf = [&](const ResourceUsage& usage) -> std::int32_t* {
        if (usage.kind == ResourceKind::Buffer) {
            return usage.buffer.index < lastBufferWriter.size() ? &lastBufferWriter[usage.buffer.index] : nullptr;
        }
        return usage.texture.index < lastTextureWriter.size() ? &lastTextureWriter[usage.texture.index] : nullptr;
    };

    for (const std::int32_t i : EffectiveOrderOf(input)) {
        const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];
        for (const ResourceUsage& usage : pass.reads) {
            const std::int32_t* slot = slotOf(usage);
            if (slot != nullptr && *slot >= 0 && *slot != i) {
                predecessors[static_cast<std::size_t>(i)].push_back(*slot);
            }
        }
        for (const ResourceUsage& usage : pass.writes) {
            std::int32_t* slot = slotOf(usage);
            if (slot != nullptr) {
                if (*slot >= 0 && *slot != i) {
                    predecessors[static_cast<std::size_t>(i)].push_back(*slot);
                }
                *slot = i;
            }
        }
    }

    const auto isRoot = [&](const ResourceUsage& usage) {
        if (usage.kind == ResourceKind::Buffer) {
            return std::find(input.finalBufferOutputs.begin(), input.finalBufferOutputs.end(), usage.buffer)
                != input.finalBufferOutputs.end();
        }
        const auto has = [&](std::span<const TextureHandle> list) {
            return std::find(list.begin(), list.end(), usage.texture) != list.end();
        };
        if (has(finalOutputs) || has(input.finalTextureOutputs)) {
            return true;
        }
        return std::any_of(input.persistentCacheTextures.begin(), input.persistentCacheTextures.end(),
            [&](const PersistentTextureUse& use) { return use.handle == usage.texture; });
    };

    std::vector<bool> kept(passCount, false);
    std::vector<std::int32_t> stack;
    for (std::size_t i = 0; i < passCount; ++i) {
        for (const ResourceUsage& usage : input.passes[i].writes) {
            if (isRoot(usage)) {
                kept[i] = true;
                stack.push_back(static_cast<std::int32_t>(i));
                break;
            }
        }
    }
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
    return kept;
}

// Kept passes in program order: the order the compiler promises.
std::vector<std::int32_t> ExpectedOrder(const CompiledGraphInput& input, const std::vector<bool>& kept)
{
    std::vector<std::int32_t> order;
    for (const std::int32_t i : EffectiveOrderOf(input)) {
        if (kept[static_cast<std::size_t>(i)]) {
            order.push_back(i);
        }
    }
    return order;
}

std::vector<ResourceLifetime> ExpectedLifetimes(const CompiledGraphInput& input,
    const std::vector<std::int32_t>& order, bool buffers)
{
    std::vector<ResourceLifetime> lifetimes(buffers ? input.buffers.size() : input.textures.size());
    for (std::size_t pos = 0; pos < order.size(); ++pos) {
        const PassRecord& pass = input.passes[static_cast<std::size_t>(order[pos])];
        const auto touch = [&](const ResourceUsage& usage) {
            if ((usage.kind == ResourceKind::Buffer) != buffers) {
                return;
            }
            const std::uint32_t index = buffers ? usage.buffer.index : usage.texture.index;
            ResourceLifetime& lifetime = lifetimes[index];
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
    return lifetimes;
}

// --- Random graph generator -----------------------------------------------------

struct RandomGraphShape {
    std::uint32_t minPasses = 2;
    std::uint32_t maxPasses = 40;
    std::uint32_t maxTextures = 8;
    std::uint32_t maxBuffers = 8;
};

struct RandomGraph {
    CompiledGraphInput input;
    std::vector<TextureHandle> finalOutputs;
};

// Reads only touch resources already written earlier in program order (or never written at all),
// so no read is an orphan. Declaration order interleaves the event tiers at random.
RandomGraph MakeRandomGraph(std::uint32_t seed, const RandomGraphShape& shape = {})
{
    constexpr RenderPassEvent kEvents[] = { RenderPassEvent::BeforeEverything, RenderPassEvent::PreOpaques,
        RenderPassEvent::Opaques, RenderPassEvent::AfterOpaques, RenderPassEvent::Transparents,
        RenderPassEvent::AfterTransparents, RenderPassEvent::AfterEverything };
    constexpr std::uint32_t kEventCount = static_cast<std::uint32_t>(std::size(kEvents));

    std::mt19937 rng(seed);
    const auto pick = [&rng](std::uint32_t lo, std::uint32_t hi) { return lo + static_cast<std::uint32_t>(rng() % (hi - lo + 1)); };

    RandomGraph graph;
    CompiledGraphInput& input = graph.input;
    const std::uint32_t passCount = pick(shape.minPasses, shape.maxPasses);
    input.textures.resize(pick(1, shape.maxTextures));
    input.buffers.resize(pick(1, shape.maxBuffers));

    std::vector<bool> textureNeverWritten(input.textures.size());
    std::vector<bool> bufferNeverWritten(input.buffers.size());
    for (std::size_t i = 0; i < textureNeverWritten.size(); ++i) {
        textureNeverWritten[i] = pick(0, 3) == 0;
    }
    for (std::size_t i = 0; i < bufferNeverWritten.size(); ++i) {
        bufferNeverWritten[i] = pick(0, 3) == 0;
    }
    std::vector<bool> textureWritten(input.textures.size(), false);
    std::vector<bool> bufferWritten(input.buffers.size(), false);

    // Program order: events sorted ascending, then reads and writes chosen walking that order.
    std::vector<std::uint32_t> eventOf(passCount);
    for (std::uint32_t& e : eventOf) {
        e = pick(0, kEventCount - 1);
    }
    std::sort(eventOf.begin(), eventOf.end());

    std::vector<PassRecord> program(passCount);
    for (std::uint32_t p = 0; p < passCount; ++p) {
        PassRecord& pass = program[p];
        pass.name = "P";
        pass.renderPassEvent = kEvents[eventOf[p]];

        const auto addUsage = [&](bool isWrite, bool isBuffer, std::uint32_t index) {
            const ResourceUsage usage = isBuffer
                ? ResourceUsage::ForBuffer(BufferHandle{ index, 1 },
                      isWrite ? ResourceAccess::ComputeShaderWrite : ResourceAccess::ComputeShaderRead)
                : ResourceUsage::ForTexture(TextureHandle{ index, 1 },
                      isWrite ? ResourceAccess::ColorAttachmentWrite : ResourceAccess::ShaderRead);
            (isWrite ? pass.writes : pass.reads).push_back(usage);
        };

        const std::uint32_t readCount = pick(0, 3);
        for (std::uint32_t r = 0; r < readCount; ++r) {
            const bool isBuffer = pick(0, 1) == 1;
            const std::uint32_t index = pick(0, static_cast<std::uint32_t>((isBuffer ? input.buffers.size() : input.textures.size()) - 1));
            const bool neverWritten = isBuffer ? bufferNeverWritten[index] : textureNeverWritten[index];
            const bool written = isBuffer ? bufferWritten[index] : textureWritten[index];
            if (neverWritten || written) {
                addUsage(false, isBuffer, index);
            }
        }

        const std::uint32_t writeCount = pick(0, 2);
        for (std::uint32_t w = 0; w < writeCount; ++w) {
            const bool isBuffer = pick(0, 1) == 1;
            const std::uint32_t index = pick(0, static_cast<std::uint32_t>((isBuffer ? input.buffers.size() : input.textures.size()) - 1));
            if (isBuffer ? bufferNeverWritten[index] : textureNeverWritten[index]) {
                continue;
            }
            addUsage(true, isBuffer, index);
            // Sometimes the same pass also reads what it writes (read-modify-write).
            if (pick(0, 5) == 0) {
                addUsage(false, isBuffer, index);
            }
            (isBuffer ? bufferWritten : textureWritten)[index] = true;
        }
    }

    // Declaration order: interleave the event groups at random, keeping each group's own order.
    std::vector<std::vector<std::uint32_t>> groups(kEventCount);
    for (std::uint32_t p = 0; p < passCount; ++p) {
        groups[eventOf[p]].push_back(p);
    }
    std::vector<std::size_t> cursor(kEventCount, 0);
    for (std::uint32_t d = 0; d < passCount; ++d) {
        std::vector<std::uint32_t> open;
        for (std::uint32_t e = 0; e < kEventCount; ++e) {
            if (cursor[e] < groups[e].size()) {
                open.push_back(e);
            }
        }
        const std::uint32_t chosen = open[pick(0, static_cast<std::uint32_t>(open.size() - 1))];
        input.passes.push_back(std::move(program[groups[chosen][cursor[chosen]++]]));
    }

    // Roots: a random subset of textures (some through the span argument) and buffers.
    for (std::uint32_t t = 0; t < input.textures.size(); ++t) {
        const std::uint32_t roll = pick(0, 9);
        if (roll < 1) {
            input.finalTextureOutputs.push_back(TextureHandle{ t, 1 });
        } else if (roll < 2) {
            graph.finalOutputs.push_back(TextureHandle{ t, 1 });
        } else if (roll < 3) {
            input.persistentCacheTextures.push_back(PersistentTextureUse{ TextureHandle{ t, 1 }, {} });
        }
    }
    for (std::uint32_t b = 0; b < input.buffers.size(); ++b) {
        if (pick(0, 4) == 0) {
            input.finalBufferOutputs.push_back(BufferHandle{ b, 1 });
        }
    }
    return graph;
}

bool SameLifetimes(const std::vector<ResourceLifetime>& a, const std::vector<ResourceLifetime>& b)
{
    return a == b;
}

// Checks one random graph against every promise; returns a message for the first broken one.
std::string CheckRandomGraph(std::uint32_t seed, const RandomGraphShape& shape)
{
    RandomGraph graph = MakeRandomGraph(seed, shape);
    RandomGraph twin = MakeRandomGraph(seed, shape);

    const std::vector<bool> expectedKept = ReferenceKept(graph.input, graph.finalOutputs);
    const CompiledGraph compiled = Compile(graph.input, graph.finalOutputs);
    const CompiledGraph again = Compile(twin.input, twin.finalOutputs);
    const std::vector<std::int32_t> order = ToIndices(compiled.executionOrder);

    if (order != ToIndices(again.executionOrder)) {
        return "two compiles of the same graph disagree on the order";
    }
    for (std::size_t i = 0; i < graph.input.passes.size(); ++i) {
        if (graph.input.passes[i].isCulled != !expectedKept[i] || twin.input.passes[i].isCulled != graph.input.passes[i].isCulled) {
            return "culled set differs from the dependency-only reference";
        }
    }
    if (order != ExpectedOrder(graph.input, expectedKept)) {
        return "order is not the kept passes in program order";
    }
    if (!FindHazards(graph.input, order).empty()) {
        return "a hazard remains in the compiled order";
    }
    if (!SameLifetimes(compiled.textureLifetimes, ExpectedLifetimes(graph.input, order, false))
        || !SameLifetimes(compiled.bufferLifetimes, ExpectedLifetimes(graph.input, order, true))
        || !SameLifetimes(compiled.textureLifetimes, again.textureLifetimes)
        || !SameLifetimes(compiled.bufferLifetimes, again.bufferLifetimes)) {
        return "lifetimes differ from the reference";
    }
    return {};
}

// --- Simple cases -----------------------------------------------------------------

TEST(RenderGraphCompilerWarTest, ReaderIsOrderedBeforeTheNextWriter)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle t = g.Tex();
    g.KeepBuffer(b);
    const std::int32_t w0 = g.Pass({}, { Wr(b) });
    const std::int32_t r = g.Pass({ Rd(b) }, { Wr(t) });
    const std::int32_t w1 = g.Pass({}, { Wr(b) });
    const TextureHandle finalOutputs[] = { t };

    const CompiledGraph compiled = Compile(g.input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2 }));
    EXPECT_TRUE(FindHazards(g.input, ToIndices(compiled.executionOrder)).empty());
    EXPECT_EQ(compiled.bufferLifetimes[b.index].firstUsePassIndex, static_cast<std::int32_t>(w0));
    EXPECT_EQ(compiled.bufferLifetimes[b.index].lastUsePassIndex, static_cast<std::int32_t>(w1));
    EXPECT_FALSE(g.input.passes[static_cast<std::size_t>(r)].isCulled);
}

TEST(RenderGraphCompilerWarTest, EveryReaderSinceTheLastWriteIsOrderedBeforeTheNextWriter)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle t1 = g.Tex();
    const TextureHandle t2 = g.Tex();
    const TextureHandle t3 = g.Tex();
    g.KeepBuffer(b);
    g.Pass({}, { Wr(b) });
    g.Pass({ Rd(b) }, { Wr(t1) });
    g.Pass({ Rd(b) }, { Wr(t2) });
    g.Pass({ Rd(b) }, { Wr(t3) });
    g.Pass({}, { Wr(b) });
    const TextureHandle finalOutputs[] = { t1, t2, t3 };

    const CompiledGraph compiled = Compile(g.input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2, 3, 4 }));
    EXPECT_TRUE(FindHazards(g.input, ToIndices(compiled.executionOrder)).empty());
}

TEST(RenderGraphCompilerWarTest, APassThatReadsAndWritesOneResourceHasNoSelfHazardOrCycle)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    g.KeepBuffer(b);
    g.Pass({}, { Wr(b) });
    g.Pass({ Rd(b) }, { Wr(b) });
    g.Pass({}, { Wr(b) });

    const std::vector<std::int32_t> order = CompileAndGetOrder(g);

    EXPECT_EQ(order, (std::vector<std::int32_t>{ 0, 1, 2 }));
    EXPECT_TRUE(FindHazards(g.input, order).empty());
}

TEST(RenderGraphCompilerWarTest, WriteReadWriteReadChainKeepsEveryHazardKindOrdered)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle t1 = g.Tex();
    const TextureHandle t2 = g.Tex();
    g.KeepBuffer(b);
    g.Pass({}, { Wr(b) });
    g.Pass({ Rd(b) }, { Wr(t1) });
    g.Pass({}, { Wr(b) });
    g.Pass({ Rd(b) }, { Wr(t2) });
    const TextureHandle finalOutputs[] = { t1, t2 };

    const CompiledGraph compiled = Compile(g.input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2, 3 }));
    EXPECT_TRUE(FindHazards(g.input, ToIndices(compiled.executionOrder)).empty());
}

TEST(RenderGraphCompilerWarTest, TextureAndBufferReadersAreBothOrderedBeforeTheNextWriter)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle src = g.Tex();
    const TextureHandle out = g.Tex();
    g.KeepBuffer(b);
    g.Pass({}, { Wr(b), Wr(src) });
    g.Pass({ Rd(b), Rd(src) }, { Wr(out) });
    g.Pass({}, { Wr(b), Wr(src) });
    g.KeepTexture(src);
    const TextureHandle finalOutputs[] = { out };

    const CompiledGraph compiled = Compile(g.input, finalOutputs);

    EXPECT_TRUE(ExecutionOrderEquals(compiled.executionOrder, { 0, 1, 2 }));
    EXPECT_TRUE(FindHazards(g.input, ToIndices(compiled.executionOrder)).empty());
}

// --- Culling must not change ------------------------------------------------------

TEST(RenderGraphCompilerWarTest, ACulledReaderStaysCulledWhenALaterWriterIsKept)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle unused = g.Tex();
    g.KeepBuffer(b);
    g.Pass({}, { Wr(b) });
    const std::int32_t reader = g.Pass({ Rd(b) }, { Wr(unused) });
    g.Pass({}, { Wr(b) });

    const std::vector<std::int32_t> order = CompileAndGetOrder(g);

    EXPECT_TRUE(g.input.passes[static_cast<std::size_t>(reader)].isCulled);
    EXPECT_EQ(order, (std::vector<std::int32_t>{ 0, 2 }));
    EXPECT_TRUE(FindHazards(g.input, order).empty());
}

TEST(RenderGraphCompilerWarTest, ACulledReaderContributesNoLifetime)
{
    TestGraph g;
    const BufferHandle kept = g.Buf();
    const BufferHandle onlyForCulled = g.Buf();
    const TextureHandle unused = g.Tex();
    g.KeepBuffer(kept);
    g.Pass({}, { Wr(kept) });
    g.Pass({ Rd(kept), Rd(onlyForCulled) }, { Wr(unused) });
    g.Pass({}, { Wr(kept) });

    const CompiledGraph compiled = Compile(g.input, {});

    EXPECT_EQ(compiled.bufferLifetimes[kept.index].lastUsePassIndex, 1);
    EXPECT_EQ(compiled.bufferLifetimes[onlyForCulled.index].firstUsePassIndex, -1);
    EXPECT_EQ(compiled.textureLifetimes[unused.index].firstUsePassIndex, -1);
}

TEST(RenderGraphCompilerWarTest, AKeptReaderDoesNotKeepACulledWriterAlive)
{
    TestGraph g;
    const BufferHandle source = g.Buf();
    const BufferHandle scratch = g.Buf();
    const TextureHandle out = g.Tex();
    g.KeepBuffer(source);
    g.Pass({}, { Wr(source) });
    g.Pass({ Rd(source) }, { Wr(out) });
    const std::int32_t deadWriter = g.Pass({}, { Wr(scratch) });
    const TextureHandle finalOutputs[] = { out };

    const std::vector<std::int32_t> order = CompileAndGetOrder(g, finalOutputs);

    EXPECT_TRUE(g.input.passes[static_cast<std::size_t>(deadWriter)].isCulled);
    EXPECT_EQ(order, (std::vector<std::int32_t>{ 0, 1 }));
}

// --- Events ---------------------------------------------------------------------------

TEST(RenderGraphCompilerWarTest, ATierThatSortsTheWriterFirstTurnsTheReaderIntoAReadAfterWrite)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle out = g.Tex();
    g.KeepBuffer(b);
    g.Pass(RenderPassEvent::AfterTransparents, { Rd(b) }, { Wr(out) });
    g.Pass(RenderPassEvent::Opaques, {}, { Wr(b) });
    const TextureHandle finalOutputs[] = { out };

    const std::vector<std::int32_t> order = CompileAndGetOrder(g, finalOutputs);

    EXPECT_EQ(order, (std::vector<std::int32_t>{ 1, 0 }));
    EXPECT_TRUE(FindHazards(g.input, order).empty());
}

TEST(RenderGraphCompilerWarTest, IndependentWritersInOneTierFollowDeclarationOrder)
{
    TestGraph g;
    const BufferHandle a = g.Buf();
    const BufferHandle b = g.Buf();
    g.KeepBuffer(a);
    g.KeepBuffer(b);
    g.Pass({}, { Wr(b) });
    g.Pass({}, { Wr(a) });
    g.Pass({}, { Wr(b) });

    EXPECT_EQ(CompileAndGetOrder(g), (std::vector<std::int32_t>{ 0, 1, 2 }));
}

TEST(RenderGraphCompilerWarTest, IndependentPassesKeepTheirDeclarationOrderForAnyPermutation)
{
    std::vector<std::int32_t> labels = { 0, 1, 2, 3, 4, 5 };
    std::mt19937 rng(7);
    for (int round = 0; round < 6; ++round) {
        std::shuffle(labels.begin(), labels.end(), rng);
        TestGraph g;
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const BufferHandle b = g.Buf();
            g.KeepBuffer(b);
            g.Pass({}, { Wr(b) });
        }
        const std::vector<std::int32_t> order = CompileAndGetOrder(g);
        EXPECT_EQ(order, (std::vector<std::int32_t>{ 0, 1, 2, 3, 4, 5 }));
    }
}

#ifdef NDEBUG
// An orphan read (producer declared later in the same tier) gets no edge; the reader runs first.
TEST(RenderGraphCompilerWarTest, AnOrphanReadKeepsDeclarationOrderInReleaseBuilds)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle out = g.Tex();
    g.KeepBuffer(b);
    g.Pass({ Rd(b) }, { Wr(out) });
    g.Pass({}, { Wr(b) });
    const TextureHandle finalOutputs[] = { out };

    EXPECT_EQ(CompileAndGetOrder(g, finalOutputs), (std::vector<std::int32_t>{ 0, 1 }));
}
#endif

// --- Lanes that share buffers ------------------------------------------------------------

struct SharedLanes {
    TestGraph graph;
    BufferHandle count;
    BufferHandle commands;
    BufferHandle visible;
    std::vector<TextureHandle> targets;
    std::vector<std::int32_t> reset;
    std::vector<std::int32_t> cull;
    std::vector<std::int32_t> draw;
};

ResourceUsage IndirectRead(BufferHandle b) { return ResourceUsage::ForBuffer(b, ResourceAccess::IndirectCommandRead); }
ResourceUsage VertexRead(BufferHandle b) { return ResourceUsage::ForBuffer(b, ResourceAccess::VertexShaderStorageRead); }
ResourceUsage FillWrite(BufferHandle b) { return ResourceUsage::ForBuffer(b, ResourceAccess::TransferDst); }

// Four lanes use one count, commands and visible buffer: Reset writes count, Cull reads and writes
// count and writes commands and visible, Draw reads all three and writes its own target.
// `laneMajor` declares lane by lane; otherwise all Resets, then all Culls, then all Draws.
SharedLanes MakeSharedLanes(bool laneMajor, RenderPassEvent drawEvent = RenderPassEvent::Opaques)
{
    constexpr std::size_t kLanes = 4;
    SharedLanes s;
    TestGraph& g = s.graph;
    s.count = g.Buf();
    s.commands = g.Buf();
    s.visible = g.Buf();
    g.KeepBuffer(s.count);
    g.KeepBuffer(s.commands);
    g.KeepBuffer(s.visible);
    for (std::size_t lane = 0; lane < kLanes; ++lane) {
        s.targets.push_back(g.Tex());
        g.KeepTexture(s.targets.back());
    }
    s.reset.assign(kLanes, -1);
    s.cull.assign(kLanes, -1);
    s.draw.assign(kLanes, -1);

    const auto declareReset = [&](std::size_t lane) {
        s.reset[lane] = g.Pass(RenderPassEvent::Opaques, {}, { FillWrite(s.count) });
    };
    const auto declareCull = [&](std::size_t lane) {
        s.cull[lane] = g.Pass(RenderPassEvent::Opaques, { Rd(s.count) }, { Wr(s.count), Wr(s.commands), Wr(s.visible) });
    };
    const auto declareDraw = [&](std::size_t lane) {
        s.draw[lane] = g.Pass(drawEvent, { IndirectRead(s.count), IndirectRead(s.commands), VertexRead(s.visible) },
            { Wr(s.targets[lane]) });
    };

    if (laneMajor) {
        for (std::size_t lane = 0; lane < kLanes; ++lane) {
            declareReset(lane);
            declareCull(lane);
            declareDraw(lane);
        }
    } else {
        for (std::size_t lane = 0; lane < kLanes; ++lane) {
            declareReset(lane);
        }
        for (std::size_t lane = 0; lane < kLanes; ++lane) {
            declareCull(lane);
        }
        for (std::size_t lane = 0; lane < kLanes; ++lane) {
            declareDraw(lane);
        }
    }
    return s;
}

TEST(RenderGraphCompilerWarTest, FourSharedLanesDeclaredLaneByLaneAreHazardFree)
{
    SharedLanes s = MakeSharedLanes(true);

    const std::vector<std::int32_t> order = CompileAndGetOrder(s.graph);

    EXPECT_EQ(order.size(), 12u);
    EXPECT_TRUE(FindHazards(s.graph.input, order).empty());
    for (std::size_t lane = 0; lane + 1 < 4; ++lane) {
        const auto pos = [&](std::int32_t pass) { return std::find(order.begin(), order.end(), pass) - order.begin(); };
        EXPECT_LT(pos(s.draw[lane]), pos(s.reset[lane + 1]));
        EXPECT_LT(pos(s.draw[lane]), pos(s.cull[lane + 1]));
    }
}

// Declaration order is the program: a pass-kind-major declaration is a valid program in which every
// Draw reads what the LAST Cull wrote. Compile() cannot know the author meant lane by lane.
TEST(RenderGraphCompilerWarTest, FourSharedLanesDeclaredByPassKindLetEveryDrawReadTheLastCull)
{
    SharedLanes s = MakeSharedLanes(false);

    const std::vector<std::int32_t> order = CompileAndGetOrder(s.graph);
    const auto pos = [&](std::int32_t pass) { return std::find(order.begin(), order.end(), pass) - order.begin(); };

    EXPECT_EQ(order.size(), 12u);
    EXPECT_TRUE(FindHazards(s.graph.input, order).empty());
    EXPECT_GT(pos(s.draw[0]), pos(s.cull[3]));
}

TEST(RenderGraphCompilerWarTest, ACulledDrawInASharedLaneStaysCulledAndNextLanesStillRun)
{
    SharedLanes s = MakeSharedLanes(true);
    // Lane 1 draws into a target nobody keeps.
    s.graph.input.finalTextureOutputs.erase(s.graph.input.finalTextureOutputs.begin() + 1);

    const std::vector<std::int32_t> order = CompileAndGetOrder(s.graph);

    EXPECT_TRUE(s.graph.input.passes[static_cast<std::size_t>(s.draw[1])].isCulled);
    EXPECT_FALSE(s.graph.input.passes[static_cast<std::size_t>(s.reset[2])].isCulled);
    EXPECT_FALSE(s.graph.input.passes[static_cast<std::size_t>(s.cull[3])].isCulled);
    EXPECT_EQ(order.size(), 11u);
    EXPECT_TRUE(FindHazards(s.graph.input, order).empty());
}

// The event tag is part of program order: a Draw tagged later than the lane Resets runs after all of them.
TEST(RenderGraphCompilerWarTest, ALaneDrawWithALaterEventThanTheLaneResetsRunsAfterEveryReset)
{
    SharedLanes s = MakeSharedLanes(true, RenderPassEvent::AfterTransparents);

    const std::vector<std::int32_t> order = CompileAndGetOrder(s.graph);
    const auto pos = [&](std::int32_t pass) { return std::find(order.begin(), order.end(), pass) - order.begin(); };

    EXPECT_GT(pos(s.draw[0]), pos(s.reset[3]));
    EXPECT_TRUE(FindHazards(s.graph.input, order).empty());
}

TEST(RenderGraphCompilerWarTest, SharedBufferLifetimeEndsAtTheLastLaneDraw)
{
    SharedLanes s = MakeSharedLanes(true);

    const CompiledGraph compiled = Compile(s.graph.input, {});

    EXPECT_EQ(compiled.bufferLifetimes[s.count.index].firstUsePassIndex, 0);
    EXPECT_EQ(compiled.bufferLifetimes[s.count.index].lastUsePassIndex, 11);
    EXPECT_EQ(compiled.bufferLifetimes[s.visible.index].lastUsePassIndex, 11);
}

// --- Same physical buffer imported twice ---------------------------------------------------

TEST(RenderGraphCompilerWarTest, ImportingTheSameBufferTwiceCreatesTwoUnrelatedResources)
{
    RenderGraphBuilder builder;
    const BufferObjectHandle physical = static_cast<BufferObjectHandle>(42);
    const BufferHandle first = builder.ImportBuffer("First", physical, 64);
    const BufferHandle second = builder.ImportBuffer("Second", physical, 64);

    EXPECT_NE(first.index, second.index);
}

// --- Oracle self-tests: it must be able to fail ----------------------------------------------

TEST(RenderGraphCompilerWarTest, OracleAcceptsTheCompiledOrderOfAShareChain)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    g.KeepBuffer(b);
    g.Pass({}, { Wr(b) });
    g.Pass({ Rd(b) }, { Wr(b) });

    EXPECT_TRUE(FindHazards(g.input, { 0, 1 }).empty());
}

TEST(RenderGraphCompilerWarTest, OracleReportsAReaderThatRunsAfterTheNextWriter)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle t = g.Tex();
    const std::int32_t w0 = g.Pass({}, { Wr(b) });
    const std::int32_t reader = g.Pass({ Rd(b) }, { Wr(t) });
    const std::int32_t w1 = g.Pass({}, { Wr(b) });

    const std::vector<Hazard> hazards = FindHazards(g.input, { w0, w1, reader });

    EXPECT_TRUE(HasHazard(hazards, HazardKind::WriteAfterRead, reader, w1));
    EXPECT_FALSE(HasHazard(hazards, HazardKind::ReadAfterWrite, w0, reader));
}

TEST(RenderGraphCompilerWarTest, OracleReportsAReaderThatRunsBeforeItsWriter)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle t = g.Tex();
    const std::int32_t writer = g.Pass({}, { Wr(b) });
    const std::int32_t reader = g.Pass({ Rd(b) }, { Wr(t) });

    EXPECT_TRUE(HasHazard(FindHazards(g.input, { reader, writer }), HazardKind::ReadAfterWrite, writer, reader));
}

TEST(RenderGraphCompilerWarTest, OracleReportsTwoWritersThatSwapped)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const std::int32_t w0 = g.Pass({}, { Wr(b) });
    const std::int32_t w1 = g.Pass({}, { Wr(b) });

    EXPECT_TRUE(HasHazard(FindHazards(g.input, { w1, w0 }), HazardKind::WriteAfterWrite, w0, w1));
}

TEST(RenderGraphCompilerWarTest, OracleReportsAKeptReaderWhoseWriterWasDropped)
{
    TestGraph g;
    const BufferHandle b = g.Buf();
    const TextureHandle t = g.Tex();
    const std::int32_t writer = g.Pass({}, { Wr(b) });
    const std::int32_t reader = g.Pass({ Rd(b) }, { Wr(t) });

    EXPECT_TRUE(HasHazard(FindHazards(g.input, { reader }), HazardKind::ReaderLostItsWriter, writer, reader));
}

TEST(RenderGraphCompilerWarTest, OracleReportsAWriteAfterReadOnTextures)
{
    TestGraph g;
    const TextureHandle t = g.Tex();
    const TextureHandle out = g.Tex();
    const std::int32_t w0 = g.Pass({}, { Wr(t) });
    const std::int32_t reader = g.Pass({ Rd(t) }, { Wr(out) });
    const std::int32_t w1 = g.Pass({}, { Wr(t) });

    const std::vector<Hazard> hazards = FindHazards(g.input, { w0, w1, reader });

    ASSERT_TRUE(HasHazard(hazards, HazardKind::WriteAfterRead, reader, w1));
    EXPECT_TRUE(std::none_of(hazards.begin(), hazards.end(), [](const Hazard& h) { return h.isBuffer; }));
}

// --- Determinism and random graphs --------------------------------------------------------------

TEST(RenderGraphCompilerWarTest, CompilingTheSameGraphTwiceGivesTheSameOrderCullingAndLifetimes)
{
    SharedLanes first = MakeSharedLanes(true);
    SharedLanes second = MakeSharedLanes(true);
    first.graph.input.finalTextureOutputs.pop_back();
    second.graph.input.finalTextureOutputs.pop_back();

    const CompiledGraph a = Compile(first.graph.input, {});
    const CompiledGraph b = Compile(second.graph.input, {});

    EXPECT_EQ(ToIndices(a.executionOrder), ToIndices(b.executionOrder));
    EXPECT_EQ(a.bufferLifetimes, b.bufferLifetimes);
    EXPECT_EQ(a.textureLifetimes, b.textureLifetimes);
    for (std::size_t i = 0; i < first.graph.input.passes.size(); ++i) {
        EXPECT_EQ(first.graph.input.passes[i].isCulled, second.graph.input.passes[i].isCulled);
    }
}

TEST(RenderGraphCompilerWarTest, RandomGraphsAreHazardFreeDeterministicAndCulledLikeTheDependencyOnlyModel)
{
    for (std::uint32_t seed = 1; seed <= 3000; ++seed) {
        const std::string failure = CheckRandomGraph(seed, RandomGraphShape{});
        ASSERT_TRUE(failure.empty()) << "seed " << seed << ": " << failure;
    }
}

TEST(RenderGraphCompilerWarTest, LargeRandomGraphsStayHazardFree)
{
    RandomGraphShape shape;
    shape.minPasses = 3000;
    shape.maxPasses = 5000;
    shape.maxTextures = 25;
    shape.maxBuffers = 25;
    for (std::uint32_t seed = 1; seed <= 3; ++seed) {
        const std::string failure = CheckRandomGraph(seed, shape);
        ASSERT_TRUE(failure.empty()) << "seed " << seed << ": " << failure;
    }
}

// Moving a reader right after its next writer must always be reported as a write-after-read hazard.
TEST(RenderGraphCompilerWarTest, MovingAReaderBehindItsNextWriterIsAlwaysDetected)
{
    std::size_t mutations = 0;
    for (std::uint32_t seed = 1; seed <= 1500; ++seed) {
        RandomGraph graph = MakeRandomGraph(seed);
        const std::vector<std::int32_t> order = ToIndices(Compile(graph.input, graph.finalOutputs).executionOrder);
        for (std::size_t pos = 0; pos < order.size(); ++pos) {
            const std::int32_t reader = order[pos];
            for (const ResourceUsage& read : graph.input.passes[static_cast<std::size_t>(reader)].reads) {
                // Next executed writer of the same resource after the reader.
                for (std::size_t later = pos + 1; later < order.size(); ++later) {
                    const PassRecord& candidate = graph.input.passes[static_cast<std::size_t>(order[later])];
                    const bool writesIt = std::any_of(candidate.writes.begin(), candidate.writes.end(), [&](const ResourceUsage& w) {
                        return w.kind == read.kind
                            && (read.kind == ResourceKind::Buffer ? w.buffer == read.buffer : w.texture == read.texture);
                    });
                    if (!writesIt) {
                        continue;
                    }
                    std::vector<std::int32_t> mutated = order;
                    mutated.erase(mutated.begin() + static_cast<std::ptrdiff_t>(pos));
                    mutated.insert(mutated.begin() + static_cast<std::ptrdiff_t>(later), reader);
                    const std::vector<Hazard> hazards = FindHazards(graph.input, mutated);
                    ASSERT_FALSE(hazards.empty()) << "seed " << seed;
                    ++mutations;
                    break;
                }
            }
        }
    }
    EXPECT_GT(mutations, 100u);
}



} // namespace
} // namespace gte::rg
