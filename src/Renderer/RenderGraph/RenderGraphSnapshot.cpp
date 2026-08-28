#include "RenderGraphSnapshot.h"

namespace gte::rg {

namespace {

// Resolves one declared read/write's resource name, from whichever of
// CompiledGraphInput::textureNames/bufferNames actually applies to its kind
// - mirrors how RenderGraph.cpp itself resolves a ResourceUsage's target
// (see ApplyUsageBarrierIfNeeded()), just for a NAME instead of a physical
// resource. Never reads out of bounds (a stale/invalid index degrades to an
// empty string rather than crashing) - defensive, since this function's
// whole job is to build a DISPLAY artifact, never to assert correctness
// that Phase 1-3's own code already guarantees elsewhere.
std::string ResourceUsageName(const ResourceUsage& usage, const CompiledGraphInput& input)
{
    if (usage.kind == ResourceKind::Texture) {
        if (usage.texture.index < input.textureNames.size()) {
            const char* name = input.textureNames[usage.texture.index];
            return name != nullptr ? name : "";
        }
        return "";
    }
    if (usage.buffer.index < input.bufferNames.size()) {
        const char* name = input.bufferNames[usage.buffer.index];
        return name != nullptr ? name : "";
    }
    return "";
}

RenderGraphPassSnapshot BuildPassSnapshot(const PassRecord& pass, const CompiledGraphInput& input, bool isCulled,
    const std::function<PassGpuStats(const char*)>& statsLookup)
{
    RenderGraphPassSnapshot snapshot;
    snapshot.name = pass.name != nullptr ? pass.name : "";
    snapshot.isCulled = isCulled;

    snapshot.readNames.reserve(pass.reads.size());
    for (const ResourceUsage& usage : pass.reads) {
        snapshot.readNames.push_back(ResourceUsageName(usage, input));
    }

    snapshot.writeNames.reserve(pass.writes.size());
    for (const ResourceUsage& usage : pass.writes) {
        snapshot.writeNames.push_back(ResourceUsageName(usage, input));
    }

    // See this file's header comment - a culled pass's stats are
    // deliberately left at PassGpuStats{}'s own default, never resolved via
    // statsLookup().
    if (!isCulled && statsLookup) {
        snapshot.stats = statsLookup(pass.name);
    }

    return snapshot;
}

} // namespace

RenderGraphSnapshot BuildRenderGraphSnapshot(const CompiledGraph& compiled, const CompiledGraphInput& input,
    const std::function<PassGpuStats(const char*)>& statsLookup)
{
    RenderGraphSnapshot snapshot;
    snapshot.passesInExecutionOrder.reserve(compiled.executionOrder.size() + input.passes.size());

    // Surviving passes first, in real execution order.
    for (const PassHandle& handle : compiled.executionOrder) {
        if (handle.index >= input.passes.size()) {
            continue; // Defensive - never expected against a real Compile() result.
        }
        snapshot.passesInExecutionOrder.push_back(
            BuildPassSnapshot(input.passes[handle.index], input, /*isCulled=*/false, statsLookup));
    }

    // Culled passes appended afterwards, in their original declaration
    // order - still visible, per this file's own header comment.
    for (const PassRecord& pass : input.passes) {
        if (!pass.isCulled) {
            continue;
        }
        snapshot.passesInExecutionOrder.push_back(BuildPassSnapshot(pass, input, /*isCulled=*/true, statsLookup));
    }

    snapshot.resources.reserve(input.textureDescs.size() + input.bufferDescs.size());

    for (std::size_t i = 0; i < input.textureDescs.size(); ++i) {
        RenderGraphResourceSnapshot resource;
        resource.name = (i < input.textureNames.size() && input.textureNames[i] != nullptr) ? input.textureNames[i] : "";
        resource.isImported = (i < input.textureImportInfo.size()) && input.textureImportInfo[i].isImported;
        if (i < compiled.textureLifetimes.size()) {
            resource.firstUsePassIndex = compiled.textureLifetimes[i].firstUsePassIndex;
            resource.lastUsePassIndex = compiled.textureLifetimes[i].lastUsePassIndex;
        }
        snapshot.resources.push_back(std::move(resource));
    }

    for (std::size_t i = 0; i < input.bufferDescs.size(); ++i) {
        RenderGraphResourceSnapshot resource;
        resource.name = (i < input.bufferNames.size() && input.bufferNames[i] != nullptr) ? input.bufferNames[i] : "";
        // GPU Vertex Skinning campaign, Phase 3
        // (GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md) -
        // a buffer resource CAN now be imported (RenderGraphBuilder::
        // ImportBuffer()) - mirrors the texture branch immediately above,
        // which was already correct.
        resource.isImported = (i < input.bufferImportInfo.size()) && input.bufferImportInfo[i].isImported;
        if (i < compiled.bufferLifetimes.size()) {
            resource.firstUsePassIndex = compiled.bufferLifetimes[i].firstUsePassIndex;
            resource.lastUsePassIndex = compiled.bufferLifetimes[i].lastUsePassIndex;
        }
        snapshot.resources.push_back(std::move(resource));
    }

    return snapshot;
}

} // namespace gte::rg
