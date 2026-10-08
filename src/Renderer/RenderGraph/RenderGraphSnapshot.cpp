#include "RenderGraphSnapshot.h"

namespace gte::rg {

// See this function's own doc comment in RenderGraphSnapshot.h for the
// full contract.
PassGpuStats CombinePassGpuStats(const std::vector<PassGpuStats>& stats)
{
    PassGpuStats combined;

    bool anyPresent = false;
    bool anyUnsupported = false;
    double summedPresentMilliseconds = 0.0;

    for (const PassGpuStats& entry : stats) {
        combined.drawStats.drawCallCount += entry.drawStats.drawCallCount;
        combined.drawStats.triangleCount += entry.drawStats.triangleCount;

        if (entry.timing.status == GpuTimingSample::Status::Present) {
            anyPresent = true;
            summedPresentMilliseconds += entry.timing.milliseconds;
        } else if (entry.timing.status == GpuTimingSample::Status::Unsupported) {
            anyUnsupported = true;
        }
    }

    if (anyPresent) {
        combined.timing.status = GpuTimingSample::Status::Present;
        combined.timing.milliseconds = summedPresentMilliseconds;
    } else if (anyUnsupported) {
        combined.timing.status = GpuTimingSample::Status::Unsupported;
    }
    // else: leave combined.timing at its default (Status::Absent, 0.0).

    return combined;
}

// Resolves one declared read/write's resource name, from whichever of
// CompiledGraphInput's texture/buffer/volume-texture/texture-array tables
// actually applies to its kind. Never reads out of bounds (a stale/invalid
// index degrades to an empty string rather than crashing) - a display
// helper, never an assertion. Exported so RenderGraph.cpp's own
// barrier-applied sink call resolves a usage's name through this exact
// same function, never a second copy.
std::string ResourceUsageName(const ResourceUsage& usage, const CompiledGraphInput& input)
{
    std::string name;
    DispatchByKind(usage,
        [&](TextureHandle h) {
            if (h.index < input.textures.size() && input.textures[h.index].name != nullptr) {
                name = input.textures[h.index].name;
            }
        },
        [&](BufferHandle h) {
            if (h.index < input.buffers.size() && input.buffers[h.index].name != nullptr) {
                name = input.buffers[h.index].name;
            }
        },
        [&](VolumeTextureHandle h) {
            if (h.index < input.volumeTextures.size() && input.volumeTextures[h.index].name != nullptr) {
                name = input.volumeTextures[h.index].name;
            }
        },
        [&](TextureArrayHandle h) {
            if (h.index < input.textureArrays.size() && input.textureArrays[h.index].name != nullptr) {
                name = input.textureArrays[h.index].name;
            }
        });
    return name;
}

namespace {

RenderGraphPassSnapshot BuildPassSnapshot(const PassRecord& pass, std::size_t declarationIndex,
    const CompiledGraphInput& input, bool isCulled, const std::function<PassGpuStats(const char*)>& statsLookup,
    const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup,
    const std::function<bool(std::size_t, const std::string&, std::string&)>& barrierLabelLookup)
{
    RenderGraphPassSnapshot snapshot;
    snapshot.name = pass.name != nullptr ? pass.name : "";
    snapshot.isCulled = isCulled;
    snapshot.kind = pass.kind;
    snapshot.viewScope = pass.viewScope;
    snapshot.renderPassEvent = pass.renderPassEvent;

    // category/drawKind/tags no longer live on PassRecord - they start at
    // RenderGraphPassSnapshot's own struct defaults (General/DrawMesh/0) and
    // are only overwritten when metadataLookup is supplied AND actually has
    // an entry for this exact declarationIndex. This is the graceful
    // fallback for a headless build (no sink installed) and for any
    // call site that does not pass a metadataLookup at all.
    if (metadataLookup) {
        PassDebugMetadata metadata;
        if (metadataLookup(declarationIndex, metadata)) {
            snapshot.category = metadata.category;
            snapshot.drawKind = metadata.drawKind;
            snapshot.tags = metadata.tags;
        }
    }

    snapshot.readNames.reserve(pass.reads.size());
    snapshot.readKinds.reserve(pass.reads.size());
    snapshot.readAccess.reserve(pass.reads.size());
    for (const ResourceUsage& usage : pass.reads) {
        snapshot.readNames.push_back(ResourceUsageName(usage, input));
        snapshot.readKinds.push_back(usage.kind);
        snapshot.readAccess.push_back(usage.access);
    }

    snapshot.writeNames.reserve(pass.writes.size());
    snapshot.writeKinds.reserve(pass.writes.size());
    snapshot.writeAccess.reserve(pass.writes.size());
    snapshot.writeBarrierLabels.reserve(pass.writes.size());
    for (const ResourceUsage& usage : pass.writes) {
        const std::string writeName = ResourceUsageName(usage, input);
        snapshot.writeNames.push_back(writeName);
        snapshot.writeKinds.push_back(usage.kind);
        snapshot.writeAccess.push_back(usage.access);

        std::string barrierLabel;
        if (barrierLabelLookup && barrierLabelLookup(declarationIndex, writeName, barrierLabel)) {
            snapshot.writeBarrierLabels.push_back(std::move(barrierLabel));
        } else {
            snapshot.writeBarrierLabels.emplace_back();
        }
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
    const std::function<PassGpuStats(const char*)>& statsLookup, bool timingSlotBudgetExhausted,
    const std::function<bool(std::size_t, PassDebugMetadata&)>& metadataLookup,
    const std::function<bool(std::size_t, const std::string&, std::string&)>& barrierLabelLookup)
{
    RenderGraphSnapshot snapshot;
    snapshot.timingSlotBudgetExhausted = timingSlotBudgetExhausted;
    snapshot.passesInExecutionOrder.reserve(compiled.executionOrder.size() + input.passes.size());

    // Surviving passes first, in real execution order. handle.index IS this
    // pass's own declarationIndex - the same index space
    // CompiledGraphInput::passes already uses (PassHandle::index), so no
    // new correlation problem is introduced.
    for (const PassHandle& handle : compiled.executionOrder) {
        if (handle.index >= input.passes.size()) {
            continue; // Defensive - never expected against a real Compile() result.
        }
        snapshot.passesInExecutionOrder.push_back(BuildPassSnapshot(input.passes[handle.index], handle.index, input,
            /*isCulled=*/false, statsLookup, metadataLookup, barrierLabelLookup));
    }

    // Culled passes appended afterwards, in their original declaration
    // order - still visible, per this file's own header comment. The loop
    // index IS this pass's own declarationIndex.
    for (std::size_t i = 0; i < input.passes.size(); ++i) {
        const PassRecord& pass = input.passes[i];
        if (!pass.isCulled) {
            continue;
        }
        snapshot.passesInExecutionOrder.push_back(
            BuildPassSnapshot(pass, i, input, /*isCulled=*/true, statsLookup, metadataLookup, barrierLabelLookup));
    }

    snapshot.resources.reserve(input.textures.size() + input.buffers.size());

    for (std::size_t i = 0; i < input.textures.size(); ++i) {
        RenderGraphResourceSnapshot resource;
        resource.name = input.textures[i].name != nullptr ? input.textures[i].name : "";
        resource.isImported = input.textures[i].importInfo.isImported;
        if (i < compiled.textureLifetimes.size()) {
            resource.firstUsePassIndex = compiled.textureLifetimes[i].firstUsePassIndex;
            resource.lastUsePassIndex = compiled.textureLifetimes[i].lastUsePassIndex;
        }
        snapshot.resources.push_back(std::move(resource));
    }

    for (std::size_t i = 0; i < input.buffers.size(); ++i) {
        RenderGraphResourceSnapshot resource;
        resource.name = input.buffers[i].name != nullptr ? input.buffers[i].name : "";
        // A buffer resource can be imported too, same as a texture above.
        resource.isImported = input.buffers[i].importInfo.isImported;
        if (i < compiled.bufferLifetimes.size()) {
            resource.firstUsePassIndex = compiled.bufferLifetimes[i].firstUsePassIndex;
            resource.lastUsePassIndex = compiled.bufferLifetimes[i].lastUsePassIndex;
        }
        snapshot.resources.push_back(std::move(resource));
    }

    return snapshot;
}

} // namespace gte::rg
